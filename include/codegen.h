#pragma once
#include "ast.h"
#include "ir.h"
#include "ir_builder.h"
#include "backend.h"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace cfe {

struct CodeGenError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class CodeGen {
public:
    CodeGen();
    ~CodeGen();

    // Build IR for the whole program into module m.
    void emit_program(const Program &p);

    IRModule *module() { return m_; }

private:
    IRModule  *m_ = nullptr;
    IRBuilder *b_ = nullptr;
    IRType    *i32_ = nullptr;

    // current function
    IRFunc  *cur_func_  = nullptr;
    IRBlock *cur_block_ = nullptr;
    uint32_t block_counter_ = 0;
    bool     terminated_ = false;

    // scopes: name -> current SSA value
    std::vector<std::unordered_map<std::string, IRValue*>> scopes_;
    // names that were assigned inside the current if/while body (v1: error if read after)
    std::vector<std::string> branch_assigned_;

    void push_scope();
    void pop_scope();
    void bind(const std::string &name, IRValue *v);
    void rebind(const std::string &name, IRValue *v);
    IRValue *lookup(const std::string &name);

    void emit_function(const FuncDecl &f);
    void emit_stmt(const Stmt &s);
    void emit_block(const BlockStmt &blk);

    IRValue *emit_expr(const Expr &e);
    IRValue *emit_binary(const BinaryExpr &bin);
    IRValue *emit_assign(const AssignExpr &a);
    IRValue *emit_call(const CallExpr &c);

    IRBlock *new_block(const std::string &prefix);
    void     set_block(IRBlock *blk);
    void     terminate(IRValue *v);   // v may be null for void
    void     terminate_br(IRBlock *dest);

    // helpers
    bool stmt_terminates(const Stmt &s);
    bool block_terminates(const BlockStmt &blk);
    std::vector<std::string> collect_assigned(const StmtPtr &s);
};

} // namespace cfe
