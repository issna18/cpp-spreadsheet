#include "FormulaAST.h"

#include "FormulaBaseListener.h"
#include "FormulaLexer.h"
#include "FormulaParser.h"

#include <cassert>
#include <cmath>
#include <memory>
#include <optional>
#include <sstream>

namespace ASTImpl {

enum ExprPrecedence {
    EP_ADD,
    EP_SUB,
    EP_MUL,
    EP_DIV,
    EP_UNARY,
    EP_ATOM,
    EP_END,
};

// a bit is set when the parentheses are needed
enum PrecedenceRule {
    PR_NONE = 0b00,                // never needed
    PR_LEFT = 0b01,                // needed for a left child
    PR_RIGHT = 0b10,               // needed for a right child
    PR_BOTH = PR_LEFT | PR_RIGHT,  // needed for both children
};

// PRECEDENCE_RULES[parent][child] determines if parentheses need
// to be inserted between a parent and a child of specific precedences;
// for some nodes rules are different for left and right children:
// (X c Y) p Z  vs  X p (Y c Z)
//
// The interesting cases are the ones where removing the parens would change the AST.
// It may happen when our precedence rules for parentheses are different from
// the grammatic precedence of operations.
//
// Case analysis:
// A + (B + C) - always okay (nothing of lower grammatic precedence could have been written to the
// right)
//    (e.g. if we had A + (B + C) / D, it wouldn't parse in a way
//    that woudld have given us A + (B + C) as a subexpression to deal with)
// A + (B - C) - always okay (nothing of lower grammatic precedence could have been written to the
// right) A - (B + C) - never okay A - (B - C) - never okay A * (B * C) - always okay (the parent
// has the highest grammatic precedence) A * (B / C) - always okay (the parent has the highest
// grammatic precedence) A / (B * C) - never okay A / (B / C) - never okay
// -(A + B) - never okay
// -(A - B) - never okay
// -(A * B) - always okay (the resulting binary op has the highest grammatic precedence)
// -(A / B) - always okay (the resulting binary op has the highest grammatic precedence)
// +(A + B) - **sometimes okay** (e.g. parens in +(A + B) / C are **not** optional)
//     (currently in the table we're always putting in the parentheses)
// +(A - B) - **sometimes okay** (same)
//     (currently in the table we're always putting in the parentheses)
// +(A * B) - always okay (the resulting binary op has the highest grammatic precedence)
// +(A / B) - always okay (the resulting binary op has the highest grammatic precedence)
constexpr PrecedenceRule PRECEDENCE_RULES[EP_END][EP_END] = {
    /* EP_ADD */ {PR_NONE, PR_NONE, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
    /* EP_SUB */ {PR_RIGHT, PR_RIGHT, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
    /* EP_MUL */ {PR_BOTH, PR_BOTH, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
    /* EP_DIV */ {PR_BOTH, PR_BOTH, PR_RIGHT, PR_RIGHT, PR_NONE, PR_NONE},
    /* EP_UNARY */ {PR_BOTH, PR_BOTH, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
    /* EP_ATOM */ {PR_NONE, PR_NONE, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
};

class Expr {
public:
    virtual ~Expr() = default;
    virtual void Print(std::ostream& out) const = 0;
    virtual void DoPrintFormula(std::ostream& out, ExprPrecedence precedence) const = 0;
    virtual double Evaluate(FunctorCellFromPosition get_cell) const = 0;

    // higher is tighter
    virtual ExprPrecedence GetPrecedence() const = 0;

    void PrintFormula(std::ostream& out, ExprPrecedence parent_precedence,
                      bool right_child = false) const {
        auto precedence = GetPrecedence();
        auto mask = right_child ? PR_RIGHT : PR_LEFT;
        bool parens_needed = PRECEDENCE_RULES[parent_precedence][precedence] & mask;
        if (parens_needed) {
            out << '(';
        }

        DoPrintFormula(out, precedence);

        if (parens_needed) {
            out << ')';
        }
    }
};

namespace {
class BinaryOpExpr final : public Expr {
public:
    enum Type : char {
        Add = '+',
        Subtract = '-',
        Multiply = '*',
        Divide = '/',
    };

public:
    explicit BinaryOpExpr(Type type, std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs)
        : type_(type)
        , lhs_(std::move(lhs))
        , rhs_(std::move(rhs)) {
    }

    void Print(std::ostream& out) const override {
        out << '(' << static_cast<char>(type_) << ' ';
        lhs_->Print(out);
        out << ' ';
        rhs_->Print(out);
        out << ')';
    }

    void DoPrintFormula(std::ostream& out, ExprPrecedence precedence) const override {
        lhs_->PrintFormula(out, precedence);
        out << static_cast<char>(type_);
        rhs_->PrintFormula(out, precedence, /* right_child = */ true);
    }

    ExprPrecedence GetPrecedence() const override {
        switch (type_) {
            case Add:
                return EP_ADD;
            case Subtract:
                return EP_SUB;
            case Multiply:
                return EP_MUL;
            case Divide:
                return EP_DIV;
            default:
                // have to do this because VC++ has a buggy warning
                assert(false);
                return static_cast<ExprPrecedence>(INT_MAX);
        }
    }

    double Evaluate(FunctorCellFromPosition get_cell) const override {

        auto Evaluator = [](double l,
                            double r,
                std::function<double(double,double)> op){
            double answer {op(l, r)};
            if (!std::isfinite(answer)) {
                throw FormulaError(FormulaError::Category::Div0);
            }
            return answer;
        };

        double lhs = lhs_->Evaluate(get_cell);
        double rhs = rhs_->Evaluate(get_cell);

        switch (type_) {
        case Add:
            return Evaluator(lhs, rhs, std::plus<double>());
        case Subtract:
            return Evaluator(lhs, rhs, std::minus<double>());
        case Multiply:
            return Evaluator(lhs, rhs, std::multiplies<double>());
        case Divide:
            return Evaluator(lhs, rhs, std::divides<double>());
        default:
            throw FormulaError(FormulaError::Category::Value);
        }
    }

private:
    Type type_;
    std::unique_ptr<Expr> lhs_;
    std::unique_ptr<Expr> rhs_;
};

class UnaryOpExpr final : public Expr {
public:
    enum Type : char {
        UnaryPlus = '+',
        UnaryMinus = '-',
    };

public:
    explicit UnaryOpExpr(Type type, std::unique_ptr<Expr> operand)
        : type_(type)
        , operand_(std::move(operand)) {
    }

    void Print(std::ostream& out) const override {
        out << '(' << static_cast<char>(type_) << ' ';
        operand_->Print(out);
        out << ')';
    }

    void DoPrintFormula(std::ostream& out, ExprPrecedence precedence) const override {
        out << static_cast<char>(type_);
        operand_->PrintFormula(out, precedence);
    }

    ExprPrecedence GetPrecedence() const override {
        return EP_UNARY;
    }

    double Evaluate(FunctorCellFromPosition get_cell) const override {
        double operand = operand_->Evaluate(get_cell);

        switch (type_) {
        case UnaryPlus:
            return operand;
        case UnaryMinus:
            return (-1.0) * operand;
        default:
            throw FormulaError(FormulaError::Category::Value);
        }
    }

private:
    Type type_;
    std::unique_ptr<Expr> operand_;
};

class CellExpr final : public Expr {
public:
    explicit CellExpr(const Position* cell)
        : cell_(cell) {
    }

    void Print(std::ostream& out) const override {
        if (!cell_->IsValid()) {
            out << FormulaError::Category::Ref;
        } else {
            out << cell_->ToString();
        }
    }

    void DoPrintFormula(std::ostream& out, ExprPrecedence /* precedence */) const override {
        Print(out);
    }

    ExprPrecedence GetPrecedence() const override {
        return EP_ATOM;
    }

    double Evaluate(FunctorCellFromPosition get_cell) const override {
        if (!cell_->IsValid()) {
            throw FormulaError(FormulaError::Category::Ref);
        }

        const auto cell {get_cell(*cell_)};
        if (cell == nullptr) {
            return 0.0;
        }

        const auto value {cell->GetValue()};
        if (std::holds_alternative<FormulaError>(value))
            throw FormulaError(FormulaError::Category::Value);

        if (std::holds_alternative<double>(value)) return std::get<double>(value);

        const std::string& str_value {std::get<std::string>(value)};
        if (str_value.empty()) return 0;

        //v.remove_prefix(std::min(v.find_first_not_of(" "), v.size()))
        size_t pos {};
        try {
            const double number {std::stod(str_value, &pos)};
            if (pos != str_value.size()) throw FormulaError(FormulaError::Category::Value);
            return number;
        } catch (...) {
            throw FormulaError(FormulaError::Category::Value);
        }
    }

private:
    const Position* cell_;
};

class NumberExpr final : public Expr {
public:
    explicit NumberExpr(double value)
        : value_(value) {
    }

    void Print(std::ostream& out) const override {
        out << value_;
    }

    void DoPrintFormula(std::ostream& out, ExprPrecedence /* precedence */) const override {
        out << value_;
    }

    ExprPrecedence GetPrecedence() const override {
        return EP_ATOM;
    }

    double Evaluate(std::function<const CellInterface*(Position)> CellFromPosition) const override {
        return value_;
    }

private:
    double value_;
};

class ParseASTListener final : public FormulaBaseListener {
public:
    std::unique_ptr<Expr> MoveRoot() {
        assert(args_.size() == 1);
        auto root = std::move(args_.front());
        args_.clear();

        return root;
    }

    std::forward_list<Position> MoveCells() {
        return std::move(cells_);
    }

public:
    void exitUnaryOp(FormulaParser::UnaryOpContext* ctx) override {
        assert(args_.size() >= 1);

        auto operand = std::move(args_.back());

        UnaryOpExpr::Type type;
        if (ctx->SUB()) {
            type = UnaryOpExpr::UnaryMinus;
        } else {
            assert(ctx->ADD() != nullptr);
            type = UnaryOpExpr::UnaryPlus;
        }

        auto node = std::make_unique<UnaryOpExpr>(type, std::move(operand));
        args_.back() = std::move(node);
    }

    void exitLiteral(FormulaParser::LiteralContext* ctx) override {
        double value = 0;
        auto valueStr = ctx->NUMBER()->getSymbol()->getText();
        std::istringstream in(valueStr);
        in >> value;
        if (!in) {
            throw ParsingError("Invalid number: " + valueStr);
        }

        auto node = std::make_unique<NumberExpr>(value);
        args_.push_back(std::move(node));
    }

    void exitCell(FormulaParser::CellContext* ctx) override {
        auto value_str = ctx->CELL()->getSymbol()->getText();
        auto value = Position::FromString(value_str);
        if (!value.IsValid()) {
            throw FormulaException("Invalid position: " + value_str);
        }

        cells_.push_front(value);
        auto node = std::make_unique<CellExpr>(&cells_.front());
        args_.push_back(std::move(node));
    }

    void exitBinaryOp(FormulaParser::BinaryOpContext* ctx) override {
        assert(args_.size() >= 2);

        auto rhs = std::move(args_.back());
        args_.pop_back();

        auto lhs = std::move(args_.back());

        BinaryOpExpr::Type type;
        if (ctx->ADD()) {
            type = BinaryOpExpr::Add;
        } else if (ctx->SUB()) {
            type = BinaryOpExpr::Subtract;
        } else if (ctx->MUL()) {
            type = BinaryOpExpr::Multiply;
        } else {
            assert(ctx->DIV() != nullptr);
            type = BinaryOpExpr::Divide;
        }

        auto node = std::make_unique<BinaryOpExpr>(type, std::move(lhs), std::move(rhs));
        args_.back() = std::move(node);
    }

    void visitErrorNode(antlr4::tree::ErrorNode* node) override {
        throw ParsingError("Error when parsing: " + node->getSymbol()->getText());
    }

private:
    std::vector<std::unique_ptr<Expr>> args_;
    std::forward_list<Position> cells_;
};

class BailErrorListener : public antlr4::BaseErrorListener {
public:
    void syntaxError(antlr4::Recognizer* /* recognizer */, antlr4::Token* /* offendingSymbol */,
                     size_t /* line */, size_t /* charPositionInLine */, const std::string& msg,
                     std::exception_ptr /* e */
                     ) override {
        throw ParsingError("Error when lexing: " + msg);
    }
};

}  // namespace
}  // namespace ASTImpl

FormulaAST ParseFormulaAST(std::istream& in) {
    using namespace antlr4;

    ANTLRInputStream input(in);

    FormulaLexer lexer(&input);
    ASTImpl::BailErrorListener error_listener;
    lexer.removeErrorListeners();
    lexer.addErrorListener(&error_listener);

    CommonTokenStream tokens(&lexer);

    FormulaParser parser(&tokens);
    auto error_handler = std::make_shared<BailErrorStrategy>();
    parser.setErrorHandler(error_handler);
    parser.removeErrorListeners();

    tree::ParseTree* tree = parser.main();
    ASTImpl::ParseASTListener listener;
    tree::ParseTreeWalker::DEFAULT.walk(&listener, tree);

    return FormulaAST(listener.MoveRoot(), listener.MoveCells());
}

FormulaAST ParseFormulaAST(const std::string& in_str) {
    std::istringstream in(in_str);
    try {
        return ParseFormulaAST(in);
    } catch(...) {
        throw FormulaException("aaa");
    }
}

void FormulaAST::PrintCells(std::ostream& out) const {
    for (auto cell : cells_) {
        out << cell.ToString() << ' ';
    }
}

void FormulaAST::Print(std::ostream& out) const {
    root_expr_->Print(out);
}

void FormulaAST::PrintFormula(std::ostream& out) const {
    root_expr_->PrintFormula(out, ASTImpl::EP_ATOM);
}

double FormulaAST::Execute(FunctorCellFromPosition get_cell) const {
    return root_expr_->Evaluate(get_cell);
}

FormulaAST::FormulaAST(std::unique_ptr<ASTImpl::Expr> root_expr, std::forward_list<Position> cells)
    : root_expr_(std::move(root_expr))
    , cells_(std::move(cells)) {
    cells_.sort();  // to avoid sorting in GetReferencedCells
}

FormulaAST::~FormulaAST() = default;
