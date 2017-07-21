#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

#include "ast.h"
#include "jit.h"
#include "log.h"
#include "parser.h"

// ========================================================================
// Code generation
// ========================================================================

std::map<std::string, llvm::AllocaInst*> NamedValues;
std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

llvm::LLVMContext TheContext;
llvm::IRBuilder<> Builder(TheContext);

static llvm::AllocaInst *CreateEntryBlockAlloca(llvm::Function *TheFunction,
                                          const std::string &VarName) {
    llvm::IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                           TheFunction->getEntryBlock().begin());
    return TmpB.CreateAlloca(llvm::Type::getDoubleTy(TheContext), 0, VarName.c_str());
}

llvm::Function *getFunction(std::string Name) {
    if (auto *F = TheModule->getFunction(Name))
        return F;

    auto FI = FunctionProtos.find(Name);
    if (FI != FunctionProtos.end())
        return FI->second->codegen();

    return nullptr;
}

llvm::Value *NumberExprAST::codegen() {
    return llvm::ConstantFP::get(TheContext, llvm::APFloat(Val));
}

llvm::Value *VariableExprAST::codegen() {
    llvm::Value *V = NamedValues[Name];
    if (!V)
        LogErrorV("unknown variable name");

    return Builder.CreateLoad(V, Name.c_str());
}

llvm::Value *VarExprAST::codegen() {
    std::vector<llvm::AllocaInst *> OldBindings;

    llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();

    for (unsigned i = 0, e = VarNames.size(); i != e; ++i) {
        const std::string &VarName = VarNames[i].first;
        ExprAST *Init = VarNames[i].second.get();

        llvm::Value *InitVal;

        if(Init) {
            InitVal = Init->codegen();
            if(!InitVal)
                return nullptr;
        } else {
            InitVal = llvm::ConstantFP::get(TheContext, llvm::APFloat(0.0));
        }

        llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
        Builder.CreateStore(InitVal, Alloca);

        OldBindings.push_back(NamedValues[VarName]);

        NamedValues[VarName] = Alloca;
    }

    llvm::Value *BodyVal = Body->codegen();
    if(!BodyVal)
        return nullptr;

    for(unsigned i = 0, e = VarNames.size(); i != e; ++i)
        NamedValues[VarNames[i].first] = OldBindings[i];

    return BodyVal;
}

llvm::Value *BinaryExprAST::codegen() {
    if(Op == '=') {
        VariableExprAST *LHSE = dynamic_cast<VariableExprAST*>(LHS.get());
        if(!LHSE)
            return LogErrorV("destination of '=' must be a variable");

        llvm::Value *Val = RHS->codegen();
        if(!Val)
            return nullptr;

        llvm::Value *Variable = NamedValues[LHSE->getName()];
        if(!Variable)
            return LogErrorV("Unknown variable name");

        Builder.CreateStore(Val, Variable);
        return Val;
    }

    llvm::Value *L = LHS->codegen();
    llvm::Value *R = RHS->codegen();

    if (!R || !L)
        return nullptr;

    switch (Op) {
    case '+':
        return Builder.CreateFAdd(L, R, "addtmp");
    case '-':
        return Builder.CreateFSub(L, R, "subtmp");
    case '*':
        return Builder.CreateFMul(L, R, "multmp");
    case '<':
        L = Builder.CreateFCmpULT(L, R, "cmptmp");
        return Builder.CreateUIToFP(L, llvm::Type::getDoubleTy(TheContext),
                                    "booltmp");
    default:
        break;
    }

    llvm::Function *F = getFunction(std::string("binary") + Op);
    assert(F && "binary operator not found");

    llvm::Value *Ops[2] = {L, R};
    return Builder.CreateCall(F, Ops, "binop");
}

llvm::Value *UnaryExprAST::codegen() {
    llvm::Value *OperandV = Operand->codegen();
    if (!OperandV)
        return nullptr;

    llvm::Function *F = getFunction(std::string("unary") + Opcode);
    if (!F)
        return LogErrorV("Unknown unary operator");

    return Builder.CreateCall(F, OperandV, "unop");
}

llvm::Value *CallExprAST::codegen() {
    llvm::Function *CalleeF = getFunction(Callee);
    if (!CalleeF)
        return LogErrorV("unknown function referenced");

    if (CalleeF->arg_size() != Args.size())
        return LogErrorV("incorrect number of arguments passed");

    std::vector<llvm::Value *> ArgsV;
    for (unsigned i = 0, e = Args.size(); i != e; i += 1) {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back())
            return nullptr;
    }

    return Builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

