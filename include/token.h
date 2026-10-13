#pragma once
#include <cstdint>
#include <string_view>

namespace cfe {

enum class TokenKind {
    // literals
    IntLit,        // 123
    StrLit,        // "hello"
    Ident,         // foo

    // keywords
    KwInt, KwChar, KwVoid, KwLong, KwShort,
    KwIf, KwElse, KwWhile, KwFor, KwReturn,
    KwBreak, KwContinue, KwSizeof,

    // punctuation
    LParen, RParen,      // ( )
    LBrace, RBrace,      // { }
    LBracket, RBracket,  // [ ]
    Semi, Comma,         // ; ,
    Dot, Arrow,          // . ->

    // operators
    Plus, Minus, Star, Slash, Percent,
    Assign,              // =
    Eq, Ne,              // == !=
    Lt, Gt, Le, Ge,      // < > <= >=
    Amp, Pipe, Caret, Tilde,      // & | ^ ~
    AmpAmp, PipePipe,             // && ||
    Bang,                         // !
    Shl, Shr,                     // << >>
    PlusPlus, MinusMinus,         // ++ --
    PlusEq, MinusEq, StarEq, SlashEq, PercentEq,  // += -= *= /= %=
    AmpEq, PipeEq, CaretEq, ShlEq, ShrEq,         // &= |= ^= <<= >>=

    Eof,
    Invalid
};

struct Token {
    TokenKind kind = TokenKind::Invalid;
    std::string_view text;        // view into source buffer
    int64_t int_value = 0;        // for IntLit
    size_t line = 1, col = 1;     // 1-based, for diagnostics

    bool is(TokenKind k) const { return kind == k; }
    bool is_any(auto... ks) const { return ((kind == ks) || ...); }
};

const char *token_kind_name(TokenKind k);

} // namespace cfe
