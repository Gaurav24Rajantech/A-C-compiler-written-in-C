/*
 * scc.c — a small C compiler, written in C.
 *
 * Compiles a useful subset of C directly to x86-64 (AT&T syntax) assembly.
 * No external parser/lexer generators, no libraries beyond libc: a single
 * self-contained recursive-descent compiler, in the spirit of chibicc/tcc's
 * early bootstrap stages.
 *
 * Supported language subset:
 *   - int and char types, pointers (int*, char*), 1-D arrays
 *   - functions with int/char/pointer params and return values, recursion
 *   - local variables with initializers
 *   - if / else, while, for, return
 *   - integer literals, char literals, string literals
 *   - operators: + - * / %  == != < <= > >=  && ||  !  = (assign)
 *     unary - & * (address-of / deref), array indexing a[i]
 *   - function calls (including forward-declared / library functions)
 *   - line and block comments
 *
 * Usage:
 *   ./scc input.c > out.s
 *   gcc -o out out.s          # or: as/ld manually
 *
 * Build:
 *   gcc -o scc scc.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================= Utilities ============================= */

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "scc: cannot open %s\n", path); exit(1); }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc(len + 2);
    fread(buf, 1, len, fp);
    buf[len] = '\n';
    buf[len + 1] = '\0';
    fclose(fp);
    return buf;
}

/* ============================== Lexer ================================= */

typedef enum {
    TK_NUM, TK_STR, TK_CHARLIT, TK_IDENT, TK_PUNCT, TK_KEYWORD, TK_EOF
} TokenKind;

typedef struct Token {
    TokenKind kind;
    struct Token *next;
    long ival;          /* TK_NUM / TK_CHARLIT */
    char *sval;          /* TK_STR (raw bytes, len in slen) / TK_IDENT / TK_PUNCT text */
    int slen;
    char *loc;
    int line;
} Token;

static char *src;
static int cur_line = 1;

static const char *keywords[] = {
    "int", "char", "void", "return", "if", "else", "while", "for",
    "sizeof", NULL
};

static int is_keyword(char *s, int len) {
    for (int i = 0; keywords[i]; i++)
        if ((int)strlen(keywords[i]) == len && strncmp(s, keywords[i], len) == 0)
            return 1;
    return 0;
}

static Token *new_token(TokenKind kind, char *start, int len) {
    Token *t = calloc(1, sizeof(Token));
    t->kind = kind;
    t->sval = malloc(len + 1);
    memcpy(t->sval, start, len);
    t->sval[len] = '\0';
    t->slen = len;
    t->loc = start;
    t->line = cur_line;
    return t;
}

static int starts_with(char *p, const char *q) {
    return strncmp(p, q, strlen(q)) == 0;
}

/* multi-char punctuators */
static const char *punct2[] = {"==", "!=", "<=", ">=", "&&", "||", NULL};

