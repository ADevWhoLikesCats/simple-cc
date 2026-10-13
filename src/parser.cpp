#include "parser.h"
#include <sstream>

namespace cfe {

Token Parser::expect(TokenKind k, const char *what) {
    if (!check(k)) {
        std::ostringstream os;
        os << "expected " << what << ", got '" << token_kind_name(peek().kind) << "'";
        throw ParseError(os.str(), peek().line, peek().col);
    }
    return next();
}

static size_t expr_line(const ExprPtr &e) {
    return std::visit([](auto &n){ return n.line; }, e->node);
}
static size_t expr_col(const ExprPtr &e) {
    return std::visit([](auto &n){ return n.col; }, e->node);
}

static ExprPtr make_binary(TokenKind op, ExprPtr lhs, ExprPtr rhs, size_t line, size_t col) {
    auto e = std::make_unique<Expr>();
    BinaryExpr b;
    b.op = op;
    b.lhs = std::move(lhs);
    b.rhs = std::move(rhs);
    b.line = line;
    b.col  = col;
    e->node = std::move(b);
    return e;
}

// ---------- top-level ----------

Program Parser::parse_program() {
    Program prog;
    while (!check(TokenKind::Eof)) {
        prog.funcs.push_back(parse_func());
    }
    return prog;
}

std::unique_ptr<FuncDecl> Parser::parse_func() {
    auto f = std::make_unique<FuncDecl>();
    f->line = peek().line;
    f->col  = peek().col;

    f->ret_type = parse_type();
    Token name  = expect(TokenKind::Ident, "function name");
    f->name     = std::string(name.text);

    expect(TokenKind::LParen, "'('");
    if (!check(TokenKind::RParen)) {
        for (;;) {
            Param p;
            p.type = parse_type();
            Token pn = expect(TokenKind::Ident, "parameter name");
            p.name = std::string(pn.text);
            f->params.push_back(std::move(p));
            if (!accept(TokenKind::Comma)) break;
        }
    }
    expect(TokenKind::RParen, "')'");

    auto body_stmt = parse_block();
    auto *block = std::get_if<BlockStmt>(&body_stmt->node);
    if (!block) throw ParseError("expected function body block", f->line, f->col);

    f->body = std::make_unique<BlockStmt>(std::move(*block));
    return f;
}

std::string Parser::parse_type() {
    if (accept(TokenKind::KwInt))   return "int";
    if (accept(TokenKind::KwVoid))  return "void";
    if (accept(TokenKind::KwChar))  return "char";
    if (accept(TokenKind::KwLong))  return "long";
    if (accept(TokenKind::KwShort)) return "short";
    throw ParseError("expected a type", peek().line, peek().col);
}

// ---------- statements ----------

StmtPtr Parser::parse_stmt() {
    if (check(TokenKind::LBrace))    return parse_block();
    if (check(TokenKind::KwReturn))  return parse_return();
    if (check(TokenKind::KwIf))      return parse_if();
    if (check(TokenKind::KwWhile))   return parse_while();
    return parse_var_decl_or_expr();
}

StmtPtr Parser::parse_block() {
    auto s = std::make_unique<Stmt>();
    BlockStmt blk;
    blk.line = peek().line;
    blk.col  = peek().col;

    expect(TokenKind::LBrace, "'{'");
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        blk.stmts.push_back(parse_stmt());
    }
    expect(TokenKind::RBrace, "'}'");

    s->node = std::move(blk);
    return s;
}

StmtPtr Parser::parse_return() {
    auto s = std::make_unique<Stmt>();
    ReturnStmt r;
    r.line = peek().line;
    r.col  = peek().col;

    expect(TokenKind::KwReturn, "'return'");
    if (!check(TokenKind::Semi)) {
        r.value = parse_expr();
    }
    expect(TokenKind::Semi, "';'");

    s->node = std::move(r);
    return s;
}

StmtPtr Parser::parse_if() {
    auto s = std::make_unique<Stmt>();
    IfStmt n;
    n.line = peek().line;
    n.col  = peek().col;

    expect(TokenKind::KwIf, "'if'");
    expect(TokenKind::LParen, "'('");
    n.cond = parse_expr();
    expect(TokenKind::RParen, "')'");
    n.then_ = parse_stmt();

    if (accept(TokenKind::KwElse)) {
        n.else_ = parse_stmt();
    }

    s->node = std::move(n);
    return s;
}

StmtPtr Parser::parse_while() {
    auto s = std::make_unique<Stmt>();
    WhileStmt n;
    n.line = peek().line;
    n.col  = peek().col;

    expect(TokenKind::KwWhile, "'while'");
    expect(TokenKind::LParen, "'('");
    n.cond = parse_expr();
    expect(TokenKind::RParen, "')'");
    n.body = parse_stmt();

    s->node = std::move(n);
    return s;
}

