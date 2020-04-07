// Copyright 2012 Rui Ueyama. Released under the MIT license.

#define __MY65816__

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "8cc.h"

static void emit_lsave(Type *ty, int off);
static void ensure_lvar_init(Node *node);
static void emit_label(const char *s);
static void emit_pre_op(Node *node, char op);
static void emit_post_op(Node *node, char op);

bool dumpstack = false;
bool dumpsource = false;

FILE *outputfd = NULL;

void set_output_file(FILE *fd) {
    outputfd = fd;
}

void close_output_file(void) {
    fclose(outputfd);
}

static void emit_line(unsigned int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    fprintf(outputfd, "; gen.c:%u\n", line);

    vfprintf(outputfd, fmt, args);

    fprintf(outputfd, "\n");
}

#define emit(...) (emit_line(__LINE__, "\t" __VA_ARGS__))
#define emit_noident(...) (emit_line(__LINE__, __VA_ARGS__))

static int stackpos = 0;

static void emit_text_segment(void) {
    emit_noident(".segment \"C_CODE\":far");
}

static void emit_data_segment(void) {
    emit_noident(".segment \"C_DATA\":absolute");
}

static void emit_bss_segment(void) {
    emit_noident(".segment \"C_BSS\":absolute");
}

void emit_literal(Node *node) {
    switch(node->ty->kind) {
        case KIND_BOOL:
            emit("lda #$%02X", !!node->ival);
            break;
        case KIND_SHORT:
        case KIND_CHAR:
            emit("lda #$%02X", node->ival);
            break;
        case KIND_INT:
            emit("lda #$%04X", node->ival);
            break;
        case KIND_LONG:
            emit("lda #$%04X", node->ival & 0xFFFF);
            emit("ldx #$%04X", (node->ival >> 16) & 0xFFFF);
            break;
        default:
            assert(0);
    }
}

void emit_lload(Type *ty, int off) {
    assert(ty->bitsize <= 0);

    if (ty->kind == KIND_ARRAY) {
        /* see emit_addr */
        emit("tsc");
        emit("clc");
        emit("adc #$%04x", (1 + stackpos - off));
    } else if (ty->kind == KIND_FLOAT) {
        assert(0);
    } else if (ty->kind == KIND_DOUBLE || ty->kind == KIND_LDOUBLE) {
        assert(0);
    } else {
        switch (ty->size) {
            case 1:
                emit("lda $%02x,S", 1 + stackpos - off);
                emit("and #$00FF");
                break;
            case 2:
                emit("; off = %d (stackpos = %u)", off, stackpos);
                emit("lda $%02x,S", 1 + stackpos - off);
                break;
            case 4:
                emit("lda $%02x,S", 1 + stackpos - off + 1);
                emit("tax");
                emit("lda $%02x,S", 1 + stackpos - off);
                break;
            default:
                assert(0);
        }
    }
}

void emit_lvar(Node *node) {
    ensure_lvar_init(node);
    emit_lload(node->ty, node->loff);
}

/* TODO: for large num there's better ways */
static void emit_stack_cleanup(size_t num) {
    if (num % 2 != 0) {
        emit("phb");
        num++;
    }

    for (size_t i = 0; i < num; i += 2) {
        emit("ply");
    }
}

/* cleanup stack and emit return instruction */
void emit_ret(void) {
    emit_stack_cleanup(stackpos);
    emit("rtl");
}

static void emit_return(Node *node) {
    if (node->retval) {
        emit_expr(node->retval);
        // maybe_booleanize_retval(node->retval->ty);
    }

    emit_ret();
}

static void emit_intcast(Type *from) {
    switch (from->kind) {
        case KIND_BOOL:
        case KIND_CHAR:
        case KIND_SHORT:
            emit("and #$00ff");
            if (! from->usig) {
                /* sign-extend */
                char *l = make_label();
                emit("bpl %s", l);
                emit("eor #$ff00");
                emit_label(l);
            }

            /* fall-through */
        case KIND_INT:
            emit("ldx #$0000");
            break;
        case KIND_LONG:
            break;
        default:
            assert(0);
    }
}

