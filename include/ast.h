#pragma once
#include "token.h"
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace cfe {

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// ---------- Expressions ----------

struct IntLitExpr {
    int64_t value;
    size_t line, col;
};

struct VarExpr {
    std::string name;
    size_t line, col;
};

struct BinaryExpr {
    TokenKind op;
    ExprPtr lhs, rhs;
    size_t line, col;
};

struct AssignExpr {
    std::string name;
    ExprPtr value;
    size_t line, col;
};

struct CallExpr {
    std::string callee;
    std::vector<ExprPtr> args;
    size_t line, col;
};

struct Expr {
    std::variant<IntLitExpr, VarExpr, BinaryExpr, AssignExpr, CallExpr> node;
};

// ---------- Statements ----------

struct ReturnStmt {
    ExprPtr value;   // may be null for "return;"
    size_t line, col;
};

struct ExprStmt {
    ExprPtr value;
    size_t line, col;
};

struct VarDeclStmt {
    std::string type;    // "int", "char", ... (only "int" for now)
    std::string name;
    ExprPtr init;        // may be null
    size_t line, col;
};

struct BlockStmt {
    std::vector<StmtPtr> stmts;
    size_t line, col;
};

struct IfStmt {
    ExprPtr cond;
    StmtPtr then_;
    StmtPtr else_;       // may be null
    size_t line, col;
};

struct WhileStmt {
    ExprPtr cond;
    StmtPtr body;
    size_t line, col;
};

struct Stmt {
    std::variant<ReturnStmt, ExprStmt, VarDeclStmt, BlockStmt, IfStmt, WhileStmt> node;
};

// ---------- Top level ----------

struct Param {
    std::string type;
    std::string name;
};

struct FuncDecl {
    std::string ret_type;   // "int" or "void"
    std::string name;
    std::vector<Param> params;
    std::unique_ptr<BlockStmt> body;
    size_t line, col;
};

struct Program {
    std::vector<std::unique_ptr<FuncDecl>> funcs;
};

} // namespace cfe
