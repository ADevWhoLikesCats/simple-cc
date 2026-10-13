#pragma once
#include "ast.h"
#include "lexer.h"
#include <stdexcept>

namespace cfe {

struct ParseError : std::runtime_error {
    size_t line, col;
    ParseError(std::string msg, size_t l, size_t c)
        : std::runtime_error(std::move(msg)), line(l), col(c) {}
};

class Parser {
public:
    explicit Parser(Lexer &lex) : lex_(lex) {}

    Program parse_program();

private:
    Lexer &lex_;

    const Token &peek()             { return lex_.peek(); }
    Token        next()             { return lex_.next(); }
    bool check(TokenKind k)         { return peek().kind == k; }
    bool accept(TokenKind k)        { if (check(k)) { next(); return true; } return false; }

    Token expect(TokenKind k, const char *what);

    std::unique_ptr<FuncDecl> parse_func();
    std::string parse_type();

    StmtPtr parse_stmt();
    StmtPtr parse_block();
    StmtPtr parse_return();
    StmtPtr parse_if();
    StmtPtr parse_while();
    StmtPtr parse_var_decl_or_expr();

    ExprPtr parse_expr();
    ExprPtr parse_assignment();
    ExprPtr parse_binary_rest(ExprPtr lhs);
    ExprPtr parse_equality();
    ExprPtr parse_relational();
    ExprPtr parse_additive();
    ExprPtr parse_multiplicative();
    ExprPtr parse_unary();
    ExprPtr parse_primary();
};

} // namespace cfe