static void emit_load_convert(Type *to, Type *from) {
    if (is_inttype(from) && to->kind == KIND_FLOAT) {
        assert(0);
    } else if (is_inttype(from) && to->kind == KIND_DOUBLE) {
        assert(0);
    } else if (from->kind == KIND_FLOAT && to->kind == KIND_DOUBLE) {
        assert(0);
    } else if ((from->kind == KIND_DOUBLE || from->kind == KIND_LDOUBLE) && to->kind == KIND_FLOAT) {
        assert(0);
    } else if (to->kind == KIND_BOOL) {
        assert(0);
    } else if (is_inttype(from) && is_inttype(to)) {
        emit_intcast(from);
    } else if (is_inttype(to)) {
    }
}

static void emit_conv(Node *node) {
    emit_expr(node->operand);
    emit_load_convert(node->ty, node->operand->ty);
}

static void emit_save_literal(Node *node, Type *totype, int off) {
    switch(totype->kind) {
        case KIND_BOOL:
        case KIND_CHAR:
        case KIND_SHORT:
            assert(0);
        case KIND_INT:
        case KIND_PTR:
            emit("tay");
            emit("lda #$%04x", node->ival);
            emit("sta $%02X,S", 1 + stackpos - off);
            emit("tya");
            break;
        case KIND_LONG:
        case KIND_LLONG:
            assert(0);
        case KIND_FLOAT:
        case KIND_DOUBLE:
        case KIND_LDOUBLE:
            assert(0);
        default:
            error("internal error: <%s> <%s> <%d>", node2s(node), ty2s(totype), off);
    }
}

static void emit_zero_filler(size_t start, size_t end) {
    assert((end - start) % 2 == 0); // TODO: implement
    if ((end - start) > 0) {
        emit("lda #$0000");
        for (;start <= end - 2; start += 2) {
            emit("sta $%02X,S", start);
        }
    }
}

/* code from the original gen.c i don't claim i understand this */
static int cmpinit(const void *x, const void *y) {
    Node *a = *(Node **)x;
    Node *b = *(Node **)y;
    return a->initoff - b->initoff;
}
static void emit_fill_holes(Vector *inits, int off, int totalsize) {
    /* if atleast one of the fields in a variable is initialized,
     * all others have to be initialized to 0 */
    size_t len = vec_len(inits);
    Node **buf = malloc(len * sizeof(Node *));
    for (size_t i = 0; i < len; i++) {
        buf[i] = vec_get(inits, i);
    }
    qsort(buf, len, sizeof(Node *), cmpinit);

    size_t lastend = 0;
    for (size_t i = 0; i < len; i++) {
        Node *node = buf[i];
        if (lastend < node->initoff) {
            emit_zero_filler(lastend + off, node->initoff + off);
        }

        lastend = node->initoff + node->totype->size;
    }

    emit_zero_filler(lastend + off, totalsize + off);
}

static void emit_decl_init(Vector *inits, int off, int totalsize) {
    emit_fill_holes(inits, off, totalsize);

    for (size_t i = 0; i < vec_len(inits); i++) {
        Node *node = vec_get(inits, i);
        assert(node->kind == AST_INIT);

        bool isbitfield = (node->totype->bitsize > 0);
        if (node->initval->kind == AST_LITERAL && !isbitfield) {
            emit_save_literal(node->initval, node->totype, node->initoff + off);
        } else {
            emit_expr(node->initval);
            emit_lsave(node->totype, node->initoff + off);
        }
    }
}

static void ensure_lvar_init(Node *node) {
    assert(node->kind == AST_LVAR);
    if (node->lvarinit)
        emit_decl_init(node->lvarinit, node->loff, node->ty->size);
    node->lvarinit = NULL;
}

static void emit_decl(Node *node) {
    if (node->declvar->ty->isextern) {
        emit_noident(".import %s", node->declvar->glabel);
    }

    if (node->declinit) {
        emit_decl_init(node->declinit, node->declvar->loff, node->declvar->ty->size);
    }
}

