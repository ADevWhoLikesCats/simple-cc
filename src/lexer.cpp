#include "lexer.h"
#include <cctype>
#include <charconv>
#include <unordered_map>

namespace cfe {

void Lexer::advance() {
    if (eof()) return;
    if (src_[pos_] == '\n') { line_++; col_ = 1; }
    else                    { col_++; }
    pos_++;
}

Token Lexer::make(TokenKind k, size_t start, size_t start_line, size_t start_col) {
    Token t;
    t.kind  = k;
    t.text  = src_.substr(start, pos_ - start);
    t.line  = start_line;
    t.col   = start_col;
    return t;
}

void Lexer::skip_whitespace_and_comments() {
    while (!eof()) {
        char c = cur();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && at(1) == '/') {
            while (!eof() && cur() != '\n') advance();
        } else if (c == '/' && at(1) == '*') {
            size_t sl = line_, sc = col_;
            advance(); advance();
            while (!eof() && !(cur() == '*' && at(1) == '/')) advance();
            if (eof()) throw LexError("unterminated block comment", sl, sc);
            advance(); advance();
        } else {
            break;
        }
    }
}

Token Lexer::lex_number() {
    size_t start = pos_, sl = line_, sc = col_;

    // hex
    if (cur() == '0' && (at(1) == 'x' || at(1) == 'X')) {
        advance(); advance();
        while (std::isxdigit((unsigned char)cur())) advance();
        Token t = make(TokenKind::IntLit, start, sl, sc);
        // strip 0x prefix
        std::string_view digits = t.text.substr(2);
        int64_t v = 0;
        std::from_chars(digits.data(), digits.data() + digits.size(), v, 16);
        t.int_value = v;
        return t;
    }

    // decimal (also handles 0 octal implicitly-ish; keep it simple)
    while (std::isdigit((unsigned char)cur())) advance();

    Token t = make(TokenKind::IntLit, start, sl, sc);
    int64_t v = 0;
    std::from_chars(t.text.data(), t.text.data() + t.text.size(), v, 10);
    t.int_value = v;
    return t;
}

Token Lexer::lex_identifier_or_keyword() {
    size_t start = pos_, sl = line_, sc = col_;
    while (std::isalnum((unsigned char)cur()) || cur() == '_') advance();

    std::string_view word = src_.substr(start, pos_ - start);

    static const std::unordered_map<std::string_view, TokenKind> kws = {
        {"int",      TokenKind::KwInt},
        {"char",     TokenKind::KwChar},
        {"void",     TokenKind::KwVoid},
        {"long",     TokenKind::KwLong},
        {"short",    TokenKind::KwShort},
        {"if",       TokenKind::KwIf},
        {"else",     TokenKind::KwElse},
        {"while",    TokenKind::KwWhile},
        {"for",      TokenKind::KwFor},
        {"return",   TokenKind::KwReturn},
        {"break",    TokenKind::KwBreak},
        {"continue", TokenKind::KwContinue},
        {"sizeof",   TokenKind::KwSizeof},
    };

    auto it = kws.find(word);
    if (it != kws.end()) return make(it->second, start, sl, sc);
    return make(TokenKind::Ident, start, sl, sc);
}

Token Lexer::lex_string() {
    size_t start = pos_, sl = line_, sc = col_;
    advance();  // skip opening "

    while (!eof() && cur() != '"') {
        if (cur() == '\\') {
            advance();  // skip backslash
            if (eof()) break;
            advance();  // skip escaped char
        } else {
            advance();
        }
    }
    if (eof()) throw LexError("unterminated string literal", sl, sc);

    advance();  // skip closing "

    Token t = make(TokenKind::StrLit, start, sl, sc);
    // t.text includes the quotes; the parser can strip them.
    return t;
}

Token Lexer::lex_punct() {
    size_t start = pos_, sl = line_, sc = col_;
    char c  = cur();
    char c1 = at(1);
    char c2 = at(2);

    auto emit2 = [&](TokenKind k) {
        advance(); advance();
        return make(k, start, sl, sc);
    };
    auto emit3 = [&](TokenKind k) {
        advance(); advance(); advance();
        return make(k, start, sl, sc);
    };
    auto emit1 = [&](TokenKind k) {
        advance();
        return make(k, start, sl, sc);
    };

    switch (c) {
        // two-char operators that start with a char that's also a one-char op
        case '+':
            if (c1 == '+') return emit2(TokenKind::PlusPlus);
            if (c1 == '=') return emit2(TokenKind::PlusEq);
            return emit1(TokenKind::Plus);
        case '-':
            if (c1 == '-') return emit2(TokenKind::MinusMinus);
            if (c1 == '=') return emit2(TokenKind::MinusEq);
            if (c1 == '>') return emit2(TokenKind::Arrow);
            return emit1(TokenKind::Minus);
        case '*':
            if (c1 == '=') return emit2(TokenKind::StarEq);
            return emit1(TokenKind::Star);
        case '/':
            if (c1 == '=') return emit2(TokenKind::SlashEq);
            return emit1(TokenKind::Slash);
        case '%':
            if (c1 == '=') return emit2(TokenKind::PercentEq);
            return emit1(TokenKind::Percent);
        case '=':
            if (c1 == '=') return emit2(TokenKind::Eq);
            return emit1(TokenKind::Assign);
        case '!':
            if (c1 == '=') return emit2(TokenKind::Ne);
            return emit1(TokenKind::Bang);
        case '<':
            if (c1 == '<' && c2 == '=') return emit3(TokenKind::ShlEq);
            if (c1 == '<') return emit2(TokenKind::Shl);
            if (c1 == '=') return emit2(TokenKind::Le);
            return emit1(TokenKind::Lt);
        case '>':
            if (c1 == '>' && c2 == '=') return emit3(TokenKind::ShrEq);
            if (c1 == '>') return emit2(TokenKind::Shr);
            if (c1 == '=') return emit2(TokenKind::Ge);
            return emit1(TokenKind::Gt);
        case '&':
            if (c1 == '&') return emit2(TokenKind::AmpAmp);
            if (c1 == '=') return emit2(TokenKind::AmpEq);
            return emit1(TokenKind::Amp);
        case '|':
            if (c1 == '|') return emit2(TokenKind::PipePipe);
            if (c1 == '=') return emit2(TokenKind::PipeEq);
            return emit1(TokenKind::Pipe);
        case '^':
            if (c1 == '=') return emit2(TokenKind::CaretEq);
            return emit1(TokenKind::Caret);
        case '~': return emit1(TokenKind::Tilde);

        case '(': return emit1(TokenKind::LParen);
        case ')': return emit1(TokenKind::RParen);
        case '{': return emit1(TokenKind::LBrace);
        case '}': return emit1(TokenKind::RBrace);
        case '[': return emit1(TokenKind::LBracket);
        case ']': return emit1(TokenKind::RBracket);
        case ';': return emit1(TokenKind::Semi);
        case ',': return emit1(TokenKind::Comma);
        case '.': return emit1(TokenKind::Dot);

        default:
            throw LexError(std::string("unexpected character '") + c + "'", sl, sc);
    }
}

