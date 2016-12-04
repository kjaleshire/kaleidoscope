#include <cctype>
#include <map>
#include <string>
#include <vector>

#include "llvm/ADT/STLExtras.h"

#include "ast.h"
#include "log.h"
#include "parser.h"

// Forward declarations
class BinaryExprAST;
class CallExprAST;
class IfExprAST;
class NumberExprAST;
class VariableExprAST;

// ========================================================================
// Token generation
// ========================================================================

std::map<char, int> BinopPrecedence;
int CurTok;

static std::string IdentifierStr;
static double NumVal;

static std::unique_ptr<ExprAST> ParseExpression();

static int gettok() {
    static int LastChar = ' ';

    while (std::isspace(LastChar)) {
        LastChar = getchar();
    }

    if (std::isalpha(LastChar)) {
        IdentifierStr = LastChar;
        while (std::isalnum(LastChar = getchar())) {
            IdentifierStr += LastChar;
        }

        if (IdentifierStr == "def") {
            return tok_def;
        } else if (IdentifierStr == "extern") {
            return tok_extern;
        } else if (IdentifierStr == "if") {
            return tok_if;
        } else if (IdentifierStr == "then") {
            return tok_then;
        } else if (IdentifierStr == "else") {
            return tok_else;
        } else {
            return tok_identifier;
        }
    }

    if (std::isdigit(LastChar) || LastChar == '.') {
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = getchar();
        } while(std::isdigit(LastChar) || LastChar == '.');

        NumVal = std::strtod(NumStr.c_str(), nullptr);
        return tok_number;
    }

    if (LastChar == '#') {
        do {
            LastChar = getchar();
        } while(LastChar != EOF && LastChar != '\n' && LastChar != '\r');
        if (LastChar != EOF) {
            return gettok();
            }
    }

    if (LastChar == EOF) {
        return tok_eof;
    }
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}

// ========================================================================
// Parser
// ========================================================================

int getNextToken() {
    return CurTok = gettok();
}

static int GetTokPrecedence() {
    if (!isascii(CurTok)) {
        return -1;
    }

    int TokPrec = BinopPrecedence[CurTok];
    if (TokPrec <= 0) {
        return -1;
    }

    return TokPrec;
}

static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = llvm::make_unique<NumberExprAST>(NumVal);
    getNextToken();
    return std::move(Result);
}

static std::unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken();
    auto V = ParseExpression();
    if (!V) {
        return nullptr;
    }

    if (CurTok != ')') {
        return LogError("Expected ')'");
    }
    getNextToken();
    return V;
}

static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
    std::string IdName = IdentifierStr;

    getNextToken();

    if (CurTok != '(') {
        return llvm::make_unique<VariableExprAST>(IdName);
    }

    getNextToken();

    std::vector<std::unique_ptr<ExprAST>> Args;

    if (CurTok != ')') {
        while(true) {
            if (auto Arg = ParseExpression()) {
                Args.push_back(std::move(Arg));
            } else {
                return nullptr;
            }

            if (CurTok == ')') {
                break;
            }

            if (CurTok != ',') {
                return LogError("Expected ')' or ',' in argument list");
            }

            getNextToken();
        }
    }

    getNextToken();

    return llvm::make_unique<CallExprAST>(IdName, std::move(Args));
}

static std::unique_ptr<ExprAST> ParseIfExpr() {
    getNextToken();

    auto Cond = ParseExpression();
    if (!Cond) {
        return nullptr;
    }

    if (CurTok != tok_then) {
        return LogError("exptected then");
    }
    getNextToken();

    auto Then = ParseExpression();
    if (!Then) {
        return nullptr;
    }

    if (CurTok != tok_else) {
        return LogError("expected else");
    }

    getNextToken();

    auto Else = ParseExpression();
    if (!Else) {
        return nullptr;
    }

    return llvm::make_unique<IfExprAST>(std::move(Cond), std::move(Then), std::move(Else));
}

static std::unique_ptr<ExprAST> ParsePrimary() {
    std::string error("unknown token '" + std::to_string(CurTok) + "' when expecting an expression");
    switch (CurTok) {
        default:
            return LogError(error.c_str());
        case tok_identifier:
            return ParseIdentifierExpr();
        case tok_number:
            return ParseNumberExpr();
        case '(':
            return ParseParenExpr();
        case tok_if:
            return ParseIfExpr();
    }
}

static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {
    while (true) {
        int TokPrec = GetTokPrecedence();

        if (TokPrec < ExprPrec) {
            return LHS;
        }

        int BinOp = CurTok;

        getNextToken();

        auto RHS = ParsePrimary();

        if (!RHS) {
            return nullptr;
        }

        int NextPrec = GetTokPrecedence();

        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
            if (!RHS) {
                return nullptr;
            }
        }

        LHS = llvm::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
    }
}

static std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParsePrimary();
    if (!LHS) {
        return nullptr;
    }

    return ParseBinOpRHS(0, std::move(LHS));
}

static std::unique_ptr<PrototypeAST> ParsePrototype() {
    if (CurTok != tok_identifier) {
        return LogErrorP("Expected function name in prototype");
    }

    std::string FnName = IdentifierStr;
    getNextToken();

    if (CurTok != '(') {
        return LogErrorP("Expected '(' in prototype");
    }

    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier) {
        ArgNames.push_back(IdentifierStr);
    }
    if (CurTok != ')') {
        return LogErrorP("Exptected ')' in prototype");
    }

    getNextToken();

    return llvm::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

std::unique_ptr<FunctionAST> ParseDefinition() {
    getNextToken();
    auto Proto = ParsePrototype();
    if (!Proto) {
        return nullptr;
    }

    if (auto E = ParseExpression()) {
        return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }

    return nullptr;
}

std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        auto Proto = llvm::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
        return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

std::unique_ptr<PrototypeAST> ParseExtern() {
    getNextToken();
    return ParsePrototype();
}
