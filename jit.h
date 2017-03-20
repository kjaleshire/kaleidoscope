#ifndef KALEIDOSCOPE_JIT_H
#define KALEIDOSCOPE_JIT_H

#include <map>
#include <memory>

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"

#include "llvm/examples/Kaleidoscope/include/KaleidoscopeJIT.h"

// Forward declarations
class PrototypeAST;

extern std::unique_ptr<llvm::Module> TheModule;
extern std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
extern std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;

void HandleDefinition();
void HandleExtern();
void HandleTopLevelExpression();
void InitializeModuleAndPassManager();

#endif // KALEIDOSCOPE_JIT_H
