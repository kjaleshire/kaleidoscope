#include <iostream>

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
// #include "llvm/IR/PassManager.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Scalar.h"

#include "ast.h"
#include "jit.h"
#include "parser.h"

// ========================================================================
// Top-level parsing and JIT generator
// ========================================================================

std::map<std::string, llvm::Value*> NamedValues;
std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
std::unique_ptr<llvm::Module> TheModule;

std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;

void InitializeModuleAndPassManager() {
    TheModule = llvm::make_unique<llvm::Module>("my cool jit", llvm::getGlobalContext());

    TheModule->setDataLayout(TheJIT->getTargetMachine().createDataLayout());
    TheFPM = llvm::make_unique<llvm::legacy::FunctionPassManager>(TheModule.get());

    TheFPM->add(llvm::createInstructionCombiningPass());
    TheFPM->add(llvm::createReassociatePass());
    TheFPM->add(llvm::createGVNPass());
    TheFPM->add(llvm::createCFGSimplificationPass());

    TheFPM->doInitialization();
}

void HandleDefinition() {
    if (auto FnAST = ParseDefinition()) {
        if (auto* FnIR = FnAST->codegen()) {
            fprintf(stderr, "Read function definition:");
            FnIR->dump();
            TheJIT->addModule(std::move(TheModule));
            InitializeModuleAndPassManager();
        }
    } else {
        getNextToken();
    }
}

void HandleExtern() {
    if (auto ProtoAST = ParseExtern()) {
        if (auto* FnIR = ProtoAST->codegen()) {
            fprintf(stderr, "Read extern:");
            FnIR->dump();
            FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
        }
    } else {
        getNextToken();
    }
}

void HandleTopLevelExpression() {
    if (auto FnAST = ParseTopLevelExpr()) {
        if (FnAST->codegen()) {
            auto H = TheJIT->addModule(std::move(TheModule));
            InitializeModuleAndPassManager();

            auto ExprSymbol = TheJIT->findSymbol("__anon_expr");
            assert(ExprSymbol && "Function not found");

            double (*FP)() = (double (*)())(intptr_t)ExprSymbol.getAddress();

            fprintf(stderr, "Evaluated to %f\n", FP());
            TheJIT->removeModule(H);
        }
    } else {
        getNextToken();
    }
}
