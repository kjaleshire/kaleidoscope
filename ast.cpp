#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

#include "ast.h"
#include "jit.h"
#include "log.h"

// ========================================================================
// Code generation
// ========================================================================

extern std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
extern std::map<std::string, llvm::Value*> NamedValues;
extern std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
extern std::unique_ptr<llvm::Module> TheModule;

llvm::IRBuilder<> Builder(llvm::getGlobalContext());

llvm::Function* getFunction(std::string Name) {
    if (auto* F = TheModule->getFunction(Name)) {
        return F;
    }

    auto FI = FunctionProtos.find(Name);
    if (FI != FunctionProtos.end()) {
        return FI->second->codegen();
    }

    return nullptr;
}

llvm::Value *NumberExprAST::codegen() {
    return llvm::ConstantFP::get(llvm::getGlobalContext(), llvm::APFloat(Val));
}

llvm::Value* VariableExprAST::codegen() {
    llvm::Value *V = NamedValues[Name];
    if (!V) {
        LogErrorV("unknown variable name");
    }
    return V;
}

llvm::Value *BinaryExprAST::codegen() {
    llvm::Value *L = LHS->codegen();
    llvm::Value *R = RHS->codegen();

    if (!R || !L) {
        return nullptr;
    }

    switch (Op) {
        case '+':
            return Builder.CreateFAdd(L, R, "addtmp");
        case '-':
            return Builder.CreateFSub(L, R, "subtmp");
        case '*':
            return Builder.CreateFMul(L, R, "multmp");
        case '<':
            L = Builder.CreateFCmpULT(L, R, "cmptmp");
            return Builder.CreateUIToFP(L, llvm::Type::getDoubleTy(llvm::getGlobalContext()), "booltmp");
        default:
            return LogErrorV("invalid binary operator");
    }
}

llvm::Value* CallExprAST::codegen() {
    llvm::Function *CalleeF = getFunction(Callee);
    if (!CalleeF) {
        return LogErrorV("unknown function referenced");
    }

    if (CalleeF->arg_size() != Args.size()) {
        return LogErrorV("incorrect number of arguments passed");
    }

    std::vector<llvm::Value *> ArgsV;
    for (unsigned i = 0, e = Args.size(); i != e; i++) {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back()) {
            return nullptr;
        }
    }

    return Builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

llvm::Value* IfExprAST::codegen() {
    llvm::Value *CondV = Cond->codegen();
    if (!CondV) {
        return nullptr;
    }

    CondV = Builder.CreateFCmpONE(CondV, llvm::ConstantFP::get(llvm::getGlobalContext(), llvm::APFloat(0.0)), "ifcond");

    llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();

    llvm::BasicBlock *ThenBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "then", TheFunction);
    llvm::BasicBlock *ElseBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "else");
    llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "ifcont");

    Builder.CreateCondBr(CondV, ThenBB, MergeBB);

    Builder.SetInsertPoint(ThenBB);
    llvm::Value* ThenV = Then->codegen();
    if (!ThenV) {
        return nullptr;
    }

    Builder.CreateBr(MergeBB);
    ThenBB = Builder.GetInsertBlock();

    TheFunction->getBasicBlockList().push_back(ElseBB);
    Builder.SetInsertPoint(ElseBB);

    llvm::Value* ElseV = Else->codegen();
    if (!ElseV) {
        return nullptr;
    }

    Builder.CreateBr(MergeBB);
    ElseBB = Builder.GetInsertBlock();

    TheFunction->getBasicBlockList().push_back(MergeBB);
    Builder.SetInsertPoint(MergeBB);
    llvm::PHINode* PN = Builder.CreatePHI(llvm::Type::getDoubleTy(llvm::getGlobalContext()), 2, "iftmp");

    PN->addIncoming(ThenV, ThenBB);
    PN->addIncoming(ElseV, ElseBB);
    return PN;
}

llvm::Function *PrototypeAST::codegen() {
    std::vector<llvm::Type*> Doubles(Args.size(), llvm::Type::getDoubleTy(llvm::getGlobalContext()));

    llvm::FunctionType *FT = llvm::FunctionType::get(llvm::Type::getDoubleTy(llvm::getGlobalContext()), Doubles, false);

    llvm::Function *F = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, Name, TheModule.get());

    unsigned Idx = 0;
    for (auto& Arg : F->args()) {
        Arg.setName(Args[Idx++]);
    }

    return F;
}

llvm::Function *FunctionAST::codegen() {
    auto& P = *Proto;
    FunctionProtos[Proto->getName()] = std::move(Proto);
    llvm::Function *TheFunction = getFunction(P.getName());

    if (!TheFunction) {
        TheFunction = Proto->codegen();
    }

    if (!TheFunction) {
        return nullptr;
    }

    if (!TheFunction->empty()) {
        return (llvm::Function*) LogErrorV("Function cannot be redefined");
    }

    llvm::BasicBlock *BB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "entry", TheFunction);
    Builder.SetInsertPoint(BB);

    NamedValues.clear();

    for (auto& Arg : TheFunction->args()) {
        NamedValues[Arg.getName()] = &Arg;
    }

    if (llvm::Value *RetVal = Body->codegen()) {
        Builder.CreateRet(RetVal);
        verifyFunction(*TheFunction);

        TheFPM->run(*TheFunction);

        return TheFunction;
    }

    TheFunction->eraseFromParent();
    return nullptr;
}
