#include <iostream>

#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"

#include "ast.h"
#include "jit.h"
#include "parser.h"

// ========================================================================
// Driver
// ========================================================================

static void MainLoop() {
    while (true) {
        std::cout << "ready> " << std::flush;
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
    }
}

int main() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;

    std::cout << "ready> " << std::flush;
    getNextToken();

    TheJIT = llvm::make_unique<llvm::orc::KaleidoscopeJIT>();

    InitializeModuleAndPassManager();

    MainLoop();

    return 0;
}
