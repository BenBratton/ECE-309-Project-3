// src/tools/calculator_eval.cpp — two-stack evaluation (spec §4.6).
//
// Iterative only (no recursion), so 5,000 nested parentheses or 20,000 terms
// cannot overflow the call stack.

#include "tools/calculator.h"

#include <stack>

namespace {

using Kind = CalcToken::Kind;

bool is_operator(Kind k) {
    return k == Kind::Plus || k == Kind::Minus || k == Kind::Times || k == Kind::Divide;
}

int precedence(Kind k) {
    return (k == Kind::Times || k == Kind::Divide) ? 2 : 1;
}

// Pop one operator and two values, compute left op right, push the result.
bool apply_top(std::stack<double>& values, std::stack<Kind>& ops, std::string* error) {
    if (ops.empty() || !is_operator(ops.top())) {
        *error = "malformed expression: missing operator";
        return false;
    }
    Kind op = ops.top();
    ops.pop();
    if (values.size() < 2) {
        *error = "malformed expression: missing operand";
        return false;
    }
    double right = values.top(); values.pop();   // first pop is the RIGHT operand
    double left  = values.top(); values.pop();

    double r = 0.0;
    switch (op) {
        case Kind::Plus:   r = left + right; break;
        case Kind::Minus:  r = left - right; break;
        case Kind::Times:  r = left * right; break;
        case Kind::Divide:
            if (right == 0.0) { *error = "division by zero"; return false; }
            r = left / right;
            break;
        default:
            *error = "malformed expression: unexpected token";
            return false;
    }
    values.push(r);
    return true;
}

}  // namespace

bool calc_evaluate(const std::vector<CalcToken>& tokens, double* out, std::string* error) {
    std::stack<double> values;
    std::stack<Kind>   ops;     // operators and '('

    for (const CalcToken& t : tokens) {
        switch (t.kind) {
            case Kind::Number:
                values.push(t.value);
                break;

            case Kind::LParen:
                ops.push(Kind::LParen);
                break;

            case Kind::RParen:
                while (!ops.empty() && ops.top() != Kind::LParen) {
                    if (!apply_top(values, ops, error)) return false;
                }
                if (ops.empty()) {
                    *error = "malformed expression: unmatched ')'";
                    return false;
                }
                ops.pop();   // discard the '('
                break;

            default:   // + - * /
                // >= gives left-to-right order for equal precedence.
                while (!ops.empty() && is_operator(ops.top()) &&
                       precedence(ops.top()) >= precedence(t.kind)) {
                    if (!apply_top(values, ops, error)) return false;
                }
                ops.push(t.kind);
                break;
        }
    }

    while (!ops.empty()) {
        if (ops.top() == Kind::LParen) {
            *error = "malformed expression: unmatched '('";
            return false;
        }
        if (!apply_top(values, ops, error)) return false;
    }

    if (values.size() != 1) {
        *error = "malformed expression";
        return false;
    }
    *out = values.top();
    return true;
}