static Token *tokenize(char *p) {
    Token head = {0};
    Token *cur = &head;
    src = p;

    while (*p) {
        if (*p == '\n') { cur_line++; p++; continue; }
        if (isspace((unsigned char)*p)) { p++; continue; }

        /* comments */
        if (starts_with(p, "//")) {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (starts_with(p, "/*")) {
            p += 2;
            while (*p && !starts_with(p, "*/")) { if (*p == '\n') cur_line++; p++; }
            if (*p) p += 2;
            continue;
        }

        /* string literal */
        if (*p == '"') {
            char *start = p++;
            char buf[4096]; int n = 0;
            while (*p && *p != '"') {
                if (*p == '\\') {
                    p++;
                    switch (*p) {
                        case 'n': buf[n++] = '\n'; break;
                        case 't': buf[n++] = '\t'; break;
                        case '0': buf[n++] = '\0'; break;
                        case '\\': buf[n++] = '\\'; break;
                        case '"': buf[n++] = '"'; break;
                        default: buf[n++] = *p;
                    }
                    p++;
                } else {
                    buf[n++] = *p++;
                }
            }
            if (*p == '"') p++;
            Token *t = new_token(TK_STR, start, (int)(p - start));
            free(t->sval);
            t->sval = malloc(n + 1);
            memcpy(t->sval, buf, n);
            t->sval[n] = '\0';
            t->slen = n;
            cur = cur->next = t;
            continue;
        }

        /* char literal */
        if (*p == '\'') {
            char *start = p++;
            long v;
            if (*p == '\\') {
                p++;
                switch (*p) {
                    case 'n': v = '\n'; break;
                    case 't': v = '\t'; break;
                    case '0': v = '\0'; break;
                    case '\\': v = '\\'; break;
                    case '\'': v = '\''; break;
                    default: v = *p;
                }
                p++;
            } else {
                v = (unsigned char)*p++;
            }
            if (*p == '\'') p++;
            Token *t = new_token(TK_CHARLIT, start, (int)(p - start));
            t->ival = v;
            cur = cur->next = t;
            continue;
        }

        /* number */
        if (isdigit((unsigned char)*p)) {
            char *start = p;
            long v = strtol(p, &p, 10);
            Token *t = new_token(TK_NUM, start, (int)(p - start));
            t->ival = v;
            cur = cur->next = t;
            continue;
        }

        /* identifier / keyword */
        if (isalpha((unsigned char)*p) || *p == '_') {
            char *start = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            int len = (int)(p - start);
            Token *t = new_token(is_keyword(start, len) ? TK_KEYWORD : TK_IDENT, start, len);
            cur = cur->next = t;
            continue;
        }

        /* multi-char punctuators */
        int matched = 0;
        for (int i = 0; punct2[i]; i++) {
            if (starts_with(p, punct2[i])) {
                cur = cur->next = new_token(TK_PUNCT, p, 2);
                p += 2; matched = 1; break;
            }
        }
        if (matched) continue;

        /* single-char punctuator */
        cur = cur->next = new_token(TK_PUNCT, p, 1);
        p++;
    }
    cur->next = new_token(TK_EOF, p, 0);
    return head.next;
}

/* ============================ Token helpers ============================ */

static Token *tok; /* current token cursor for parser */

static int equal(Token *t, const char *s) {
    return (t->kind == TK_PUNCT || t->kind == TK_KEYWORD) &&
           (int)strlen(s) == t->slen && strncmp(t->sval, s, t->slen) == 0;
}
static Token *skip(Token *t, const char *s) {
    if (!equal(t, s)) {
        fprintf(stderr, "scc: line %d: expected '%s' but got '%s'\n", t->line, s, t->sval);
        exit(1);
    }
    return t->next;
}
static int consume(const char *s) {
    if (equal(tok, s)) { tok = tok->next; return 1; }
    return 0;
}

/* ============================== Types ================================= */

typedef enum { TY_INT, TY_CHAR, TY_PTR, TY_ARRAY, TY_VOID } TypeKind;

typedef struct Type {
    TypeKind kind;
    struct Type *base;  /* for PTR/ARRAY */
    int array_len;
} Type;

static Type *ty_int, *ty_char, *ty_void;

static Type *new_type(TypeKind k) { Type *t = calloc(1, sizeof(Type)); t->kind = k; return t; }
static Type *pointer_to(Type *base) { Type *t = new_type(TY_PTR); t->base = base; return t; }
static Type *array_of(Type *base, int len) { Type *t = new_type(TY_ARRAY); t->base = base; t->array_len = len; return t; }

static int type_size(Type *t) {
    switch (t->kind) {
        case TY_CHAR: return 1;
        case TY_INT: return 4;
        case TY_PTR: return 8;
        case TY_ARRAY: return type_size(t->base) * t->array_len;
        default: return 8;
    }
}

/* ============================== AST ==================================== */

typedef enum {
    ND_NUM, ND_VAR, ND_ADD, ND_SUB, ND_MUL, ND_DIV, ND_MOD,
    ND_EQ, ND_NE, ND_LT, ND_LE, ND_GT, ND_GE, ND_AND, ND_OR, ND_NOT,
    ND_ASSIGN, ND_ADDR, ND_DEREF, ND_INDEX, ND_CALL, ND_STR,
    ND_EXPR_STMT, ND_RETURN, ND_IF, ND_WHILE, ND_FOR, ND_BLOCK, ND_VARDECL,
    ND_NEG
} NodeKind;

typedef struct Var {
    char *name;
    Type *ty;
    int offset;      /* stack offset for locals; unused for globals */
    int is_local;
    int is_param;
    struct Var *next;        /* link in the function's full locals list (declaration order via prepend) */
    struct Var *param_next;  /* separate link for the params-only list, in declaration order */
} Var;

typedef struct Node {
    NodeKind kind;
    struct Node *lhs, *rhs;
    struct Node *cond, *then, *els, *init, *inc;
    struct Node *body;      /* block: list via next */
    struct Node *next;
    long ival;
    Var *var;
    char *funcname;
    struct Node *args;      /* call args, linked via next */
    char *strlit; int strid;
    Type *ty;
} Node;

typedef struct Function {
    char *name;
    Type *ret_ty;
    Var *params;
    Node *body;
    Var *locals;
    int stack_size;
    struct Function *next;
} Function;

static Function *functions;
static Var *cur_locals;
static Var *globals;
static Function *cur_func;

typedef struct StrLit { char *data; int len; int id; struct StrLit *next; } StrLit;
static StrLit *strlits;
static int strlit_id = 0;

static Var *find_var(char *name, int len) {
    for (Var *v = cur_locals; v; v = v->next)
        if ((int)strlen(v->name) == len && strncmp(v->name, name, len) == 0) return v;
    for (Var *v = globals; v; v = v->next)
        if ((int)strlen(v->name) == len && strncmp(v->name, name, len) == 0) return v;
    return NULL;
}

static Var *new_lvar(char *name, Type *ty) {
    Var *v = calloc(1, sizeof(Var));
    v->name = name; v->ty = ty; v->is_local = 1;
    v->next = cur_locals;
    cur_locals = v;
    return v;
}

static Node *new_node(NodeKind kind) { Node *n = calloc(1, sizeof(Node)); n->kind = kind; return n; }
static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs) {
    Node *n = new_node(kind); n->lhs = lhs; n->rhs = rhs; return n;
}
static Node *new_num(long v) { Node *n = new_node(ND_NUM); n->ival = v; return n; }

