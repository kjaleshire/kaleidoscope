#include <iostream>

#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/TargetSelect.h"

#include "ast.h"
#include "jit.h"
#include "parser.h"

// ========================================================================
// Driver
// ========================================================================

static void MainLoop() {
  while (true) {
    switch (CurTok) {
    case tok_eof:
      return;
    case ';':
      getNextToken();
      break;
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
    std::cerr << "ready> " << std::flush;
  }
}

#ifdef LLVM_ON_WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/// putchard - putchar that takes a double and returns 0.
extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

/// printd - printf that takes a double prints it as "%f\n", returning 0.
extern "C" DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f\n", X);
  return 0;
}

int main() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;

  std::cerr << "ready> " << std::flush;
  getNextToken();

  TheJIT = llvm::make_unique<llvm::orc::KaleidoscopeJIT>();

  InitializeModuleAndPassManager();

  MainLoop();

  return 0;
}
