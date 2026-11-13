#include "transpiler/transpiler.h"
#include "lexer/lexer.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

enum class Type { Int, Str, Bool };
enum class Kind { Num, Text, Var, Lit, Neg, Bin, Cmp, And, Or, Not };

struct Expr {
    Kind kind;
    Type type;
    std::string text;
    int lhs;
    int rhs;
};

struct Piece {
    bool var;
    std::string text;
};

struct Error {
    uint32_t line;
    std::string msg;
};

struct Backend;

struct Transpiler {
    const Backend *be;
    Lexer lex;
    Token *tok;
    std::vector<Expr> nodes;
    std::map<std::string, Type> vars;
    std::vector<int> loops;
    std::string out;
    int depth;
    int temps;
    int labels;
};

struct Backend {
    const char *ext;
    const char *header;
    const char *eol;
    void (*assign)(Transpiler &t, const std::string &name, int e);
    void (*print)(Transpiler &t, const std::vector<int> &args);
    void (*input)(Transpiler &t, const std::string &name, const std::string &prompt);
    void (*open_if)(Transpiler &t, int cond, int id);
    void (*open_elif)(Transpiler &t, int cond, int id, int k);
    void (*open_else)(Transpiler &t, int id, int k);
    void (*close_if)(Transpiler &t, int id, int k);
    void (*open_while)(Transpiler &t, int cond, int id);
    void (*close_while)(Transpiler &t, int id);
    void (*jump)(Transpiler &t, bool brk, int id);
    void (*pass)(Transpiler &t);
};

static const char *type_name(Type type)
{
    switch (type) {
    case Type::Int:
        return "int";
    case Type::Str:
        return "str";
    default:
        return "bool";
    }
}

static void next(Transpiler &t)
{
    lexer_free_token(t.tok);
    t.tok = lexer_next_token(&t.lex);
}

static void skip(Transpiler &t, int n)
{
    while (n-- > 0)
        next(t);
}

static void line(Transpiler &t, const std::string &s)
{
    t.out.append(t.depth * 4, ' ');
    t.out += s;
    t.out += t.be->eol;
}

static int add(Transpiler &t, Kind kind, Type type, const std::string &text, int lhs = -1, int rhs = -1)
{
    t.nodes.push_back({kind, type, text, lhs, rhs});
    return (int)t.nodes.size() - 1;
}

static int lit(Transpiler &t, bool v)
{
    return add(t, Kind::Lit, Type::Bool, v ? "True" : "False");
}

static bool is_lit(const Transpiler &t, int e, bool v)
{
    const Expr &x = t.nodes[e];
    return x.kind == Kind::Lit && (x.text == "True") == v;
}

static int to_bool(Transpiler &t, int e)
{
    Type type = t.nodes[e].type;
    if (type == Type::Bool)
        return e;

    // 0 and "" are falsy
    int zero = type == Type::Int
        ? add(t, Kind::Num, Type::Int, "0")
        : add(t, Kind::Text, Type::Str, "");
    return add(t, Kind::Cmp, Type::Bool, "!=", e, zero);
}

static int negate(Transpiler &t, int e)
{
    e = to_bool(t, e);
    if (t.nodes[e].kind == Kind::Lit)
        return lit(t, t.nodes[e].text != "True");
    return add(t, Kind::Not, Type::Bool, "not", e);
}

static int logic(Transpiler &t, Kind kind, int l, int r)
{
    l = to_bool(t, l);
    r = to_bool(t, r);

    // x and True -> x, x or True -> True
    bool absorb = kind == Kind::Or;
    if (is_lit(t, l, absorb) || is_lit(t, r, absorb))
        return lit(t, absorb);
    if (is_lit(t, l, !absorb))
        return r;
    if (is_lit(t, r, !absorb))
        return l;

    return add(t, kind, Type::Bool, kind == Kind::And ? "and" : "or", l, r);
}

static int compare(Transpiler &t, const std::string &op, int l, int r, uint32_t ln)
{
    Type a = t.nodes[l].type;
    Type b = t.nodes[r].type;

    if (a != b || a == Type::Bool) {
        throw Error{ln, "'" + op + "' not supported between instances of '"
            + type_name(a) + "' and '" + type_name(b) + "'"};
    }

    if (a == Type::Str && op != "==" && op != "!=")
        throw Error{ln, "pish can only compare strings with == and !="};

    return add(t, Kind::Cmp, Type::Bool, op, l, r);
}