/* ============================ Parser (decl) ============================= */

static Type *base_type(void) {
    if (consume("int")) return ty_int;
    if (consume("char")) return ty_char;
    if (consume("void")) return ty_void;
    fprintf(stderr, "scc: line %d: expected type, got '%s'\n", tok->line, tok->sval);
    exit(1);
}

/* parses pointer stars after a base type */
static Type *read_pointers(Type *base) {
    while (consume("*")) base = pointer_to(base);
    return base;
}

static Node *expr(void);
static Node *assign(void);
static Node *stmt(void);
static Node *compound_stmt(void);

static Node *funcall(char *name) {
    Node *n = new_node(ND_CALL);
    n->funcname = name;
    Node head = {0}; Node *cur = &head;
    while (!equal(tok, ")")) {
        if (cur != &head) tok = skip(tok, ",");
        cur = cur->next = assign();
    }
    tok = skip(tok, ")");
    n->args = head.next;
    return n;
}

static Node *primary(void) {
    if (consume("(")) {
        Node *n = expr();
        tok = skip(tok, ")");
        return n;
    }
    if (tok->kind == TK_NUM) { Node *n = new_num(tok->ival); tok = tok->next; return n; }
    if (tok->kind == TK_CHARLIT) { Node *n = new_num(tok->ival); tok = tok->next; return n; }
    if (tok->kind == TK_STR) {
        StrLit *s = calloc(1, sizeof(StrLit));
        s->data = tok->sval; s->len = tok->slen; s->id = strlit_id++;
        s->next = strlits; strlits = s;
        Node *n = new_node(ND_STR); n->strlit = s->data; n->strid = s->id;
        tok = tok->next;
        return n;
    }
    if (tok->kind == TK_IDENT) {
        char *name = tok->sval;
        Token *save = tok;
        tok = tok->next;
        if (consume("(")) return funcall(name);
        Var *v = find_var(save->sval, save->slen);
        if (!v) { fprintf(stderr, "scc: line %d: undeclared variable '%s'\n", save->line, name); exit(1); }
        Node *n = new_node(ND_VAR); n->var = v;
        return n;
    }
    fprintf(stderr, "scc: line %d: unexpected token '%s'\n", tok->line, tok->sval);
    exit(1);
}

static Node *postfix(void) {
    Node *n = primary();
    for (;;) {
        if (consume("[")) {
            Node *idx = expr();
            tok = skip(tok, "]");
            Node *add = new_binary(ND_ADD, n, idx);
            add->ty = n->ty; /* size info resolved at codegen for arrays */
            Node *d = new_node(ND_INDEX);
            d->lhs = n; d->rhs = idx;
            n = d;
            continue;
        }
        break;
    }
    return n;
}

