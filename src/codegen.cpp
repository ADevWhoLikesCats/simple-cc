#include "codegen.h"
#include <sstream>
#include <algorithm>

namespace cfe {

// ---------- assignment collector (for loop-carried vars) ----------

static void collect_assigned_expr(const Expr &e, std::vector<std::string> &out) {
    std::visit([&](auto &&n) {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, AssignExpr>) {
            out.push_back(n.name);
            collect_assigned_expr(*n.value, out);
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            collect_assigned_expr(*n.lhs, out);
            collect_assigned_expr(*n.rhs, out);
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            for (auto &a : n.args) collect_assigned_expr(*a, out);
        }
    }, e.node);
}

static void collect_assigned_stmt(const Stmt &s, std::vector<std::string> &out) {
    std::visit([&](auto &&n) {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, ExprStmt>) {
            collect_assigned_expr(*n.value, out);
        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            if (n.value) collect_assigned_expr(*n.value, out);
        } else if constexpr (std::is_same_v<T, VarDeclStmt>) {
            if (n.init) collect_assigned_expr(*n.init, out);
        } else if constexpr (std::is_same_v<T, BlockStmt>) {
            for (auto &st : n.stmts) collect_assigned_stmt(*st, out);
        } else if constexpr (std::is_same_v<T, IfStmt>) {
            collect_assigned_expr(*n.cond, out);
            collect_assigned_stmt(*n.then_, out);
            if (n.else_) collect_assigned_stmt(*n.else_, out);
        } else if constexpr (std::is_same_v<T, WhileStmt>) {
            collect_assigned_expr(*n.cond, out);
            collect_assigned_stmt(*n.body, out);
        }
    }, s.node);
}

std::vector<std::string> CodeGen::collect_assigned(const StmtPtr &s) {
    std::vector<std::string> out;
    collect_assigned_stmt(*s, out);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}


CodeGen::CodeGen() {
    m_   = ir_module_new();
    b_   = ir_builder_new(m_);
    i32_ = ir_type_i32();
}

CodeGen::~CodeGen() {
    if (b_) ir_builder_free(b_);
    // m_ is owned by the caller? No — we own it. But ir_module_new
    // doesn't expose a free in the header. Leak it; it's per-compile.
}

// ---------- scopes ----------

void CodeGen::push_scope() { scopes_.emplace_back(); }
void CodeGen::pop_scope()  { scopes_.pop_back(); }

void CodeGen::bind(const std::string &name, IRValue *v) {
    if (scopes_.empty()) push_scope();
    scopes_.back()[name] = v;
}

IRValue *CodeGen::lookup(const std::string &name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return f->second;
    }
    throw CodeGenError("undefined variable '" + name + "'");
}

// ---------- blocks ----------

IRBlock *CodeGen::new_block(const std::string &prefix) {
    return ir_builder_block(b_, cur_func_, prefix.c_str());
}

void CodeGen::set_block(IRBlock *blk) {
    ir_set_insert(b_, blk);
    cur_block_  = blk;
    terminated_ = false;
}

void CodeGen::terminate(IRValue *v) {
    ir_ret(b_, v);
    terminated_ = true;
}

void CodeGen::terminate_br(IRBlock *dest) {
    ir_br(b_, dest);
    terminated_ = true;
}

// ---------- program / functions ----------

void CodeGen::emit_program(const Program &p) {
    for (auto &f : p.funcs) emit_function(*f);
    ir_builder_finalize(b_);
}

void CodeGen::emit_function(const FuncDecl &f) {
    IRType *ret = (f.ret_type == "void") ? ir_type_void() : i32_;

    cur_func_ = ir_builder_func(b_, f.name.c_str(), ret);

    // params (must be set before creating the entry block, matching bt_params.c)
    if (!f.params.empty()) {
        std::vector<IRType*> ptypes(f.params.size(), i32_);
        ir_builder_params(b_, cur_func_, ptypes.data(), (int)ptypes.size());
    }

    IRBlock *entry = new_block("entry");
    set_block(entry);

    scopes_.clear();
    branch_assigned_.clear();
    push_scope();

    for (size_t i = 0; i < f.params.size(); i++) {
        bind(f.params[i].name, ir_arg(b_, (int)i));
    }

    if (f.body) emit_block(*f.body);

    if (!terminated_) {
        if (ret == ir_type_void()) terminate(nullptr);
        else                        terminate(ir_const_i(i32_, 0));
    }

    pop_scope();
    cur_func_  = nullptr;
    cur_block_ = nullptr;
}

// ---------- statements ----------