void emit_binop_int(Node *node) {
    switch (node->kind) {
        case '+':
            if (node->left->kind == AST_LITERAL) {
                emit_expr(node->right);
                emit("clc");
                emit("adc #$%04X", node->left->ival);
            } else if (node->right->kind == AST_LITERAL) {
                emit_expr(node->left);
                emit("clc");
                emit("adc #$%04X", node->right->ival);
            } else {
                emit_expr(node->left);
                emit("pha");
                stackpos += 2;
                size_t stackpos_saved = stackpos;
                emit_expr(node->right);
                assert(stackpos_saved == stackpos);
                emit("clc");
                emit("adc $1,S");
                emit("ply");
                stackpos -= 2;
            }
            break;
        case '-':
            assert(node->left->ty->size == 2);
            assert(node->right->ty->size == 2);
            if (node->right->kind == AST_LITERAL) {
                emit_expr(node->left);
                emit("sec");
                emit("sbc #$%0X", node->right->ival);
            } else {
                emit_expr(node->right);
                emit("pha");
                stackpos += 2;
                emit_expr(node->left);
                emit("sec");
                emit("sbc $1,S");
                emit("ply");
                stackpos -= 2;
            }
            break;
        case '*':
            assert(0);
        default:
            printf("node->kind = %u\n", node->kind);
            assert(0);
    }
}

static void emit_lsave(Type *ty, int off) {
    switch(ty->kind) {
        case KIND_BOOL:
        case KIND_CHAR:
        case KIND_SHORT:
            emit("sep #$20");
            emit(".a8");
            emit("sta $%02X,S", 1 + stackpos - off);
            emit("rep #$20");
            emit(".a16");
            break;
        case KIND_INT:
        case KIND_PTR:
            emit("sta $%02X,S", 1 + stackpos - off);
            break;
        case KIND_LONG:
            assert(0);
        case KIND_LLONG:
            assert(0);
        case KIND_FLOAT:
        case KIND_DOUBLE:
        case KIND_LDOUBLE:
            assert(0);
        default:
            assert(0);
    }
}

static void emit_gsave(const char *label, Type *ty, int off) {
    assert(ty->kind != KIND_ARRAY);
    // maybe_convert_bool(ty);
    // maybe_emit_bitshift_save(ty, addr);

    switch (ty->size) {
        case 1:
            emit("sep #$20");
            emit(".a8");
            emit("sta %s + %u", label, off);
            emit("rep #$20");
            emit(".a16");
            break;
        case 2:
            emit("sta %s + %u", label, off);
            break;
        case 4:
            emit("sta %s + %u", label, off);
            emit("stx %s + %u 2", label, off);
            break;
        default:
            assert(0);
    }
}

/* FIXME: wrong, assumes too many things, will break */
static void do_emit_assign_deref(Type *ty, int off) {
    assert((ty->size == 2) || (ty->size == 1));

    emit("pha");
    stackpos += 2;

    emit("lda $3,S");
    emit("ldy #$%04x", off);
    emit("sta ($1,S),Y");

    emit("ply");
    emit("ply");
    stackpos -= 4;
}

static void emit_assign_struct_ref(Node *struc, Type *field, int off) {
    switch(struc->kind) {
        case AST_LVAR:
            assert(0);
        case AST_GVAR:
            emit_gsave(struc->glabel, field, field->offset + off);
            break;
        case AST_STRUCT_REF:
            assert(0);
        case AST_DEREF:
            emit("pha");
            stackpos += 2;
            emit_expr(struc->operand);
            do_emit_assign_deref(field, field->offset + off);
            break;
        default:
            error("internal error %s", node2s(struc));
    }
}

static void emit_assign_deref(Node *node) {
    /* this is generally really bad */
    emit("pha");
    stackpos += 2;
    emit_expr(node->operand);
    // assert(node->operand->ty->ptr->size == 2);
    do_emit_assign_deref(node->operand->ty, 0);
}