static Node *unary(void) {
    if (consume("-")) { Node *n = new_node(ND_NEG); n->lhs = unary(); return n; }
    if (consume("+")) return unary();
    if (consume("!")) { Node *n = new_node(ND_NOT); n->lhs = unary(); return n; }
    if (consume("&")) { Node *n = new_node(ND_ADDR); n->lhs = unary(); return n; }
    if (consume("*")) { Node *n = new_node(ND_DEREF); n->lhs = unary(); return n; }
    if (consume("sizeof")) {
        /* very small subset: sizeof(int)/sizeof(char)/sizeof(var) */
        tok = skip(tok, "(");
        long sz;
        if (equal(tok, "int") || equal(tok, "char") || equal(tok, "void")) {
            Type *t = base_type();
            t = read_pointers(t);
            sz = type_size(t);
        } else {
            Node *e = expr();
            sz = e->kind == ND_VAR ? type_size(e->var->ty) : 8;
        }
        tok = skip(tok, ")");
        return new_num(sz);
    }
    return postfix();
}

static Node *mul(void) {
    Node *n = unary();
    for (;;) {
        if (consume("*")) n = new_binary(ND_MUL, n, unary());
        else if (consume("/")) n = new_binary(ND_DIV, n, unary());
        else if (consume("%")) n = new_binary(ND_MOD, n, unary());
        else return n;
    }
}

static Node *add(void) {
    Node *n = mul();
    for (;;) {
        if (consume("+")) n = new_binary(ND_ADD, n, mul());
        else if (consume("-")) n = new_binary(ND_SUB, n, mul());
        else return n;
    }
}

static Node *relational(void) {
    Node *n = add();
    for (;;) {
        if (consume("<")) n = new_binary(ND_LT, n, add());
        else if (consume("<=")) n = new_binary(ND_LE, n, add());
        else if (consume(">")) n = new_binary(ND_GT, n, add());
        else if (consume(">=")) n = new_binary(ND_GE, n, add());
        else return n;
    }
}

static Node *equality(void) {
    Node *n = relational();
    for (;;) {
        if (consume("==")) n = new_binary(ND_EQ, n, relational());
        else if (consume("!=")) n = new_binary(ND_NE, n, relational());
        else return n;
    }
}

static Node *logand(void) {
    Node *n = equality();
    while (consume("&&")) n = new_binary(ND_AND, n, equality());
    return n;
}
static Node *logor(void) {
    Node *n = logand();
    while (consume("||")) n = new_binary(ND_OR, n, logand());
    return n;
}

static Node *assign(void) {
    Node *n = logor();
    if (consume("=")) n = new_binary(ND_ASSIGN, n, assign());
    return n;
}

static Node *expr(void) { return assign(); }

static int is_type_start(Token *t) {
    return equal(t, "int") || equal(t, "char") || equal(t, "void");
}

static Node *declaration(void) {
    Type *base = base_type();
    Type *ty = read_pointers(base);
    char *name = tok->sval;
    tok = tok->next; /* ident */
    if (consume("[")) {
        long len = tok->ival;
        tok = tok->next;
        tok = skip(tok, "]");
        ty = array_of(ty, (int)len);
    }
    Var *v = new_lvar(name, ty);
    Node *n = new_node(ND_VARDECL);
    n->var = v;
    if (consume("=")) {
        Node *val = assign();
        n->rhs = val;
    }
    tok = skip(tok, ";");
    return n;
}

static Node *stmt(void) {
    if (consume("return")) {
        Node *n = new_node(ND_RETURN);
        if (!equal(tok, ";")) n->lhs = expr();
        tok = skip(tok, ";");
        return n;
    }
    if (consume("if")) {
        Node *n = new_node(ND_IF);
        tok = skip(tok, "(");
        n->cond = expr();
        tok = skip(tok, ")");
        n->then = stmt();
        if (consume("else")) n->els = stmt();
        return n;
    }
    if (consume("while")) {
        Node *n = new_node(ND_WHILE);
        tok = skip(tok, "(");
        n->cond = expr();
        tok = skip(tok, ")");
        n->then = stmt();
        return n;
    }
    if (consume("for")) {
        Node *n = new_node(ND_FOR);
        tok = skip(tok, "(");
        if (!equal(tok, ";")) {
            if (is_type_start(tok)) n->init = declaration();
            else { n->init = new_node(ND_EXPR_STMT); n->init->lhs = expr(); tok = skip(tok, ";"); }
        } else tok = skip(tok, ";");
        if (!equal(tok, ";")) n->cond = expr();
        tok = skip(tok, ";");
        if (!equal(tok, ")")) n->inc = expr();
        tok = skip(tok, ")");
        n->then = stmt();
        return n;
    }
    if (equal(tok, "{")) return compound_stmt();
    if (is_type_start(tok)) return declaration();

    Node *n = new_node(ND_EXPR_STMT);
    n->lhs = expr();
    tok = skip(tok, ";");
    return n;
}

