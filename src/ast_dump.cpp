#include "ast.h"
#include <cstdio>

namespace cfe {

template <class... Ts> struct ov : Ts... { using Ts::operator()...; };
template <class... Ts> ov(Ts...) -> ov<Ts...>;

static void pad(int n) { for (int i = 0; i < n; i++) std::fputc(' ', stderr); }

static void dump_expr(const ExprPtr &e, int ind);
static void dump_stmt(const StmtPtr &s, int ind);

static void dump_expr(const ExprPtr &e, int ind) {
    std::visit(ov{
        [&](const IntLitExpr &n) {
            pad(ind); std::fprintf(stderr, "IntLit %lld\n", (long long)n.value);
        },
        [&](const VarExpr &n) {
            pad(ind); std::fprintf(stderr, "Var %s\n", n.name.c_str());
        },
        [&](const BinaryExpr &n) {
            pad(ind); std::fprintf(stderr, "Binary %s\n", token_kind_name(n.op));
            dump_expr(n.lhs, ind + 2);
            dump_expr(n.rhs, ind + 2);
        },
        [&](const AssignExpr &n) {
            pad(ind); std::fprintf(stderr, "Assign %s =\n", n.name.c_str());
            dump_expr(n.value, ind + 2);
        },
        [&](const CallExpr &n) {
            pad(ind); std::fprintf(stderr, "Call %s (%zu args)\n",
                                   n.callee.c_str(), n.args.size());
            for (auto &a : n.args) dump_expr(a, ind + 2);
        },
    }, e->node);
}

static void dump_stmt(const StmtPtr &s, int ind) {
    std::visit(ov{
        [&](const ReturnStmt &n) {
            pad(ind); std::fprintf(stderr, "Return\n");
            if (n.value) dump_expr(n.value, ind + 2);
        },
        [&](const ExprStmt &n) {
            pad(ind); std::fprintf(stderr, "ExprStmt\n");
            dump_expr(n.value, ind + 2);
        },
        [&](const VarDeclStmt &n) {
            pad(ind); std::fprintf(stderr, "VarDecl %s %s\n",
                                   n.type.c_str(), n.name.c_str());
            if (n.init) dump_expr(n.init, ind + 2);
        },
        [&](const BlockStmt &n) {
            pad(ind); std::fprintf(stderr, "Block\n");
            for (auto &st : n.stmts) dump_stmt(st, ind + 2);
        },
        [&](const IfStmt &n) {
            pad(ind); std::fprintf(stderr, "If\n");
            pad(ind + 2); std::fprintf(stderr, "cond:\n");
            dump_expr(n.cond, ind + 4);
            pad(ind + 2); std::fprintf(stderr, "then:\n");
            dump_stmt(n.then_, ind + 4);
            if (n.else_) {
                pad(ind + 2); std::fprintf(stderr, "else:\n");
                dump_stmt(n.else_, ind + 4);
            }
        },
        [&](const WhileStmt &n) {
            pad(ind); std::fprintf(stderr, "While\n");
            pad(ind + 2); std::fprintf(stderr, "cond:\n");
            dump_expr(n.cond, ind + 4);
            pad(ind + 2); std::fprintf(stderr, "body:\n");
            dump_stmt(n.body, ind + 4);
        },
    }, s->node);
}

void dump_ast(const Program &p) {
    for (auto &f : p.funcs) {
        std::fprintf(stderr, "func %s %s (%zu params)\n",
                     f->ret_type.c_str(), f->name.c_str(), f->params.size());
        for (auto &prm : f->params)
            std::fprintf(stderr, "  param %s %s\n",
                         prm.type.c_str(), prm.name.c_str());
        if (f->body) {
            std::fprintf(stderr, "  body:\n");
            for (auto &s : f->body->stmts) dump_stmt(s, 4);
        }
    }
}

} // namespace cfe