static void emit_store(Node *node) {
    switch(node->kind) {
        case AST_DEREF:
            emit_assign_deref(node);
            break;
        case AST_STRUCT_REF:
            emit_assign_struct_ref(node->struc, node->ty, 0);
            break;
        case AST_LVAR:
			ensure_lvar_init(node);
            emit_lsave(node->ty, node->loff);
            break;
        case AST_GVAR:
            /* FIXME: wrong on so many levels */
            emit("sta a:%s", node->glabel);
            break;
        default: error("internal error");
    }
}

void emit_assign(Node *node) {
    assert(node->left->ty->kind != KIND_STRUCT);
    emit_expr(node->right);
    emit_load_convert(node->ty, node->right->ty);
    emit_store(node->left);
}

static void emit_gload(Type *ty, char *label, int off) {
    assert(ty->bitsize <= 0);

    if (ty->kind == KIND_ARRAY) {
        /* TODO: should be easy to implement */
        assert(0);
    } else if (ty->kind == KIND_FLOAT) {
        assert(0);
    } else if (ty->kind == KIND_DOUBLE || ty->kind == KIND_LDOUBLE) {
        assert(0);
    } else {
        switch (ty->size) {
            case 1:
                emit("lda %s + %u", label, off);
                emit("and $00ff");
                break;
            case 2:
                emit("lda %s + %u", label, off);
                break;
            case 4:
                emit("lda %s + %u", label, off);
                emit("ldx %s + %u 2", label, off);
                break;
            default:
                assert(0);
        }
    }
}

static void emit_gvar(Node *node) {
    emit_gload(node->ty, node->glabel, 0);
}

static void emit_func_call(Node *node) {
    int original_stackpos = stackpos;

    assert(!(node->kind == AST_FUNCPTR_CALL));

    // Vector *args = vec_reverse(node->args);

    for (size_t i = 0; i < vec_len(node->args); i++) {
        Node *v = vec_get(node->args, i);
        assert(v->ty->kind != KIND_STRUCT);
        emit_expr(v);
        assert(v->ty->size == 2);

        /* all but the last argument are passed on the stack */
        if (i + 1 < vec_len(node->args)) {
            emit("pha");
            stackpos += 2;
        }
    }

    emit("jsl %s", node->fname);

    const size_t cleanup = stackpos - original_stackpos;
    emit_stack_cleanup(cleanup);
    stackpos -= cleanup;

    assert(original_stackpos == stackpos);
    // emit_noident(";");
}

static void emit_deref(Node *node) {
    emit_expr(node->operand);
    assert(node->operand->ty->ptr->kind != KIND_ARRAY);
    assert(node->operand->ty->ptr->kind != KIND_FLOAT);
    assert(node->operand->ty->ptr->kind != KIND_DOUBLE);
    assert(node->operand->ty->ptr->kind != KIND_LDOUBLE);

    emit("pha");
    stackpos += 2;

    switch(node->operand->ty->ptr->size) {
        case 1:
            emit("ldy #$0000"); /* offset */
            emit("lda ($1,S),Y");
            emit("and #$00FF");
            break;
        case 2:
            emit("ldy #$0000"); /* offset */
            emit("lda ($1,S),Y");
            break;
        case 3:
            emit("ldy #$0001"); /* offset */
            emit("lda ($1,S),Y");
            emit("tax");
            emit("dey");
            emit("lda ($1,S),Y");
            break;
    }

    emit("ply");
    stackpos -= 2;

    emit_load_convert(node->ty, node->operand->ty->ptr);
}

static void emit_pointer_arith(char kind, Node *left, Node *right) {
    emit_expr(left);
    emit("pha");
    stackpos += 2;
    emit_expr(right);

    int size = left->ty->ptr->size;
    if (size > 1) {
        assert(size % 2 == 0);
        for (size_t i = 0; i < size; i += 2) {
            emit("asl");
        }
    }

    /* 16-bit arithmatic */
    if (kind == '+') {
        emit("clc");
        emit("adc $1,S");
    } else if (kind == '-') {
        emit("sec");
        emit("sbc $1,S");
    }

    emit("ply");
    stackpos -= 2;
}

