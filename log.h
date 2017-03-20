#ifndef KALEIDOSCOPE_LOG_H
#define KALEIDOSCOPE_LOG_H

#include <memory>

#include "llvm/IR/Value.h"

// Forward decls
class ExprAST;
class PrototypeAST;

std::unique_ptr<ExprAST> LogError(const char *Str);
std::unique_ptr<PrototypeAST> LogErrorP(const char *Str);
llvm::Value *LogErrorV(const char *Str);

#endif // KALEIDOSCOPE_LOG_H