void CodeGen::emit_stmt(const Stmt &s) {
    if (terminated_) return;   // skip unreachable code

    std::visit([&](auto &&n) {
        using T = std::decay_t<decltype(n)>;

        if constexpr (std::is_same_v<T, ReturnStmt>) {
            IRValue *v = n.value ? emit_expr(*n.value) : nullptr;
            terminate(v);
        }
        else if constexpr (std::is_same_v<T, ExprStmt>) {
            emit_expr(*n.value);
        }
        else if constexpr (std::is_same_v<T, VarDeclStmt>) {
            IRValue *v = n.init ? emit_expr(*n.init) : ir_const_i(i32_, 0);
            bind(n.name, v);
        }
        else if constexpr (std::is_same_v<T, BlockStmt>) {
            push_scope();
            emit_block(n);
            pop_scope();
        }
        else if constexpr (std::is_same_v<T, IfStmt>) {
            IRBlock *pre_if_blk = cur_block_;

            // Collect variables assigned in either branch AND declared
            // before the if. These need merge phis.
            std::vector<std::string> assigned;
            {
                std::vector<std::string> cand;
                for (auto &v : collect_assigned(n.then_)) cand.push_back(v);
                if (n.else_) for (auto &v : collect_assigned(n.else_)) cand.push_back(v);
                std::sort(cand.begin(), cand.end());
                cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
                for (auto &v : cand) {
                    bool exists = false;
                    for (auto &sc : scopes_) if (sc.count(v)) { exists = true; break; }
                    if (exists) assigned.push_back(v);
                }
            }

            // Snapshot pre-if values
            std::unordered_map<std::string, IRValue*> pre_if;
            for (auto &v : assigned) pre_if[v] = lookup(v);

            IRValue *cond = emit_expr(*n.cond);

            IRBlock *then_blk  = new_block("if.then");
            IRBlock *else_blk  = n.else_ ? new_block("if.else") : nullptr;
            IRBlock *merge_blk = new_block("if.end");

            if (else_blk) ir_cbr(b_, cond, then_blk, else_blk);
            else          ir_cbr(b_, cond, then_blk, merge_blk);
            terminated_ = true;

            // ---- then branch ----
            set_block(then_blk);
            emit_stmt(*n.then_);
            bool then_terminated = terminated_;
            std::unordered_map<std::string, IRValue*> post_then;
            for (auto &v : assigned) post_then[v] = lookup(v);
            if (!then_terminated) terminate_br(merge_blk);

            // ---- else branch ----
            std::unordered_map<std::string, IRValue*> post_else;
            bool else_terminated = false;
            if (else_blk) {
                set_block(else_blk);
                emit_stmt(*n.else_);
                else_terminated = terminated_;
                for (auto &v : assigned) post_else[v] = lookup(v);
                if (!else_terminated) terminate_br(merge_blk);
            }

            // ---- merge block with phis ----
            set_block(merge_blk);

            // If both branches terminated, merge is unreachable.
            if (then_terminated && (else_blk ? else_terminated : true) && !else_blk) {
                // then returns, no else: merge is reachable from pre_if only.
                // Fall through to the merge logic below (which handles it).
            }

            // Reject the mixed case for now: one branch returns, the other
            // doesn't reach merge.
            bool then_reaches  = !then_terminated;
            bool else_reaches  = else_blk ? !else_terminated : true;  // no else means pre_if reaches
            // (Actually for no-else case, "else reaches" = pre_if reaches)

            if (then_reaches && !else_reaches && else_blk) {
                throw CodeGenError("if: one branch returns, the other doesn't — merge phis not yet handled for this case");
            }

            // Build the phi for each assigned variable.
            for (auto &v : assigned) {
                IRValue *phi_v = nullptr;

                if (else_blk) {
                    // Two predecessors: then and else.
                    if (then_reaches && else_reaches) {
                        phi_v = ir_phi(b_, i32_, post_then[v], then_blk,
                                              post_else[v], else_blk);
                    } else if (then_reaches) {
                        // else returns; only then reaches.
                        phi_v = post_then[v];   // no phi needed
                    } else if (else_reaches) {
                        phi_v = post_else[v];
                    } else {
                        // Both return. Merge unreachable; value doesn't matter.
                        phi_v = pre_if[v];
                    }
                } else {
                    // No else: predecessors are then and pre_if.
                    if (then_reaches) {
                        phi_v = ir_phi(b_, i32_, post_then[v], then_blk,
                                              pre_if[v],    pre_if_blk);
                    } else {
                        // then returns; only pre_if reaches.
                        phi_v = pre_if[v];
                    }
                }

                bind(v, phi_v);
            }
        }
        else if constexpr (std::is_same_v<T, WhileStmt>) {
            IRBlock *pre_loop_blk = cur_block_;
            IRBlock *head = new_block("while.head");
            IRBlock *body = new_block("while.body");
            IRBlock *exit = new_block("while.exit");

            // Loop-carried variables: assigned inside the body AND declared
            // before the loop (otherwise they don't escape).
            std::vector<std::string> assigned;
            for (auto &v : collect_assigned(n.body)) {
                bool exists = false;
                for (auto &sc : scopes_) if (sc.count(v)) { exists = true; break; }
                if (exists) assigned.push_back(v);
            }

            // Snapshot pre-loop values
            std::unordered_map<std::string, IRValue*> pre_loop;
            for (auto &v : assigned) pre_loop[v] = lookup(v);

            terminate_br(head);

            // ---- head block: bind to placeholders, emit phis, then condition ----
            set_block(head);

            std::unordered_map<std::string, IRValue*> head_use;
            std::unordered_map<std::string, IRValue*> body_use;
            for (auto &v : assigned) {
                head_use[v] = ir_value_placeholder(b_, i32_);
                body_use[v] = ir_value_placeholder(b_, i32_);
                bind(v, head_use[v]);
            }

            std::unordered_map<std::string, IRValue*> phi_val;
            for (auto &v : assigned) {
                phi_val[v] = ir_phi(b_, i32_, pre_loop[v], pre_loop_blk,
                                    body_use[v], body);
            }

            IRValue *cond = emit_expr(*n.cond);
            ir_cbr(b_, cond, body, exit);
            terminated_ = true;

            // ---- body block ----
            set_block(body);
            emit_stmt(*n.body);

            // Capture post-body values (the current SSA value of each var)
            std::unordered_map<std::string, IRValue*> post_body;
            for (auto &v : assigned) post_body[v] = lookup(v);

            if (!terminated_) terminate_br(head);

            // Backfill: body->head edge, and the placeholder used by the phi
            for (auto &v : assigned) {
                ir_value_define(b_, body_use[v], post_body[v]);
                ir_value_define(b_, head_use[v], phi_val[v]);
            }

            // After the loop, each loop-carried var takes the phi value
            set_block(exit);
            for (auto &v : assigned) bind(v, phi_val[v]);
        }
        else {
            // shouldn't happen — all cases covered above
        }
    }, s.node);
}