/* ++X / --X */
/* FIXME: assumes size = 2 */
static void emit_pre_op(Node *node, char op) {
    assert((op == '+') || (op == '-'));
    emit_expr(node->operand);
    if (node->ty->ptr != NULL) {
        if (node->ty->ptr->size == 1) {
            if (op == '+') {
                emit("inc");
            } else if (op == '-') {
                emit("dec");
            }
        } else {
            assert(0);
        }
    } else {
        if (op == '+') {
            emit("inc");
        } else if (op == '-') {
            emit("dec");
        }
    }
    emit_store(node->operand);
}

/* X++ / X-- */
/* FIXME: assumes size = 2 */
static void emit_post_op(Node *node, char op) {
    emit_expr(node->operand);
    emit("pha");
    stackpos += 2;
    if (node->ty->ptr != NULL) {
        if (node->ty->ptr->size == 1) {
            if (op == '+') {
                emit("inc");
            } else if (op == '-') {
                emit("dec");
            }
        } else {
            assert(0);
        }
    } else {
        if (op == '+') {
            emit("inc");
        } else if (op == '-') {
            emit("dec");
        }
    }
    emit_store(node->operand);
    emit("pla");
    stackpos -= 2;
}

static void emit_ternary(Node *node) {
    emit_expr(node->cond);
    const char *ne = make_label();
    emit("cmp #$0000");
    emit("beq %s", ne);

    if (node->then) {
        emit_expr(node->then);
    }

    if (node->els) {
        const char *end = make_label();
        emit("bra %s", end);
        emit_label(ne);
        emit_expr(node->els);
        emit_label(end);
    } else {
        emit_label(ne);
    }
}

/* FIXME: this is inefficient ... */
static void emit_cmp_eq(Node *node) {
    assert(node->left->ty->size == 2);
    assert(node->right->ty->size == 2);
    emit_expr(node->left);
    emit("pha");
    stackpos += 2;
    emit_expr(node->right);
    emit("sec");
    emit("sbc $1,S");

    const char *cmp_true = make_label();
    const char *cmp_cont = make_label();
    emit("beq %s", cmp_true);
    emit("lda #$0000"); /* false */
    emit("bra %s", cmp_cont);
    emit_noident("%s:", cmp_true);
    emit("lda #$0001"); /* true */
    emit_noident("%s:", cmp_cont);

    emit("ply");
    stackpos -= 2;
}

static void emit_load_struct_ref(Node *struc, Type *field, int off) {
    switch (struc->kind) {
        case AST_LVAR:
            assert(0);
        case AST_GVAR:
            emit_gload(field, struc->glabel, field->offset + off);
            break;
        case AST_STRUCT_REF:
            assert(0);
        case AST_DEREF:
            assert(0);
        default:
            error("internal error %s", node2s(struc));
    }
}

static void emit_addr(Node *node) {
    switch (node->kind) {
        case AST_LVAR:
            ensure_lvar_init(node);
            emit("tsc");
            /* see emit_lload: lda [1 + stackpos - node->loff],S */
            emit("clc");
            emit("adc #$%04x", (1 + stackpos - node->loff));
            break;
        case AST_GVAR:
            emit("lda #%s", node->glabel);
            break;
        case AST_DEREF:
            emit_expr(node->operand);
            break;
        case AST_STRUCT_REF:
            emit_addr(node->struc);
            emit("clc");
            emit("adc #$%04x", node->ty->offset);
            break;
        case AST_FUNCDESG:
            emit("lda #%s", node->fname);
            break;
        default:
            error("internal error: %s", node2s(node));
    }
}