static int math(Transpiler &t, const std::string &op, int l, int r, uint32_t ln)
{
    Type a = t.nodes[l].type;
    Type b = t.nodes[r].type;

    if (op == "+" && a == Type::Str && b == Type::Str)
        return add(t, Kind::Bin, Type::Str, op, l, r);

    if (a != Type::Int || b != Type::Int) {
        throw Error{ln, "unsupported operand type(s) for " + op + ": '"
            + type_name(a) + "' and '" + type_name(b) + "'"};
    }

    return add(t, Kind::Bin, Type::Int, op, l, r);
}

static int expr(Transpiler &t, int level);

static int atom(Transpiler &t)
{
    uint32_t ln = t.tok->line;
    int e;

    switch (t.tok->type) {
    case TOKEN_MINUS:
        next(t);
        e = atom(t);
        if (t.nodes[e].type != Type::Int)
            throw Error{ln, std::string("bad operand type for unary -: '") + type_name(t.nodes[e].type) + "'"};
        return add(t, Kind::Neg, Type::Int, "-", e);
    case TOKEN_LPAREN:
        next(t);
        e = expr(t, 0);
        next(t);
        return e;
    case TOKEN_TRUE:
    case TOKEN_FALSE:
        e = lit(t, t.tok->type == TOKEN_TRUE);
        break;
    case TOKEN_INTEGER_LITERAL:
        e = add(t, Kind::Num, Type::Int, t.tok->value);
        break;
    case TOKEN_STRING_LITERAL:
        e = add(t, Kind::Text, Type::Str, t.tok->value);
        break;
    default: {
        auto it = t.vars.find(t.tok->value);
        if (it == t.vars.end())
            throw Error{ln, "name '" + std::string(t.tok->value) + "' is not defined"};
        e = add(t, Kind::Var, it->second, t.tok->value);
    }
    }

    next(t);
    return e;
}

static bool is_op(TokenType type, int level)
{
    switch (level) {
    case 0:
        return type == TOKEN_OR;
    case 1:
        return type == TOKEN_AND;
    case 3:
        return type == TOKEN_EQ || type == TOKEN_NE || type == TOKEN_LT
            || type == TOKEN_LE || type == TOKEN_GT || type == TOKEN_GE;
    case 4:
        return type == TOKEN_PLUS || type == TOKEN_MINUS;
    case 5:
        return type == TOKEN_STAR || type == TOKEN_FLOORDIV || type == TOKEN_PERCENT;
    }
    return false;
}

// same levels as the parser: or, and, not, comparison, + -, * // %, atom
static int expr(Transpiler &t, int level)
{
    if (level == 6)
        return atom(t);

    if (level == 2 && t.tok->type == TOKEN_NOT) {
        next(t);
        return negate(t, expr(t, 2));
    }

    int lhs = expr(t, level + 1);

    while (is_op(t.tok->type, level)) {
        std::string op = t.tok->value;
        uint32_t ln = t.tok->line;
        next(t);

        int rhs = expr(t, level + 1);

        if (level <= 1)
            lhs = logic(t, level == 0 ? Kind::Or : Kind::And, lhs, rhs);
        else if (level == 3)
            lhs = compare(t, op, lhs, rhs, ln);
        else
            lhs = math(t, op, lhs, rhs, ln);
    }

    return lhs;
}

static int value(Transpiler &t)
{
    uint32_t ln = t.tok->line;
    int e = expr(t, 0);

    if (t.nodes[e].type == Type::Bool)
        throw Error{ln, "comparisons and True/False only work in if and while for now"};

    return e;
}

static int condition(Transpiler &t)
{
    int c = to_bool(t, expr(t, 0));
    next(t);
    return c;
}

static int prec(const std::string &op)
{
    return op == "+" || op == "-" ? 0 : 1;
}