void CodeGen::emit_block(const BlockStmt &blk) {
    for (auto &s : blk.stmts) {
        if (terminated_) break;
        emit_stmt(*s);
    }
}

// ---------- expressions ----------

IRValue *CodeGen::emit_expr(const Expr &e) {
    return std::visit([&](auto &&n) -> IRValue* {
        using T = std::decay_t<decltype(n)>;

        if constexpr (std::is_same_v<T, IntLitExpr>) {
            return ir_const_i(i32_, n.value);
        }
        else if constexpr (std::is_same_v<T, VarExpr>) {
            return lookup(n.name);
        }
        else if constexpr (std::is_same_v<T, BinaryExpr>) {
            return emit_binary(n);
        }
        else if constexpr (std::is_same_v<T, AssignExpr>) {
            return emit_assign(n);
        }
        else if constexpr (std::is_same_v<T, CallExpr>) {
            return emit_call(n);
        }
        else {
            throw CodeGenError("unhandled expression node");
        }
    }, e.node);
}

IRValue *CodeGen::emit_binary(const BinaryExpr &bin) {
    IRValue *lhs = emit_expr(*bin.lhs);
    IRValue *rhs = emit_expr(*bin.rhs);

    switch (bin.op) {
        case TokenKind::Plus:    return ir_add (b_, i32_, lhs, rhs);
        case TokenKind::Minus:   return ir_sub (b_, i32_, lhs, rhs);
        case TokenKind::Star:    return ir_mul (b_, i32_, lhs, rhs);
        case TokenKind::Slash:   return ir_sdiv(b_, i32_, lhs, rhs);
        case TokenKind::Percent: return ir_srem(b_, i32_, lhs, rhs);

        case TokenKind::Eq:  return ir_icmp(b_, ICMP_EQ,  lhs, rhs);
        case TokenKind::Ne:  return ir_icmp(b_, ICMP_NE,  lhs, rhs);
        case TokenKind::Lt:  return ir_icmp(b_, ICMP_SLT, lhs, rhs);
        case TokenKind::Le:  return ir_icmp(b_, ICMP_SLE, lhs, rhs);
        case TokenKind::Gt:  return ir_icmp(b_, ICMP_SGT, lhs, rhs);
        case TokenKind::Ge:  return ir_icmp(b_, ICMP_SGE, lhs, rhs);

        default: {
            std::ostringstream os;
            os << "unsupported binary operator '"
               << token_kind_name(bin.op) << "'";
            throw CodeGenError(os.str());
        }
    }
}

IRValue *CodeGen::emit_assign(const AssignExpr &a) {
    IRValue *v = emit_expr(*a.value);
    // rebind in the innermost scope where the variable exists
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->find(a.name);
        if (f != it->end()) { f->second = v; return v; }
    }
    throw CodeGenError("assignment to undefined variable '" + a.name + "'");
}

IRValue *CodeGen::emit_call(const CallExpr &c) {
    std::vector<IRValue*> argv;
    argv.reserve(c.args.size());
    for (auto &a : c.args) argv.push_back(emit_expr(*a));

    return ir_call(b_, i32_, c.callee.c_str(),
                   argv.empty() ? nullptr : argv.data(),
                   (int)argv.size());
}

} // namespace cfe
