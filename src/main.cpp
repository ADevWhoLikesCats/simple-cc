#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "codegen.h"
#include "backend.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <cstdlib>

namespace cfe { void dump_ast(const Program &p); }

static std::string read_all(const char *path) {
    std::ifstream in(path);
    if (!in) { std::perror(path); std::exit(1); }
    std::stringstream ss; ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char **argv) {
    bool dump_ast_flag = false;
    bool emit_s = false;
    const char *target = "x86_64";
    const char *file = nullptr;

    for (int i = 1; i < argc; i++) {
        if      (!std::strcmp(argv[i], "-dump-ast")) dump_ast_flag = true;
        else if (!std::strcmp(argv[i], "-S"))        emit_s = true;
        else if (!std::strcmp(argv[i], "-t") && i+1 < argc) target = argv[++i];
        else if (argv[i][0] != '-') file = argv[i];
    }

    if (!file) {
        std::fprintf(stderr, "usage: cfe [-dump-ast] [-S] [-t target] <file.c>\n");
        return 1;
    }

    std::string src = read_all(file);

    try {
        cfe::Lexer lex(src);
        cfe::Parser p(lex);
        auto prog = p.parse_program();

        if (dump_ast_flag) cfe::dump_ast(prog);

        cfe::CodeGen cg;
        cg.emit_program(prog);

        const TargetDesc *t = backend_lookup(target);
        if (!t) { std::fprintf(stderr, "unknown target '%s'\n", target); return 1; }

        const char *s_path = "/tmp/cfe_out.s";
        FILE *f = std::fopen(s_path, "w");
        if (!f) { std::perror("fopen /tmp/cfe_out.s"); return 1; }
        target_emit_dispatch(cg.module(), f, t);
        std::fclose(f);

        std::fprintf(stderr, "wrote %s for target %s\n", s_path, target);

        if (emit_s) return 0;

        // Assemble + link + run
        char cmd[512];
        std::snprintf(cmd, sizeof cmd, "as %s -o /tmp/cfe_out.o", s_path);
        if (std::system(cmd)) { std::fprintf(stderr, "assembler failed\n"); return 1; }

        std::snprintf(cmd, sizeof cmd, "ld /tmp/cfe_out.o -o /tmp/cfe_bin");
        if (std::system(cmd)) { std::fprintf(stderr, "linker failed\n"); return 1; }

        int rc = std::system("/tmp/cfe_bin");
        int code = WIFEXITED(rc) ? WEXITSTATUS(rc) : 128;
        std::fprintf(stderr, "program exited with %d\n", code);
        return code;
    } catch (const cfe::LexError &e) {
        std::fprintf(stderr, "%zu:%zu: lex error: %s\n", e.line, e.col, e.what());
        return 1;
    } catch (const cfe::ParseError &e) {
        std::fprintf(stderr, "%zu:%zu: parse error: %s\n", e.line, e.col, e.what());
        return 1;
    } catch (const cfe::CodeGenError &e) {
        std::fprintf(stderr, "codegen error: %s\n", e.what());
        return 1;
    }
}