void emit_expr(Node *node) {
    switch (node->kind) {
        case AST_LITERAL:
            emit_literal(node);
            break;
        case AST_LVAR:
            emit_lvar(node);
            break;
        case AST_GVAR:
            emit_gvar(node);
            break;
        case AST_FUNCDESG:
            emit_addr(node);
            break;
        case AST_FUNCALL:
            emit_func_call(node);
            break;
        case AST_FUNCPTR_CALL:
            assert(0);
        case AST_DECL:
            emit_decl(node);
            break;
        case AST_CONV:
            emit_conv(node);
            break;
        case AST_ADDR:
            emit_addr(node->operand);
            break;
        case AST_DEREF:
            emit_deref(node);
            break;
        case AST_IF:
        case AST_TERNARY:
            emit_ternary(node);
            break;
        case AST_GOTO:
            assert(node->newlabel);
            emit("jmp f:%s", node->newlabel);
            break;
        case AST_LABEL:
            if (node->newlabel) {
                emit_label(node->newlabel);
            }
            break;
        case AST_RETURN:
            emit_return(node);
            break;
        case AST_COMPOUND_STMT:
            for (size_t i = 0; i < vec_len(node->stmts); i++) {
                emit_expr(vec_get(node->stmts, i));
            }
            break;
        case AST_STRUCT_REF:
            emit_load_struct_ref(node->struc, node->ty, 0);
            break;
        case OP_PRE_INC:
            emit_pre_op(node, '+');
            break;
        case OP_PRE_DEC:
            emit_pre_op(node, '-');
            break;
        case OP_POST_INC:
            emit_post_op(node, '+');
            break;
        case OP_POST_DEC:
            emit_post_op(node, '-');
            break;
        case '!':
            assert(0);
        case '&':
            assert(0);
        case '|':
            assert(0);
        case '~':
            assert(0);
        case OP_LOGAND:
            assert(0);
        case OP_LOGOR:
            assert(0);
        case OP_CAST:
            emit_expr(node->operand);
            emit_load_convert(node->ty, node->operand->ty);
            break;
        case ',':
            assert(0);
        case '=':
            emit_assign(node);
            break;
        case OP_LABEL_ADDR:
            assert(0);
        default:
            if (node->ty->kind == KIND_PTR) {
                emit_pointer_arith(node->kind, node->left, node->right);
                break;
            } else if (node->kind == '<') {
                assert(0);
            } else if (node->kind == OP_EQ) {
                emit_cmp_eq(node);
                break;
            } else if (node->kind == OP_LE) {
                assert(0);
            } else if (node->kind == OP_NE) {
                assert(0);
            }
            if (is_inttype(node->ty)) {
                emit_binop_int(node);
                break;
            }

            printf("node->kind = %u\n", node->kind);
            assert(0);
    }
}

void emit_func(Node *func) {
    /* function prologue */
    emit_noident("; function!");
    emit_text_segment();
    if (!func->ty->isstatic) {
        emit_noident(".global %s", func->fname);
    }
    emit_noident("%s:", func->fname);

    // stackpos = 2; /* we already have the return address on stack */

    assert(!func->ty->hasva);

    stackpos = 0;

    {
        /* assign offset to arguments */

        /* return address for rtl */
        size_t off = 3;

        if (vec_len(func->params) > 0) {
            printf("vec_len(...) = %u\n", vec_len(func->params));
            if (vec_len(func->params) > 1) {
                for (size_t i = vec_len(func->params) - 1; i > 0; i--) {
                    Node *v = vec_get(func->params, i - 1);
                    const size_t size = v->ty->size;
                    v->loff = -off;
                    off += size <= 2 ? 2 : size;
                }
            }

            /* last argument in A */
            Node *v = vec_get(func->params, vec_len(func->params) - 1);

            printf("v->ty->size = %u\n", v->ty->size);
            assert(v->loff == 0);
            if (v->ty->size <= 2 ) {
                emit("pha");
                stackpos += 2;
                v->loff = 2;
            } else if (v->ty->size == 4) {
                emit("pha");
                emit("phx");
                stackpos += 4;
                v->loff = 4;
            } else {
                assert(0);
            }
        }
    }

    {
        /* local variables */
        size_t localarea = 0;
        for (size_t i = 0; i < vec_len(func->localvars); i++) {
            Node *v = vec_get(func->localvars, i);
            const size_t size = v->ty->size;
            localarea += size;
            v->loff = localarea;
            emit_noident("; local offset = %#x\n", v->loff);
        }

        if (localarea % 2 != 0) {
            emit("phb");
            stackpos += 1;
        }

        for (size_t i = 0; i < localarea / 2; i++) {
            emit("phx");
            stackpos += 2;
        }
    }

    /* function body */
    emit_expr(func->body);

    /* function epilog */
    emit_ret();
}