static Node *compound_stmt(void) {
    tok = skip(tok, "{");
    Node *n = new_node(ND_BLOCK);
    Node head = {0}; Node *cur = &head;
    while (!equal(tok, "}")) cur = cur->next = stmt();
    tok = skip(tok, "}");
    n->body = head.next;
    return n;
}

static void function(void) {
    Type *ret = base_type();
    ret = read_pointers(ret);
    char *name = tok->sval;
    tok = tok->next;
    tok = skip(tok, "(");

    cur_locals = NULL;
    Var *plist = NULL, *ptail = NULL;
    if (!equal(tok, ")")) {
        for (;;) {
            Type *pty = base_type();
            pty = read_pointers(pty);
            char *pname = tok->sval;
            tok = tok->next;
            Var *v = new_lvar(pname, pty);
            v->is_param = 1;
            /* param_next is a dedicated link, independent of v->next (used by cur_locals) */
            if (!ptail) plist = ptail = v; else { ptail->param_next = v; ptail = v; }
            if (!consume(",")) break;
        }
    }
    tok = skip(tok, ")");

    Function *f = calloc(1, sizeof(Function));
    f->name = name; f->ret_ty = ret; f->params = plist;
    cur_func = f;

    f->body = compound_stmt();
    f->locals = cur_locals;

    f->next = functions;
    functions = f;
}

static void program(void) {
    while (tok->kind != TK_EOF) {
        function();
    }
}

/* ============================= Codegen ================================= */

static FILE *out;
static int label_id = 0;
static const char *argreg64[] = {"%rdi","%rsi","%rdx","%rcx","%r8","%r9"};
static const char *argreg32[] = {"%edi","%esi","%edx","%ecx","%r8d","%r9d"};

static void gen_expr(Node *n);
static void gen_addr(Node *n);
static void gen_stmt(Node *n);
static Type *node_type(Node *n);

/* assign stack offsets to a function's locals */
static void assign_lvar_offsets(Function *f) {
    int offset = 0;
    for (Var *v = f->locals; v; v = v->next) {
        offset += type_size(v->ty);
        /* align to at least its own size, and ints/ptrs to 4/8 */
        int align = type_size(v->ty);
        if (align > 8) align = 8;
        offset = (offset + align - 1) / align * align;
        v->offset = -offset;
    }
    offset = (offset + 15) / 16 * 16;
    f->stack_size = offset;
}

static void gen_addr(Node *n) {
    switch (n->kind) {
        case ND_VAR:
            if (n->var->is_local) {
                fprintf(out, "    lea %d(%%rbp), %%rax\n", n->var->offset);
            } else {
                fprintf(out, "    lea %s(%%rip), %%rax\n", n->var->name);
            }
            return;
        case ND_DEREF:
            gen_expr(n->lhs);
            return;
        case ND_INDEX: {
            /* &(base[idx]) = base_ptr + idx*elemsize */
            Type *bty = node_type(n->lhs);
            int esz = 4;
            if (bty && (bty->kind == TY_ARRAY || bty->kind == TY_PTR)) esz = type_size(bty->base);
            if (n->lhs->kind == ND_VAR && n->lhs->var->ty->kind == TY_ARRAY) {
                gen_addr(n->lhs); /* array decays to its base address */
            } else {
                gen_expr(n->lhs); /* pointer value */
            }
            fprintf(out, "    push %%rax\n");
            gen_expr(n->rhs);
            fprintf(out, "    imul $%d, %%rax\n", esz);
            fprintf(out, "    mov %%rax, %%rcx\n");
            fprintf(out, "    pop %%rax\n");
            fprintf(out, "    add %%rcx, %%rax\n");
            return;
        }
        default:
            fprintf(stderr, "scc: not an lvalue (node kind %d)\n", n->kind);
            exit(1);
    }
}

/* Best-effort type inference for an expression node. Used to pick the
 * right load/store width and to scale pointer arithmetic. Not a full
 * type checker, but covers the subset this compiler accepts. */
