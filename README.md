# cfe — a C frontend for mini-asm

A C compiler frontend written in C++20 that targets the
[mini-asm](https://github.com/ADevWhoLikesCats/mini-asm) compiler backend.

Currently supports a useful subset of C:

- `int`-typed functions and parameters
- Local variables (`int x = ...;` and `int x;`)
- Assignment and re-assignment (SSA with phi insertion for loops)
- Arithmetic: `+ - * / %`
- Comparisons: `== != < <= > >=`
- `if` / `else`
- `while` loops (including nested loops)
- Function calls with up to 16 arguments
- `return`

Not yet supported (planned):

- `for` loops
- Pointers, arrays, structs
- `char`, `long`, `short`, `float`, `double`
- Function declarations (only definitions)
- Globals
- `void` functions that call other functions
- `if`-merge phi insertion (currently wrong when a variable is assigned
  in one branch and read after)

## Pipeline

    C source
      → Lexer        (include/lexer.h, src/lexer.cpp)
      → Parser       (include/parser.h, src/parser.cpp)
      → AST          (include/ast.h)
      → CodeGen      (include/codegen.h, src/codegen.cpp)
      → mini-asm IR  (via libmini-asm's ir_builder.h)
      → x86_64 asm, ELF binary

The frontend owns lexing, parsing, and IR construction. Everything
from IR onward — optimization, register allocation, code emission,
assembling, linking — is handled by mini-asm.

## Building

Requires:

- A C++20 compiler (g++ 10+, clang 12+)
- mini-asm built as a static library (`libmini-asm.a`)
- GNU `as` and `ld` for the target architecture

Clone and build mini-asm first:

    git clone https://github.com/ADevWhoLikesCats/mini-asm ~/Downloads/cc-backend
    cd ~/Downloads/cc-backend
    make lib

Then build cfe:

    mkdir -p lib
    cp ~/Downloads/cc-backend/libmini-asm.a lib/
    g++ -std=c++20 \
        -Iinclude -I$HOME/Downloads/cc-backend/include \
        src/lexer.cpp src/parser.cpp src/ast_dump.cpp \
        src/codegen.cpp src/main.cpp \
        lib/libmini-asm.a \
        -o cfe

## Usage

    cfe [options] <file.c>

Options:

- `-S`               — emit assembly to `/tmp/cfe_out.s`, don't assemble
- `-dump-ast`        — print the parsed AST to stderr
- `-t <target>`      — select backend target (default `x86_64`)

Examples:

    cfe -dump-ast test.c        # see the AST
    cfe -S test.c               # see the generated .s
    cfe test.c                  # compile, link, run; prints exit code

The exit code of the compiled program is printed to stderr and
returned as cfe's own exit status.

## Example

    $ cat hello.c
    int main() {
        int i = 0;
        int sum = 0;
        while (i < 10) {
            sum = sum + i;
            i = i + 1;
        }
        return sum;
    }

    $ ./cfe hello.c
    wrote /tmp/cfe_out.s for target x86_64
    program exited with 45

## Project layout

    include/
      token.h       token kinds
      lexer.h       lexer interface
      ast.h         AST nodes (std::variant-based)
      parser.h      parser interface
      codegen.h     codegen interface
    src/
      lexer.cpp
      parser.cpp
      ast_dump.cpp  AST pretty-printer for debugging
      codegen.cpp   AST → mini-asm IR
      main.cpp      driver

## Design notes

**Lexer.** Hand-written, one-token lookahead. Tokens hold
`std::string_view` into the source buffer, so lexing is
allocation-free. Keywords are looked up in a static map.

**AST.** `std::variant` of structs rather than a class hierarchy.
`std::unique_ptr` for children. Visitors use the standard
`overloaded` trick. This keeps the tree compact and avoids
virtual dispatch.

**Codegen.** Direct SSA construction — no alloca/load/store,
no separate mem2reg pass. Each local variable maps to an SSA
value in a scope stack. Assignments rebind. Reads look up.

For `while` loops, loop-carried variables are identified
(assigned in the body, declared before the loop) and given
phi nodes at the loop head. mini-asm's `ir_value_placeholder`
+ `ir_value_define` API handles the forward references.

For `if`/`else`, merge-block phi insertion is not yet
implemented. Assigning to a variable inside one branch and
reading it after the `if` gives incorrect results.

## Status

Working:

- `return <int>;`
- Arithmetic and comparisons
- Local variables and assignment
- `if`/`else` (no merge phis yet)
- `while` loops with loop-carried variables
- Nested loops
- Function calls with parameters

Tested against:

    int main() { return 42; }                          → 42
    int main() { return 10 + 20 * 3 > 50 ? 1 : 0; }    → (n/a, no ternary)
    int add(int a, int b) { return a + b; }
    int main() { return add(1, 2) + 10; }              → 13
    int main() { ... while (i < 10) sum += i; ... }    → 45
    int main() { ... nested while ... }                → 9

## License

Apache 2.0 (matching mini-asm).
