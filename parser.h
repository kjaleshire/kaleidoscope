#ifndef KALEIDOSCOPE_PARSER_H
#define KALEIDOSCOPE_PARSER_H

#include <map>
#include <memory>
#include <string>

// Forward declarations
class ExprAST;
class FunctionAST;
class PrototypeAST;

enum Token {
    tok_eof = -1,

    tok_def = -2,
    tok_extern = -3,

    tok_identifier = -4,
    tok_number = -5,

    tok_if = -6,
    tok_then = -7,
    tok_else = -8
};

extern std::map<char, int> BinopPrecedence;
extern int CurTok;

int getNextToken();
std::unique_ptr<FunctionAST> ParseDefinition();
std::unique_ptr<PrototypeAST> ParseExtern();
std::unique_ptr<FunctionAST> ParseTopLevelExpr();

#endif // KALEIDOSCOPE_PARSER_H