static Type *node_type(Node *n) {
    if (!n) return ty_int;
    switch (n->kind) {
        case ND_VAR: return n->var->ty;
        case ND_ADDR: return pointer_to(node_type(n->lhs));
        case ND_DEREF: case ND_INDEX: {
            Type *t = node_type(n->lhs);
            if (t && (t->kind == TY_PTR || t->kind == TY_ARRAY)) return t->base;
            return ty_int;
        }
        case ND_ADD: case ND_SUB: {
            Type *lt = node_type(n->lhs), *rt = node_type(n->rhs);
            if (lt && (lt->kind == TY_PTR || lt->kind == TY_ARRAY)) return lt;
            if (rt && (rt->kind == TY_PTR || rt->kind == TY_ARRAY)) return rt;
            return ty_int;
        }
        case ND_STR: return pointer_to(ty_char);
        default: return ty_int;
    }
}

static int node_size(Node *n) {
    Type *t = node_type(n);
    if (t->kind == TY_ARRAY) return type_size(t->base); /* size of one element */
    return type_size(t);
}

static void load(Node *n) {
    int sz = node_size(n);
    if (n->kind == ND_VAR && n->var->ty->kind == TY_ARRAY) return; /* array decays, address already in rax */
    if (sz == 1) fprintf(out, "    movsbq (%%rax), %%rax\n");
    else if (sz == 4) fprintf(out, "    movslq (%%rax), %%rax\n");
    else fprintf(out, "    mov (%%rax), %%rax\n");
}

static void store(Node *n) {
    int sz = node_size(n);
    fprintf(out, "    pop %%rdi\n");        /* address */
    if (sz == 1) fprintf(out, "    mov %%al, (%%rdi)\n");
    else if (sz == 4) fprintf(out, "    mov %%eax, (%%rdi)\n");
    else fprintf(out, "    mov %%rax, (%%rdi)\n");
}