StmtPtr Parser::parse_var_decl_or_expr() {
    bool is_decl =
        check(TokenKind::KwInt)  || check(TokenKind::KwChar) ||
        check(TokenKind::KwLong) || check(TokenKind::KwShort);

    if (is_decl) {
        auto s = std::make_unique<Stmt>();
        VarDeclStmt d;
        d.line = peek().line;
        d.col  = peek().col;

        d.type = parse_type();
        Token name = expect(TokenKind::Ident, "variable name");
        d.name = std::string(name.text);

        if (accept(TokenKind::Assign)) {
            d.init = parse_expr();
        }
        expect(TokenKind::Semi, "';'");

        s->node = std::move(d);
        return s;
    }

    auto s = std::make_unique<Stmt>();
    ExprStmt e;
    e.line = peek().line;
    e.col  = peek().col;
    e.value = parse_expr();
    expect(TokenKind::Semi, "';'");
    s->node = std::move(e);
    return s;
}

// ---------- expressions ----------

ExprPtr Parser::parse_expr() {
    return parse_assignment();
}

ExprPtr Parser::parse_assignment() {
    auto lhs = parse_equality();

    if (check(TokenKind::Assign)) {
        Token eq = next();
        auto *v = std::get_if<VarExpr>(&lhs->node);
        if (!v) {
            std::ostringstream os;
            os << "left side of '=' must be a variable";
            throw ParseError(os.str(), eq.line, eq.col);
        }
        auto rhs = parse_assignment();   // right-associative
        auto e = std::make_unique<Expr>();
        AssignExpr a;
        a.name  = v->name;
        a.line  = expr_line(lhs);
        a.col   = expr_col(lhs);
        a.value = std::move(rhs);
        e->node = std::move(a);
        return e;
    }

    return lhs;
}

ExprPtr Parser::parse_equality() {
    auto lhs = parse_relational();
    while (check(TokenKind::Eq) || check(TokenKind::Ne)) {
        Token op = next();
        auto rhs = parse_relational();
        lhs = make_binary(op.kind, std::move(lhs), std::move(rhs),
                          op.line, op.col);
    }
    return lhs;
}

ExprPtr Parser::parse_relational() {
    auto lhs = parse_additive();
    while (check(TokenKind::Lt) || check(TokenKind::Le) ||
           check(TokenKind::Gt) || check(TokenKind::Ge)) {
        Token op = next();
        auto rhs = parse_additive();
        lhs = make_binary(op.kind, std::move(lhs), std::move(rhs),
                          op.line, op.col);
    }
    return lhs;
}

ExprPtr Parser::parse_additive() {
    auto lhs = parse_multiplicative();
    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        Token op = next();
        auto rhs = parse_multiplicative();
        lhs = make_binary(op.kind, std::move(lhs), std::move(rhs),
                          op.line, op.col);
    }
    return lhs;
}

ExprPtr Parser::parse_multiplicative() {
    auto lhs = parse_unary();
    while (check(TokenKind::Star) || check(TokenKind::Slash) ||
           check(TokenKind::Percent)) {
        Token op = next();
        auto rhs = parse_unary();
        lhs = make_binary(op.kind, std::move(lhs), std::move(rhs),
                          op.line, op.col);
    }
    return lhs;
}

ExprPtr Parser::parse_unary() {
    if (check(TokenKind::Minus)) {
        Token m = next();
        auto operand = parse_unary();
        auto e = std::make_unique<Expr>();
        IntLitExpr z;
        z.value = 0; z.line = m.line; z.col = m.col;
        auto zero = std::make_unique<Expr>();
        zero->node = z;
        e->node = BinaryExpr{ TokenKind::Minus, std::move(zero), std::move(operand),
                              m.line, m.col };
        return e;
    }
    if (check(TokenKind::Bang)) {
        Token m = next();
        auto operand = parse_unary();
        auto e = std::make_unique<Expr>();
        IntLitExpr z;
        z.value = 0; z.line = m.line; z.col = m.col;
        auto zero = std::make_unique<Expr>();
        zero->node = z;
        e->node = BinaryExpr{ TokenKind::Eq, std::move(operand), std::move(zero),
                              m.line, m.col };
        return e;
    }
    return parse_primary();
}

ExprPtr Parser::parse_primary() {
    Token t = peek();

    if (t.kind == TokenKind::IntLit) {
        next();
        auto e = std::make_unique<Expr>();
        IntLitExpr n;
        n.value = t.int_value;
        n.line  = t.line;
        n.col   = t.col;
        e->node = std::move(n);
        return e;
    }

    if (t.kind == TokenKind::Ident) {
        next();
        if (check(TokenKind::LParen)) {
            next();
            auto e = std::make_unique<Expr>();
            CallExpr c;
            c.callee = std::string(t.text);
            c.line   = t.line;
            c.col    = t.col;
            if (!check(TokenKind::RParen)) {
                for (;;) {
                    c.args.push_back(parse_expr());
                    if (!accept(TokenKind::Comma)) break;
                }
            }
            expect(TokenKind::RParen, "')'");
            e->node = std::move(c);
            return e;
        }
        auto e = std::make_unique<Expr>();
        VarExpr v;
        v.name = std::string(t.text);
        v.line = t.line;
        v.col  = t.col;
        e->node = std::move(v);
        return e;
    }

    if (t.kind == TokenKind::LParen) {
        next();
        auto e = parse_expr();
        expect(TokenKind::RParen, "')'");
        return e;
    }

    std::ostringstream os;
    os << "unexpected token '" << token_kind_name(t.kind) << "' in expression";
    throw ParseError(os.str(), t.line, t.col);
}

} // namespace cfe