static void emit_zero(size_t size) {
    for (; size >= 1; size--) { emit(".byte $00"); }
}

static void emit_data_addr(Node *operand, int depth) {
    assert(0);
}

static void emit_label(const char *s) {
    emit_noident("%s:", s);
}

static void emit_data_charptr(const char *s, int depth) {
    assert(depth == 0);
    const char *label = make_label();
    emit_noident(".addr %s", label);
    emit_data_segment();
    emit_label(label);
    emit_noident(".byte \"%s\"", quote_cstring(s));
    emit_noident(".byte $00");
}

static void emit_data_primtype(Type *ty, Node *val, int depth) {
    switch(ty->kind) {
        case KIND_BOOL:
            emit(".byte %d", !!eval_intexpr(val, NULL));
            break;
        case KIND_CHAR:
            emit(".byte %d", eval_intexpr(val, NULL));
            break;
        case KIND_INT:
            emit(".word $%04X", eval_intexpr(val, NULL));
            break;
        case KIND_PTR:
            if (val->kind == OP_LABEL_ADDR) {
                emit_noident(".addr %s", val->newlabel);
            }
            bool is_char_ptr = (val->operand->ty->kind == KIND_ARRAY && val->operand->ty->ptr->kind == KIND_CHAR);
            if (is_char_ptr) {
                emit_data_charptr(val->operand->sval, depth);
            } else if (val->kind == AST_GVAR) {
                emit_noident(".addr %s", val->glabel);
            } else {
                Node *base = NULL;
                int v = eval_intexpr(val, &base);
                if (base == NULL) {
                    emit_noident(".word $%04x", v);
                    break;
                }
                assert(0);
            }
            break;
        default:
            assert(0);
    }
}

static void do_emit_data(Vector *inits, int size, int off, int depth) {
    for (int i = 0; i < vec_len(inits) && 0 < size; i++) {
        Node *node = vec_get(inits, i);
        Node *v = node->initval;
        /* emit_padding(node, off); */
        if (node->totype->bitsize > 0) {
            assert(0);
        } else {
            off += node->totype->size;
            size -= node->totype->size;
        }

        if (v->kind == AST_ADDR) {
            emit_data_addr(v->operand, depth);
            continue;
        }

        if (v->kind == AST_LVAR && v->lvarinit) {
            do_emit_data(v->lvarinit, v->ty->size, 0, depth);
            continue;
        }

        emit_data_primtype(node->totype, node->initval, depth);
    }

    emit_zero(size);
}

static void emit_global_var(Node *v) {
    emit_noident("; global variable");
    if (v->declinit) {
        /* .data */
        emit_data_segment();
        if (!v->declvar->ty->isstatic) {
            emit_noident(".global %s : abs", v->declvar->glabel);
        }
        emit_noident("%s:", v->declvar->glabel);
        do_emit_data(v->declinit, v->declvar->ty->size, 0, 0);
    } else {
        /* .bss */
        emit_bss_segment();
        if (!v->declvar->ty->isstatic) {
            emit_noident(".global %s : abs", v->declvar->glabel);
        }
        emit_noident("%s:", v->declvar->glabel);
        emit_noident(".res %u", v->declvar->ty->size);
    }
}

static void emit_global_decl(Node *v) {
    if (v->declvar->ty->isextern) {
        emit_noident(".import %s", v->declvar->glabel);
    } else {
        if (!v->declvar->ty->isstatic) {
            emit_noident(".global %s:far", v->declvar->glabel);
        }
        emit_global_var(v);
    }
}

static bool first = true;
void emit_toplevel(Node *v) {
    if (first) {
        emit_noident("; 8cc : ca65 assembly output");
        emit_noident(".feature string_escapes");
        emit_noident(".setcpu \"65816\"");
        emit_noident(".A16");
        emit_noident(".I16");
        emit_noident(".P816");
    }

    first = false;
    if (v->kind == AST_FUNC) {
        emit_func(v);
    } else if (v->kind == AST_DECL) {
        emit_global_decl(v);
    } else {
        error("internal error");
    }

    emit_noident("");
}
