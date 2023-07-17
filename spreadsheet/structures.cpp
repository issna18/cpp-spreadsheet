#include "common.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <tuple>
#include <iostream>

constexpr char LETTER_A = 'A';
constexpr int LETTERS = 26;
constexpr int MAX_POSITION_LENGTH = 17;
constexpr int MAX_POS_LETTER_COUNT = 3;

const Position Position::NONE = {-1, -1};

bool Position::operator==(const Position rhs) const {
    return row == rhs.row && col == rhs.col;
}

bool Position::operator<(const Position rhs) const {
    return std::tie(row, col) < std::tie(rhs.row, rhs.col);
}

bool Position::IsValid() const {
    return row >= 0 && col >= 0 && row < MAX_ROWS && col < MAX_COLS;
}

std::string Position::ToString() const {
    if (!IsValid()) return {};

    std::string str;
    for(int column {col}; column >= 0; column = column / LETTERS - 1) {
        str += LETTER_A + (column % LETTERS);
    }
    std::reverse(str.begin(), str.end());
    return str + std::to_string(row + 1);
}

Position Position::FromString(std::string_view str) {
    if (str.empty()) return Position::NONE;

    auto is_valid_char = [](const char c) {
        return std::isalpha(c) && std::isupper(c);
    };

    auto is_valid_digit = [](const char c) {
        return std::isdigit(c);
    };

    const auto it_letters {std::find_if_not(str.begin(), str.end(), is_valid_char)};
    const auto it_digits {std::find_if_not(it_letters, str.end(), is_valid_digit)};

    if (it_letters == str.begin() || it_letters == str.end()) return Position::NONE;
    if (it_digits != str.end()) return Position::NONE;

    const auto position {std::distance(str.begin(), it_letters)};
    if (position > MAX_POS_LETTER_COUNT) return Position::NONE;

    const auto letters = str.substr(0, position);
    const auto digits = str.substr(position);

    int row {};
    try {
        row = std::stoi(std::string(digits));
    } catch (...) {
        return Position::NONE;
    }

    int column {};
    for (char letter : letters) {
        column = column * LETTERS + letter - LETTER_A + 1;
    }

    return {row - 1, column - 1};
}

bool Size::operator==(Size rhs) const {
    return rows == rhs.rows && cols == rhs.cols;
}


FormulaError::FormulaError(FormulaError::Category category)
    : category_ {category}
{}

FormulaError::Category FormulaError::GetCategory() const {
    return category_;
}

bool FormulaError::operator==(FormulaError rhs) const {
    return category_ == rhs.category_;
}

std::string_view FormulaError::ToString() const {
    switch (category_) {
    case Category::Ref:
        return "#REF!";
    case Category::Value:
        return "#VALUE!";
    case Category::Div0:
        return "#DIV/0!";
    default:
        return {};
    }
}
