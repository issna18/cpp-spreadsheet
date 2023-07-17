#include "sheet.h"

#include "cell.h"
#include "common.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <optional>
#include <utility>

using namespace std::literals;

Sheet::~Sheet() {}

void Sheet::SetCell(Position pos, std::string text) {
    if (!pos.IsValid()) {
        throw InvalidPositionException("Invalid position");
    }
    if (sheet_.count(pos) == 0) sheet_.emplace(pos, std::make_unique<Cell>(*this));

    auto& added_cell {sheet_.at(pos)};
    added_cell->Set(text);
}

const CellInterface* Sheet::GetCell(Position pos) const {
    if (!pos.IsValid()) {
        throw InvalidPositionException("Invalid position");
    }

    if (sheet_.count(pos) == 0) return nullptr;

    return sheet_.at(pos).get();
}

CellInterface* Sheet::GetCell(Position pos) {
    return const_cast<CellInterface*>(std::as_const(*this).GetCell(pos));
}

void Sheet::ClearCell(Position pos) {
    if (!pos.IsValid()) {
        throw InvalidPositionException("Invalid position");
    }

    sheet_.erase(pos);
}

Size Sheet::GetPrintableSize() const {
    Size size {0, 0};
    for (const auto& [pos, _] : sheet_) {
        size = {std::max(size.rows, pos.row + 1),
                std::max(size.cols, pos.col + 1)};
    }
    return size;
}

void Sheet::PrintValues(std::ostream& output) const {
    const Size size {GetPrintableSize()};
    for (int row {0}; row < size.rows; ++row) {
        for (int column {0}; column < size.cols; ++column) {
            Position pos {row, column};
            if (sheet_.count(pos) == 0) {
                output << "";
            } else {
                sheet_.at(pos)->PrintValue(output);
            }
            if (column < size.cols - 1) {
                output << '\t';
            }
        }
        output << '\n';
    }
}

void Sheet::PrintTexts(std::ostream& output) const {
    const Size size {GetPrintableSize()};
    for (int row {0}; row < size.rows; ++row) {
        for (int column {0}; column < size.cols; ++column) {
            Position pos {row, column};
            if (sheet_.count(pos) == 0) {
                output << "";
            } else {
                output << sheet_.at(pos)->GetText();
            }
            if (column < size.cols - 1) {
                output << '\t';
            }
        }
        output << '\n';
    }
}

std::unique_ptr<SheetInterface> CreateSheet() {
    return std::make_unique<Sheet>();
}


