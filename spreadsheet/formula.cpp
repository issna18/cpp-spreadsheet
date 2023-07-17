#include "formula.h"

#include "FormulaAST.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <sstream>
#include <set>

#include <iostream>
#include <iterator>

using namespace std::literals;

std::ostream& operator<<(std::ostream& output, FormulaError fe) {
    return output << fe.ToString();
}

namespace {
class Formula : public FormulaInterface {
public:
// Реализуйте следующие методы:
    explicit Formula(std::string expression)
        : ast_(ParseFormulaAST(std::move(expression)))
    {}

    Value Evaluate(const SheetInterface& sheet) const override {
        FunctorCellFromPosition GetCellLambda =
                [&sheet](Position pos)
            {
                return sheet.GetCell(pos);
            };
        try {
            return ast_.Execute(GetCellLambda);
        } catch (const FormulaError& exc) {
            return exc;
        }
    }

    std::string GetExpression() const override {
        std::stringstream output;
        ast_.PrintFormula(output);
        return output.str();
    }

    std::vector<Position> GetReferencedCells() const override {
        std::set<Position> ref_set;

        for (Position pos : ast_.GetCells()) {
            if (pos.IsValid()) {
                ref_set.emplace(std::move(pos));
            }
        }
        std::vector<Position> refs(std::move_iterator(ref_set.begin()),
                                   std::move_iterator(ref_set.end()));
        return refs;
    }

private:
    FormulaAST ast_;
};
}  // namespace

std::unique_ptr<FormulaInterface> ParseFormula(std::string expression) {
    return std::make_unique<Formula>(std::move(expression));
}