static std::string arith(const Transpiler &t, int i)
{
    const Expr &e = t.nodes[i];

    if (e.kind == Kind::Num || e.kind == Kind::Var)
        return e.text;

    if (e.kind == Kind::Neg) {
        Kind sub = t.nodes[e.lhs].kind;
        std::string s = arith(t, e.lhs);
        return sub == Kind::Bin || sub == Kind::Neg ? "-(" + s + ")" : "-" + s;
    }

    const Expr &l = t.nodes[e.lhs];
    const Expr &r = t.nodes[e.rhs];
    std::string ls = arith(t, e.lhs);
    std::string rs = arith(t, e.rhs);

    if (l.kind == Kind::Bin && prec(l.text) < prec(e.text))
        ls = "(" + ls + ")";
    if (r.kind == Kind::Bin && prec(r.text) <= prec(e.text))
        rs = "(" + rs + ")";

    return ls + " " + (e.text == "//" ? "/" : e.text) + " " + rs;
}

static void pieces(const Transpiler &t, int i, std::vector<Piece> &out)
{
    const Expr &e = t.nodes[i];

    if (e.kind == Kind::Bin) {
        pieces(t, e.lhs, out);
        pieces(t, e.rhs, out);
        return;
    }

    out.push_back({e.kind == Kind::Var, e.text});
}

static std::string bash_quote(const std::string &text)
{
    std::string out = "'";

    for (char c : text) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }

    return out + "'";
}

static std::string bash_word(const Transpiler &t, int i)
{
    const Expr &e = t.nodes[i];

    if (e.type == Type::Int) {
        if (e.kind == Kind::Num)
            return e.text;
        if (e.kind == Kind::Var)
            return "\"$" + e.text + "\"";
        return "\"$((" + arith(t, i) + "))\"";
    }

    std::vector<Piece> ps;
    pieces(t, i, ps);

    std::string out;
    for (const Piece &p : ps)
        out += p.var ? "\"$" + p.text + "\"" : bash_quote(p.text);

    return out;
}

static const std::map<std::string, std::string> bash_int_ops = {
    {"==", "-eq"}, {"!=", "-ne"}, {"<", "-lt"}, {"<=", "-le"}, {">", "-gt"}, {">=", "-ge"},
};

static std::string bash_test(const Transpiler &t, int i)
{
    const Expr &e = t.nodes[i];

    if (e.kind == Kind::Cmp) {
        bool str = t.nodes[e.lhs].type == Type::Str;
        std::string op = str ? e.text : bash_int_ops.at(e.text);
        return bash_word(t, e.lhs) + " " + op + " " + bash_word(t, e.rhs);
    }

    if (e.kind == Kind::Not) {
        std::string s = bash_test(t, e.lhs);
        return t.nodes[e.lhs].kind == Kind::Cmp ? "! " + s : "! (" + s + ")";
    }

    std::string l = bash_test(t, e.lhs);
    std::string r = bash_test(t, e.rhs);

    if (e.kind == Kind::Or)
        return l + " || " + r;

    if (t.nodes[e.lhs].kind == Kind::Or)
        l = "(" + l + ")";
    if (t.nodes[e.rhs].kind == Kind::Or)
        r = "(" + r + ")";

    return l + " && " + r;
}

static std::string bash_cond(const Transpiler &t, int i)
{
    const Expr &e = t.nodes[i];

    if (e.kind == Kind::Lit)
        return e.text == "True" ? "true" : "false";

    return "[[ " + bash_test(t, i) + " ]]";
}

static void bash_assign(Transpiler &t, const std::string &name, int e)
{
    line(t, name + "=" + bash_word(t, e));
}

static void bash_print(Transpiler &t, const std::vector<int> &args)
{
    if (args.empty()) {
        line(t, "echo");
        return;
    }

    std::string fmt;
    std::string words;

    for (int a : args) {
        fmt += fmt.empty() ? "%s" : " %s";
        words += " " + bash_word(t, a);
    }

    line(t, "printf '" + fmt + "\\n'" + words);
}

static void bash_input(Transpiler &t, const std::string &name, const std::string &prompt)
{
    if (!prompt.empty())
        line(t, "printf '%s' " + bash_quote(prompt));

    // python stops with EOFError when input runs out, a bare read would loop forever
    line(t, "IFS= read -r " + name + " || [[ -n $" + name + " ]] || exit 1");
}

static void bash_open_if(Transpiler &t, int cond, int)
{
    line(t, "if " + bash_cond(t, cond) + "; then");
}

static void bash_open_elif(Transpiler &t, int cond, int, int)
{
    line(t, "elif " + bash_cond(t, cond) + "; then");
}

static void bash_open_else(Transpiler &t, int, int)
{
    line(t, "else");
}

static void bash_close_if(Transpiler &t, int, int)
{
    line(t, "fi");
}

