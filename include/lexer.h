#pragma once
#include "token.h"
#include <string_view>
#include <stdexcept>

namespace cfe {

struct LexError : std::runtime_error {
    size_t line, col;
    LexError(std::string msg, size_t l, size_t c)
        : std::runtime_error(std::move(msg)), line(l), col(c) {}
};

class Lexer {
public:
    explicit Lexer(std::string_view source) : src_(source) {}

    // Produce the next token. Returns Eof token repeatedly at end.
    Token next();

    // Peek without consuming. Useful for the parser.
    const Token &peek();

private:
    std::string_view src_;
    size_t pos_  = 0;
    size_t line_ = 1;
    size_t col_  = 1;
    Token  peeked_;
    bool   has_peek_ = false;

    char cur() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }
    char at(size_t off) const {
        return pos_ + off < src_.size() ? src_[pos_ + off] : '\0';
    }
    bool eof() const { return pos_ >= src_.size(); }

    void advance();
    void skip_whitespace_and_comments();

    Token make(TokenKind k, size_t start, size_t start_line, size_t start_col);
    Token lex_number();
    Token lex_identifier_or_keyword();
    Token lex_string();
    Token lex_punct();
};

} // namespace cfe