static void gen_expr(Node *n) {
    switch (n->kind) {
        case ND_NUM: fprintf(out, "    mov $%ld, %%rax\n", n->ival); return;
        case ND_STR: fprintf(out, "    lea .Lstr%d(%%rip), %%rax\n", n->strid); return;
        case ND_NEG: gen_expr(n->lhs); fprintf(out, "    neg %%rax\n"); return;
        case ND_NOT:
            gen_expr(n->lhs);
            fprintf(out, "    cmp $0, %%rax\n    sete %%al\n    movzb %%al, %%rax\n");
            return;
        case ND_VAR:
            gen_addr(n);
            load(n);
            return;
        case ND_DEREF:
            gen_expr(n->lhs);
            load(n);
            return;
        case ND_INDEX:
            gen_addr(n);
            load(n);
            return;
        case ND_ADDR:
            gen_addr(n->lhs);
            return;
        case ND_ASSIGN:
            gen_addr(n->lhs);
            fprintf(out, "    push %%rax\n");
            gen_expr(n->rhs);
            store(n->lhs);
            return;
        case ND_CALL: {
            int nargs = 0;
            Node *args[6];
            for (Node *a = n->args; a; a = a->next) args[nargs++] = a;
            for (int i = nargs - 1; i >= 0; i--) { gen_expr(args[i]); fprintf(out, "    push %%rax\n"); }
            for (int i = 0; i < nargs; i++) fprintf(out, "    pop %s\n", argreg64[i]);
            fprintf(out, "    mov $0, %%al\n");
            fprintf(out, "    call %s\n", n->funcname);
            return;
        }
        case ND_AND: {
            int id = label_id++;
            gen_expr(n->lhs);
            fprintf(out, "    cmp $0, %%rax\n    je .Lfalse%d\n", id);
            gen_expr(n->rhs);
            fprintf(out, "    cmp $0, %%rax\n    je .Lfalse%d\n", id);
            fprintf(out, "    mov $1, %%rax\n    jmp .Lend%d\n", id);
            fprintf(out, ".Lfalse%d:\n    mov $0, %%rax\n.Lend%d:\n", id, id);
            return;
        }
        case ND_OR: {
            int id = label_id++;
            gen_expr(n->lhs);
            fprintf(out, "    cmp $0, %%rax\n    jne .Ltrue%d\n", id);
            gen_expr(n->rhs);
            fprintf(out, "    cmp $0, %%rax\n    jne .Ltrue%d\n", id);
            fprintf(out, "    mov $0, %%rax\n    jmp .Lend%d\n", id);
            fprintf(out, ".Ltrue%d:\n    mov $1, %%rax\n.Lend%d:\n", id, id);
            return;
        }
        case ND_ADD: case ND_SUB: {
            /* Pointer arithmetic: scale the integer operand by the size
             * of the pointee, exactly like real C (p + i advances i
             * *elements*, not i bytes). Plain int +/- falls through to
             * the generic path below. */
            Type *lt = node_type(n->lhs), *rt = node_type(n->rhs);
            int lptr = lt->kind == TY_PTR || lt->kind == TY_ARRAY;
            int rptr = rt->kind == TY_PTR || rt->kind == TY_ARRAY;

            if (lptr && rptr && n->kind == ND_SUB) {
                /* ptr - ptr => element distance */
                int esz = type_size(lt->base); if (esz < 1) esz = 1;
                gen_expr(n->lhs);
                fprintf(out, "    push %%rax\n");
                gen_expr(n->rhs);
                fprintf(out, "    mov %%rax, %%rcx\n    pop %%rax\n    sub %%rcx, %%rax\n");
                if (esz != 1) fprintf(out, "    mov $%d, %%rcx\n    cqo\n    idiv %%rcx\n", esz);
                return;
            }
            if (lptr && !rptr) {
                int esz = type_size(lt->base); if (esz < 1) esz = 1;
                gen_expr(n->lhs);
                fprintf(out, "    push %%rax\n");
                gen_expr(n->rhs);
                if (esz != 1) fprintf(out, "    imul $%d, %%rax\n", esz);
                fprintf(out, "    mov %%rax, %%rcx\n    pop %%rax\n");
                fprintf(out, n->kind == ND_ADD ? "    add %%rcx, %%rax\n" : "    sub %%rcx, %%rax\n");
                return;
            }
            if (rptr && !lptr && n->kind == ND_ADD) {
                int esz = type_size(rt->base); if (esz < 1) esz = 1;
                gen_expr(n->rhs);
                fprintf(out, "    push %%rax\n");
                gen_expr(n->lhs);
                if (esz != 1) fprintf(out, "    imul $%d, %%rax\n", esz);
                fprintf(out, "    mov %%rax, %%rcx\n    pop %%rax\n    add %%rcx, %%rax\n");
                return;
            }
            break; /* both int: fall through to the generic binary path */
        }
        default: break;
    }

    /* binary arithmetic/comparison */
    gen_expr(n->lhs);
    fprintf(out, "    push %%rax\n");
    gen_expr(n->rhs);
    fprintf(out, "    mov %%rax, %%rcx\n");
    fprintf(out, "    pop %%rax\n");

    switch (n->kind) {
        case ND_ADD: fprintf(out, "    add %%rcx, %%rax\n"); break;
        case ND_SUB: fprintf(out, "    sub %%rcx, %%rax\n"); break;
        case ND_MUL: fprintf(out, "    imul %%rcx, %%rax\n"); break;
        case ND_DIV: fprintf(out, "    cqo\n    idiv %%rcx\n"); break;
        case ND_MOD: fprintf(out, "    cqo\n    idiv %%rcx\n    mov %%rdx, %%rax\n"); break;
        case ND_EQ: fprintf(out, "    cmp %%rcx, %%rax\n    sete %%al\n    movzb %%al, %%rax\n"); break;
        case ND_NE: fprintf(out, "    cmp %%rcx, %%rax\n    setne %%al\n    movzb %%al, %%rax\n"); break;
        case ND_LT: fprintf(out, "    cmp %%rcx, %%rax\n    setl %%al\n    movzb %%al, %%rax\n"); break;
        case ND_LE: fprintf(out, "    cmp %%rcx, %%rax\n    setle %%al\n    movzb %%al, %%rax\n"); break;
        case ND_GT: fprintf(out, "    cmp %%rcx, %%rax\n    setg %%al\n    movzb %%al, %%rax\n"); break;
        case ND_GE: fprintf(out, "    cmp %%rcx, %%rax\n    setge %%al\n    movzb %%al, %%rax\n"); break;
        default:
            fprintf(stderr, "scc: unhandled node kind %d\n", n->kind);
            exit(1);
    }
}