static void bash_open_while(Transpiler &t, int cond, int)
{
    line(t, "while " + bash_cond(t, cond) + "; do");
}

static void bash_close_while(Transpiler &t, int)
{
    line(t, "done");
}

static void bash_jump(Transpiler &t, bool brk, int)
{
    line(t, brk ? "break" : "continue");
}

static void bash_pass(Transpiler &t)
{
    line(t, ":");
}

static std::string batch_escape(const std::string &text, bool &quoted, bool bang)
{
    std::string out;

    for (char c : text) {
        // cmd eats one caret outside quotes, and delayed expansion eats another on lines with a !
        std::string esc = quoted ? "" : "^";

        switch (c) {
        case '"':
            quoted = !quoted;
            out += c;
            break;
        case '%':
            out += "%%";
            break;
        case '!':
            out += esc + "^!";
            break;
        case '^':
            out += bang ? esc + esc + "^^" : esc + "^";
            break;
        case '&':
        case '|':
        case '<':
        case '>':
            out += esc + c;
            break;
        default:
            out += c;
        }
    }

    return out;
}

static bool has_bang(const std::vector<Piece> &ps)
{
    for (const Piece &p : ps) {
        if (p.var || p.text.find('!') != std::string::npos)
            return true;
    }
    return false;
}

static std::string batch_render(const std::vector<Piece> &ps, bool quoted, bool bang)
{
    std::string out;

    for (const Piece &p : ps)
        out += p.var ? "!" + p.text + "!" : batch_escape(p.text, quoted, bang);

    return out;
}

static std::string batch_arith(const Transpiler &t, int i)
{
    std::string out;

    for (char c : arith(t, i)) {
        if (c == '%')
            out += "%%";
        else
            out += c;
    }

    return out;
}

static std::vector<Piece> batch_operand(Transpiler &t, int i)
{
    const Expr &e = t.nodes[i];
    std::vector<Piece> ps;

    if (e.type == Type::Str) {
        pieces(t, i, ps);
    } else if (e.kind == Kind::Num || e.kind == Kind::Var) {
        ps.push_back({e.kind == Kind::Var, e.text});
    } else {
        std::string tmp = "_pish" + std::to_string(++t.temps);
        line(t, "set /a \"" + tmp + "=" + batch_arith(t, i) + "\"");
        ps.push_back({true, tmp});
    }

    return ps;
}

static void batch_assign(Transpiler &t, const std::string &name, int e)
{
    const Expr &x = t.nodes[e];

    if (x.type == Type::Str) {
        std::vector<Piece> ps;
        pieces(t, e, ps);
        line(t, "set \"" + name + "=" + batch_render(ps, true, has_bang(ps)) + "\"");
    } else if (x.kind == Kind::Num) {
        line(t, "set \"" + name + "=" + x.text + "\"");
    } else {
        line(t, "set /a \"" + name + "=" + batch_arith(t, e) + "\"");
    }
}

static void batch_print(Transpiler &t, const std::vector<int> &args)
{
    std::vector<Piece> ps;

    for (size_t k = 0; k < args.size(); k++) {
        if (k > 0)
            ps.push_back({false, " "});

        std::vector<Piece> arg = batch_operand(t, args[k]);
        ps.insert(ps.end(), arg.begin(), arg.end());
    }

    line(t, "echo(" + batch_render(ps, false, has_bang(ps)));
}

static void batch_input(Transpiler &t, const std::string &name, const std::string &prompt)
{
    std::vector<Piece> ps = {{false, prompt}};

    // set /p keeps the old value on an empty answer, python gives ""
    line(t, "set \"" + name + "=\"");
    line(t, "set /p \"" + name + "=" + batch_render(ps, true, has_bang(ps)) + "\"");
}

static const std::map<std::string, std::string> batch_int_ops = {
    {"==", "EQU"}, {"!=", "NEQ"}, {"<", "LSS"}, {"<=", "LEQ"}, {">", "GTR"}, {">=", "GEQ"},
};