llvm::Function *PrototypeAST::codegen() {
    std::vector<llvm::Type *> Doubles(Args.size(),
                                      llvm::Type::getDoubleTy(TheContext));

    llvm::FunctionType *FT = llvm::FunctionType::get(
            llvm::Type::getDoubleTy(TheContext), Doubles, false);

    llvm::Function *F = llvm::Function::Create(
            FT, llvm::Function::ExternalLinkage, Name, TheModule.get());

    unsigned Idx = 0;
    for (auto &Arg : F->args())
        Arg.setName(Args[Idx++]);

    return F;
}

llvm::Function *FunctionAST::codegen() {
    auto &P = *Proto;
    FunctionProtos[Proto->getName()] = std::move(Proto);
    llvm::Function *TheFunction = getFunction(P.getName());

    if (!TheFunction)
        return nullptr;

    if (P.isBinaryOp())
        BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

    llvm::BasicBlock *BB =
            llvm::BasicBlock::Create(TheContext, "entry", TheFunction);
    Builder.SetInsertPoint(BB);

    NamedValues.clear();

    for (auto &Arg : TheFunction->args()) {
        llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());

        Builder.CreateStore(&Arg, Alloca);

        NamedValues[Arg.getName()] = Alloca;
    }

    if (llvm::Value *RetVal = Body->codegen()) {
        Builder.CreateRet(RetVal);

        llvm::verifyFunction(*TheFunction);

        // TheFPM->run(*TheFunction);

        return TheFunction;
    }

    TheFunction->eraseFromParent();
    return nullptr;
}

llvm::Value *IfExprAST::codegen() {
    llvm::Value *CondV = Cond->codegen();
    if (!CondV)
        return nullptr;

    CondV = Builder.CreateFCmpONE(
            CondV, llvm::ConstantFP::get(TheContext, llvm::APFloat(0.0)), "ifcond");

    llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();

    llvm::BasicBlock *ThenBB =
            llvm::BasicBlock::Create(TheContext, "then", TheFunction);
    llvm::BasicBlock *ElseBB = llvm::BasicBlock::Create(TheContext, "else");
    llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(TheContext, "ifcont");

    Builder.CreateCondBr(CondV, ThenBB, ElseBB);

    Builder.SetInsertPoint(ThenBB);

    llvm::Value *ThenV = Then->codegen();
    if (!ThenV)
        return nullptr;

    Builder.CreateBr(MergeBB);

    ThenBB = Builder.GetInsertBlock();

    TheFunction->getBasicBlockList().push_back(ElseBB);
    Builder.SetInsertPoint(ElseBB);

    llvm::Value *ElseV = Else->codegen();
    if (!ElseV)
        return nullptr;

    Builder.CreateBr(MergeBB);

    ElseBB = Builder.GetInsertBlock();

    TheFunction->getBasicBlockList().push_back(MergeBB);
    Builder.SetInsertPoint(MergeBB);

    llvm::PHINode *PN =
            Builder.CreatePHI(llvm::Type::getDoubleTy(TheContext), 2, "iftmp");

    PN->addIncoming(ThenV, ThenBB);
    PN->addIncoming(ElseV, ElseBB);

    return PN;
}

llvm::Value *ForExprAST::codegen() {
    llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();

    llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);

    llvm::Value *StartVal = Start->codegen();
    if (!StartVal)
        return nullptr;

    Builder.CreateStore(StartVal, Alloca);

    llvm::BasicBlock *LoopBB =
            llvm::BasicBlock::Create(TheContext, "loop", TheFunction);
    Builder.CreateBr(LoopBB);
    Builder.SetInsertPoint(LoopBB);

    llvm::AllocaInst *OldVal = NamedValues[VarName];
    NamedValues[VarName] = Alloca;

    if (!Body->codegen())
        return nullptr;

    llvm::Value *StepVal = nullptr;
    if (Step) {
        StepVal = Step->codegen();
        if (!StepVal)
            return nullptr;
    } else {
        StepVal = llvm::ConstantFP::get(TheContext, llvm::APFloat(1.0));
    }

    llvm::Value *EndCond = End->codegen();
    if (!EndCond)
        return nullptr;

    llvm::Value *CurVar = Builder.CreateLoad(Alloca, VarName.c_str());
    llvm::Value *NextVar = Builder.CreateFAdd(CurVar, StepVal, "nextvar");
    Builder.CreateStore(NextVar, Alloca);

    EndCond = Builder.CreateFCmpONE(
            EndCond, llvm::ConstantFP::get(TheContext, llvm::APFloat(0.0)),
            "loopcond");

    llvm::BasicBlock *AfterBB =
            llvm::BasicBlock::Create(TheContext, "afterloop", TheFunction);

    Builder.CreateCondBr(EndCond, LoopBB, AfterBB);

    Builder.SetInsertPoint(AfterBB);

    if (OldVal)
        NamedValues[VarName] = OldVal;
    else
        NamedValues.erase(VarName);

    return llvm::Constant::getNullValue(llvm::Type::getDoubleTy(TheContext));
}