static void gen_stmt(Node *n) {
    switch (n->kind) {
        case ND_EXPR_STMT: gen_expr(n->lhs); return;
        case ND_RETURN:
            if (n->lhs) gen_expr(n->lhs);
            fprintf(out, "    jmp .Lreturn.%s\n", cur_func->name);
            return;
        case ND_VARDECL:
            if (n->rhs) {
                gen_addr((Node[]){{.kind = ND_VAR, .var = n->var}}); /* address of new var */
                fprintf(out, "    push %%rax\n");
                gen_expr(n->rhs);
                Node tmp = {0}; tmp.kind = ND_VAR; tmp.var = n->var;
                store(&tmp);
            }
            return;
        case ND_BLOCK:
            for (Node *s = n->body; s; s = s->next) gen_stmt(s);
            return;
        case ND_IF: {
            int id = label_id++;
            gen_expr(n->cond);
            fprintf(out, "    cmp $0, %%rax\n    je .Lelse%d\n", id);
            gen_stmt(n->then);
            fprintf(out, "    jmp .Lifend%d\n.Lelse%d:\n", id, id);
            if (n->els) gen_stmt(n->els);
            fprintf(out, ".Lifend%d:\n", id);
            return;
        }
        case ND_WHILE: {
            int id = label_id++;
            fprintf(out, ".Lbegin%d:\n", id);
            gen_expr(n->cond);
            fprintf(out, "    cmp $0, %%rax\n    je .Lend%d\n", id);
            gen_stmt(n->then);
            fprintf(out, "    jmp .Lbegin%d\n.Lend%d:\n", id, id);
            return;
        }
        case ND_FOR: {
            int id = label_id++;
            if (n->init) gen_stmt(n->init);
            fprintf(out, ".Lbegin%d:\n", id);
            if (n->cond) { gen_expr(n->cond); fprintf(out, "    cmp $0, %%rax\n    je .Lend%d\n", id); }
            gen_stmt(n->then);
            if (n->inc) gen_expr(n->inc);
            fprintf(out, "    jmp .Lbegin%d\n.Lend%d:\n", id, id);
            return;
        }
        default:
            fprintf(stderr, "scc: unhandled stmt kind %d\n", n->kind);
            exit(1);
    }
}

static void emit_function(Function *f) {
    assign_lvar_offsets(f);
    fprintf(out, "    .globl %s\n%s:\n", f->name, f->name);
    fprintf(out, "    push %%rbp\n    mov %%rsp, %%rbp\n    sub $%d, %%rsp\n", f->stack_size);

    int i = 0;
    for (Var *v = f->params; v; v = v->param_next) {
        int sz = type_size(v->ty);
        if (sz == 1) fprintf(out, "    mov %s, %d(%%rbp)\n", argreg32[i], v->offset);
        else if (sz == 4) fprintf(out, "    mov %s, %d(%%rbp)\n", argreg32[i], v->offset);
        else fprintf(out, "    mov %s, %d(%%rbp)\n", argreg64[i], v->offset);
        i++;
    }

    gen_stmt(f->body);

    fprintf(out, ".Lreturn.%s:\n", f->name);
    fprintf(out, "    mov %%rbp, %%rsp\n    pop %%rbp\n    ret\n");
}

static void emit_strlits(void) {
    if (!strlits) return;
    fprintf(out, "    .section .rodata\n");
    for (StrLit *s = strlits; s; s = s->next) {
        fprintf(out, ".Lstr%d:\n    .byte ", s->id);
        for (int i = 0; i < s->len; i++) fprintf(out, "%d,", (unsigned char)s->data[i]);
        fprintf(out, "0\n");
    }
    fprintf(out, "    .text\n");
}

/* ================================ main ================================== */

/* need vsnprintf/va_list for the (unused) error() above */
#include <stdarg.h>

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s file.c\n", argv[0]); return 1; }

    ty_int = new_type(TY_INT);
    ty_char = new_type(TY_CHAR);
    ty_void = new_type(TY_VOID);

    char *code = read_file(argv[1]);
    tok = tokenize(code);
    program();

    /* functions were pushed onto a stack in reverse declaration order */
    Function *rev = NULL;
    for (Function *f = functions; f; ) {
        Function *nx = f->next;
        f->next = rev;
        rev = f;
        f = nx;
    }
    functions = rev;

    out = stdout;
    fprintf(out, "    .text\n");
    for (Function *f = functions; f; f = f->next) {
        cur_func = f;
        emit_function(f);
    }
    emit_strlits();

    return 0;
}