static void batch_compare(Transpiler &t, int i, bool when, const std::string &label)
{
    const Expr &e = t.nodes[i];
    std::vector<Piece> l = batch_operand(t, e.lhs);
    std::vector<Piece> r = batch_operand(t, e.rhs);
    bool bang = has_bang(l) || has_bang(r);
    bool flip = !when;
    std::string test;

    if (t.nodes[e.lhs].type == Type::Str) {
        test = "\"" + batch_render(l, true, bang) + "\"==\"" + batch_render(r, true, bang) + "\"";
        if (e.text == "!=")
            flip = !flip;
    } else {
        test = batch_render(l, false, bang) + " " + batch_int_ops.at(e.text) + " " + batch_render(r, false, bang);
    }

    line(t, std::string("if ") + (flip ? "not " : "") + test + " goto " + label);
}

static void batch_jump_if(Transpiler &t, int i, bool when, const std::string &label)
{
    const Expr &e = t.nodes[i];

    if (e.kind == Kind::Lit) {
        if ((e.text == "True") == when)
            line(t, "goto " + label);
        return;
    }

    if (e.kind == Kind::Not) {
        batch_jump_if(t, e.lhs, !when, label);
        return;
    }

    if (e.kind == Kind::Cmp) {
        batch_compare(t, i, when, label);
        return;
    }

    // a false side decides an and, a true side decides an or
    bool decides = e.kind == Kind::Or;
    if (when == decides) {
        batch_jump_if(t, e.lhs, when, label);
        batch_jump_if(t, e.rhs, when, label);
        return;
    }

    std::string skip_label = "pish_skip" + std::to_string(++t.labels);
    batch_jump_if(t, e.lhs, !when, skip_label);
    batch_jump_if(t, e.rhs, when, label);
    line(t, ":" + skip_label);
}

static std::string if_label(int id, int k)
{
    std::string s = "pish_if" + std::to_string(id) + "_";
    return k == 0 ? s + "end" : s + std::to_string(k);
}

static std::string loop_label(int id, bool end)
{
    return "pish_loop" + std::to_string(id) + (end ? "_end" : "");
}

static void batch_open_if(Transpiler &t, int cond, int id)
{
    batch_jump_if(t, cond, false, if_label(id, 1));
}

static void batch_open_elif(Transpiler &t, int cond, int id, int k)
{
    line(t, "goto " + if_label(id, 0));
    line(t, ":" + if_label(id, k));
    batch_jump_if(t, cond, false, if_label(id, k + 1));
}

static void batch_open_else(Transpiler &t, int id, int k)
{
    line(t, "goto " + if_label(id, 0));
    line(t, ":" + if_label(id, k));
}

static void batch_close_if(Transpiler &t, int id, int k)
{
    if (k > 0)
        line(t, ":" + if_label(id, k));
    line(t, ":" + if_label(id, 0));
}

static void batch_open_while(Transpiler &t, int cond, int id)
{
    line(t, ":" + loop_label(id, false));
    batch_jump_if(t, cond, false, loop_label(id, true));
}

static void batch_close_while(Transpiler &t, int id)
{
    line(t, "goto " + loop_label(id, false));
    line(t, ":" + loop_label(id, true));
}

static void batch_jump(Transpiler &t, bool brk, int id)
{
    line(t, "goto " + loop_label(id, brk));
}

static void batch_pass(Transpiler &)
{
}

static const Backend bash = {
    ".sh", "#!/bin/bash\n", "\n",
    bash_assign, bash_print, bash_input,
    bash_open_if, bash_open_elif, bash_open_else, bash_close_if,
    bash_open_while, bash_close_while, bash_jump, bash_pass,
};

static const Backend batch = {
    ".bat", "@echo off\r\nsetlocal EnableDelayedExpansion\r\n", "\r\n",
    batch_assign, batch_print, batch_input,
    batch_open_if, batch_open_elif, batch_open_else, batch_close_if,
    batch_open_while, batch_close_while, batch_jump, batch_pass,
};

static void block(Transpiler &t, int indent);

static void body(Transpiler &t, int indent, uint32_t ln)
{
    while (t.tok->type == TOKEN_SEMICOLON)
        next(t);

    if (t.tok->type == TOKEN_EOF || (int)t.tok->column <= indent)
        throw Error{ln, "expected an indented block"};

    t.depth++;
    block(t, t.tok->column);
    t.depth--;
}

static void print_stmt(Transpiler &t)
{
    skip(t, 2);

    std::vector<int> args;
    while (t.tok->type != TOKEN_RPAREN) {
        args.push_back(value(t));
        if (t.tok->type == TOKEN_COMMA)
            next(t);
    }
    next(t);

    t.be->print(t, args);
}

