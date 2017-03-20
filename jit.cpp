#include <iostream>

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

#include "ast.h"
#include "jit.h"
#include "parser.h"

// ========================================================================
// Top-level parsing and JIT generator
// ========================================================================

std::unique_ptr<llvm::Module> TheModule;
std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;

void HandleDefinition() {
  if (auto FnAST = ParseDefinition()) {
    if (auto *FnIR = FnAST->codegen()) {
      std::cerr << "Read function definition: ";
      FnIR->print(llvm::errs());
      std::cerr << "\n";
      TheJIT->addModule(std::move(TheModule));
      InitializeModuleAndPassManager();
    }
  } else {
    getNextToken();
  }
}

void HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    if (auto *FnIR = ProtoAST->codegen()) {
      std::cerr << "Read extern: ";
      FnIR->print(llvm::errs());
      std::cerr << "\n";
      FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    }
  } else {
    getNextToken();
  }
}

void HandleTopLevelExpression() {
  if (auto FnAST = ParseTopLevelExpr()) {
    if (auto *FnIR = FnAST->codegen()) {
      std::cerr << "Read top-level expression: ";
      FnIR->print(llvm::errs());
      std::cerr << "\n";

      auto H = TheJIT->addModule(std::move(TheModule));
      InitializeModuleAndPassManager();

      auto ExprSymbol = TheJIT->findSymbol("__anon_expr");
      assert(ExprSymbol && "Function not found");

      double (*FP)() = (double (*)())(intptr_t)ExprSymbol.getAddress();
      double output = FP();
      std::cerr << "Evaluated to " << output << std::endl;

      TheJIT->removeModule(H);
    }
  } else {
    getNextToken();
  }
}

void InitializeModuleAndPassManager() {
  TheModule = llvm::make_unique<llvm::Module>("my cool jit", TheContext);
  TheModule->setDataLayout(TheJIT->getTargetMachine().createDataLayout());

  TheFPM =
      llvm::make_unique<llvm::legacy::FunctionPassManager>(TheModule.get());

  TheFPM->add(llvm::createInstructionCombiningPass());
  TheFPM->add(llvm::createReassociatePass());
  TheFPM->add(llvm::createGVNPass());
  TheFPM->add(llvm::createCFGSimplificationPass());
  TheFPM->doInitialization();
}