Token Lexer::next() {
    if (has_peek_) {
        has_peek_ = false;
        return peeked_;
    }
    skip_whitespace_and_comments();

    if (eof()) {
        Token t;
        t.kind = TokenKind::Eof;
        t.line = line_; t.col = col_;
        return t;
    }

    char c = cur();
    if (std::isdigit((unsigned char)c))        return lex_number();
    if (std::isalpha((unsigned char)c) || c == '_') return lex_identifier_or_keyword();
    if (c == '"')                              return lex_string();
    return lex_punct();
}

const Token &Lexer::peek() {
    if (!has_peek_) {
        peeked_   = next();
        has_peek_ = true;
    }
    return peeked_;
}

const char *token_kind_name(TokenKind k) {
    switch (k) {
        case TokenKind::IntLit:      return "IntLit";
        case TokenKind::StrLit:      return "StrLit";
        case TokenKind::Ident:       return "Ident";
        case TokenKind::KwInt:       return "int";
        case TokenKind::KwChar:      return "char";
        case TokenKind::KwVoid:      return "void";
        case TokenKind::KwLong:      return "long";
        case TokenKind::KwShort:     return "short";
        case TokenKind::KwIf:        return "if";
        case TokenKind::KwElse:      return "else";
        case TokenKind::KwWhile:     return "while";
        case TokenKind::KwFor:       return "for";
        case TokenKind::KwReturn:    return "return";
        case TokenKind::KwBreak:     return "break";
        case TokenKind::KwContinue:  return "continue";
        case TokenKind::KwSizeof:    return "sizeof";
        case TokenKind::LParen:      return "(";
        case TokenKind::RParen:      return ")";
        case TokenKind::LBrace:      return "{";
        case TokenKind::RBrace:      return "}";
        case TokenKind::LBracket:    return "[";
        case TokenKind::RBracket:    return "]";
        case TokenKind::Semi:        return ";";
        case TokenKind::Comma:       return ",";
        case TokenKind::Dot:         return ".";
        case TokenKind::Arrow:       return "->";
        case TokenKind::Plus:        return "+";
        case TokenKind::Minus:       return "-";
        case TokenKind::Star:        return "*";
        case TokenKind::Slash:       return "/";
        case TokenKind::Percent:     return "%";
        case TokenKind::Assign:      return "=";
        case TokenKind::Eq:          return "==";
        case TokenKind::Ne:          return "!=";
        case TokenKind::Lt:          return "<";
        case TokenKind::Gt:          return ">";
        case TokenKind::Le:          return "<=";
        case TokenKind::Ge:          return ">=";
        case TokenKind::Amp:         return "&";
        case TokenKind::Pipe:        return "|";
        case TokenKind::Caret:       return "^";
        case TokenKind::Tilde:       return "~";
        case TokenKind::AmpAmp:      return "&&";
        case TokenKind::PipePipe:    return "||";
        case TokenKind::Bang:        return "!";
        case TokenKind::Shl:         return "<<";
        case TokenKind::Shr:         return ">>";
        case TokenKind::PlusPlus:    return "++";
        case TokenKind::MinusMinus:  return "--";
        case TokenKind::PlusEq:      return "+=";
        case TokenKind::MinusEq:     return "-=";
        case TokenKind::StarEq:      return "*=";
        case TokenKind::SlashEq:     return "/=";
        case TokenKind::PercentEq:   return "%=";
        case TokenKind::AmpEq:       return "&=";
        case TokenKind::PipeEq:      return "|=";
        case TokenKind::CaretEq:     return "^=";
        case TokenKind::ShlEq:       return "<<=";
        case TokenKind::ShrEq:       return ">>=";
        case TokenKind::Eof:         return "<eof>";
        case TokenKind::Invalid:     return "<invalid>";
    }
    return "<unknown>";
}

} // namespace cfe