static void assign_stmt(Transpiler &t)
{
    std::string name = t.tok->value;
    skip(t, 2);

    if (t.tok->type == TOKEN_INPUT) {
        skip(t, 2);

        std::string prompt;
        if (t.tok->type == TOKEN_STRING_LITERAL) {
            prompt = t.tok->value;
            next(t);
        }
        next(t);

        t.vars[name] = Type::Str;
        t.be->input(t, name, prompt);
        return;
    }

    int e = value(t);
    t.vars[name] = t.nodes[e].type;
    t.be->assign(t, name, e);
}

static void if_stmt(Transpiler &t, int indent)
{
    uint32_t ln = t.tok->line;
    int id = ++t.labels;
    int k = 1;

    next(t);
    t.be->open_if(t, condition(t), id);
    body(t, indent, ln);

    while (t.tok->type == TOKEN_ELIF && (int)t.tok->column == indent) {
        ln = t.tok->line;
        next(t);
        t.be->open_elif(t, condition(t), id, k++);
        body(t, indent, ln);
    }

    if (t.tok->type == TOKEN_ELSE && (int)t.tok->column == indent) {
        ln = t.tok->line;
        skip(t, 2);
        t.be->open_else(t, id, k);
        k = 0;
        body(t, indent, ln);
    }

    t.be->close_if(t, id, k);
}

static void while_stmt(Transpiler &t, int indent)
{
    uint32_t ln = t.tok->line;
    int id = ++t.labels;

    next(t);
    t.be->open_while(t, condition(t), id);

    t.loops.push_back(id);
    body(t, indent, ln);
    t.loops.pop_back();

    t.be->close_while(t, id);
}

static void jump_stmt(Transpiler &t)
{
    bool brk = t.tok->type == TOKEN_BREAK;

    if (t.loops.empty())
        throw Error{t.tok->line, "'" + std::string(t.tok->value) + "' outside loop"};

    next(t);
    t.be->jump(t, brk, t.loops.back());
}

static void statement(Transpiler &t, int indent)
{
    // parser already checked the syntax
    switch (t.tok->type) {
    case TOKEN_PRINT:
        print_stmt(t);
        break;
    case TOKEN_IF:
        if_stmt(t, indent);
        break;
    case TOKEN_WHILE:
        while_stmt(t, indent);
        break;
    case TOKEN_BREAK:
    case TOKEN_CONTINUE:
        jump_stmt(t);
        break;
    case TOKEN_PASS:
        next(t);
        t.be->pass(t);
        break;
    case TOKEN_ELIF:
    case TOKEN_ELSE:
        throw Error{t.tok->line, "'" + std::string(t.tok->value) + "' without 'if'"};
    default:
        assign_stmt(t);
    }
}

static void block(Transpiler &t, int indent)
{
    for (;;) {
        while (t.tok->type == TOKEN_SEMICOLON)
            next(t);

        if (t.tok->type == TOKEN_EOF || (int)t.tok->column < indent)
            return;
        if ((int)t.tok->column > indent)
            throw Error{t.tok->line, "unexpected indent"};

        statement(t, indent);
    }
}

static std::string out_path(const std::string &input, const char *ext)
{
    size_t slash = input.find_last_of("/\\");
    size_t dot = input.find_last_of('.');

    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return input + ext;

    return input.substr(0, dot) + ext;
}

static int transpile(const char *code, const char *filename, const Backend &be)
{
    Transpiler t{};
    t.be = &be;
    t.out = be.header;
    lexer_init(&t.lex, code);
    t.tok = lexer_next_token(&t.lex);

    try {
        block(t, 0);
    } catch (const Error &err) {
        std::printf("\033[31mError on line %u: %s.\033[0m\n", err.line, err.msg.c_str());
        lexer_free_token(t.tok);
        return 1;
    }
    lexer_free_token(t.tok);

    std::string path = out_path(filename, be.ext);

    // wb or windows turns \n into \r\n
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::perror(path.c_str());
        return 1;
    }

    std::fwrite(t.out.data(), 1, t.out.size(), f);
    std::fclose(f);
    return 0;
}

extern "C" int transpileToBash(const char *code, const char *filename)
{
    return transpile(code, filename, bash);
}

extern "C" int transpileToBatch(const char *code, const char *filename)
{
    return transpile(code, filename, batch);
}
