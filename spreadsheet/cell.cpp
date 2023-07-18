#include "cell.h"

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>
#include <functional>

Cell::Cell(SheetInterface &sheet)
    : impl_ {std::make_unique<EmptyImpl>()},
      sheet_ {sheet}
{}

Cell::~Cell() = default;

void Cell::Set(std::string text) {
    auto tmp_cell {Cell(sheet_)};

    if (text.empty()) {
        tmp_cell.impl_ = std::make_unique<EmptyImpl>();
        std::swap(impl_, tmp_cell.impl_);
    } else if (text.front() == FORMULA_SIGN && text.size() > 1) {
        tmp_cell.impl_ = std::make_unique<FormulaImpl>(text.substr(1), sheet_);

        // Циклическую зависимость нужно проверять
        // только если мы меняем значение ячейки на формулу.
        // Предположим, что формула поменялась на текст или пустышку.
        // Клетку будем считать узлом графа. Тогда это будет означать,
        // что узел перестал иметь ребра от себя к другим,
        // (но рёбра от других могут остатья), т.е. узел стал листиком в графе.
        // А так как мы всегда имеем валидный граф без циклов,
        // то уничтожение рёбер не привдёт к появлению цикла.

        // Спасибо за подсказку,
        // что рёбра от других на текущую клетку могут остаться :)

        ThrowIfHasCycle(&tmp_cell);

        std::swap(impl_, tmp_cell.impl_);

        // Добавим пустые клетки, если формула на них ссылается.
        // Если потом кто-то изменит значение пустой клетки,
        // значение формулы в кэше должно инвалидироатьcя
        for (auto& ref_pos : impl_->GetReferencedCells()) {
            if (sheet_.GetCell(ref_pos) == nullptr) {
                sheet_.SetCell(ref_pos, {});
            }
        }
    } else {
        tmp_cell.impl_ = std::make_unique<TextImpl>(text);
        std::swap(impl_, tmp_cell.impl_);
    }

    // Теперь в tmp_cell лежит предыдущие значение клетки, получим из неё
    // список клеток на которые она ссылалась и удалим себя из них,
    // затем обновим обратные зависимости, добавив в них себя.
    // !!! Это нужно делать для всех, т.к. предыдущее значение
    //     может быть формулой и иметь ссылки.

    // Поменяла сигнатуру функции, кажется, так более явно
    UpdateDependencies(tmp_cell.GetReferencedCells(),
                       this->GetReferencedCells());
    InvalidateCache();
}

void Cell::SetFormula(std::string text) {
    impl_ = std::make_unique<FormulaImpl>(std::move(text), sheet_);
}

void Cell::Clear() {
    auto old = std::move(impl_);
    impl_ = std::make_unique<EmptyImpl>();
    UpdateDependencies(old->GetReferencedCells(),
                       this->GetReferencedCells());
    InvalidateCache();
}

void Cell::ThrowIfHasCycle(const CellInterface* cell) const {
    enum class DFSColor {
        None,
        Gray,
        Black,
    };

    std::unordered_map<const CellInterface*, DFSColor> visited_colors;

    std::function<bool(const CellInterface* cell)> DFSPaint =
            [&visited_colors, this, &DFSPaint](const CellInterface* cell)
    {
        const auto& color {visited_colors.emplace(cell, DFSColor::Gray)};
        for (const auto& ref_pos : cell->GetReferencedCells()) {
            const auto ref_cell {sheet_.GetCell(ref_pos)};
            if (ref_cell == nullptr) continue;
            if (visited_colors.count(ref_cell) > 0) {
                auto& ref_color = visited_colors.at(ref_cell);
                if (ref_color == DFSColor::Black) continue;
                if (ref_color == DFSColor::Gray) {
                    throw CircularDependencyException("cycle found");
                }
            }
            DFSPaint(ref_cell);
        }
        color.first->second = DFSColor::Black;
        return false;
    };

    visited_colors.emplace(this, DFSColor::Gray);
    DFSPaint(cell);
}

void Cell::UpdateDependencies(const std::vector<Position>& old_deps,
                              const std::vector<Position>& new_deps) {
    for (auto pos : old_deps) {
        auto cell = sheet_.GetCell(pos);
        dynamic_cast<Cell*>(cell)->back_dependencies_.erase(this);
    }

    for (auto pos : new_deps) {
        auto cell = sheet_.GetCell(pos);
        dynamic_cast<Cell*>(cell)->back_dependencies_.insert(this);
    }
}

bool Cell::IsReferenced() const {
    return back_dependencies_.size();
}

void Cell::InvalidateCache() {
    std::unordered_set<CellInterface*> visited_cells;

    std::function<void(Position)> DFS =
            [&visited_cells, this, &DFS](Position pos)
    {
        const auto cell {sheet_.GetCell(pos)};
        if (cell == nullptr) return;

        visited_cells.insert(cell);
        for (const auto& ref_pos : cell->GetReferencedCells()) {
            const auto ref_cell {sheet_.GetCell(ref_pos)};
            if (ref_cell == nullptr) continue;
            if (visited_cells.count(ref_cell) == 0) {
                DFS(ref_pos);
            }
        }
    };

    visited_cells.insert(this);
    for (auto ref_pos : GetReferencedCells()) {
        DFS(ref_pos);
    }

    for (auto cell : visited_cells) {
        dynamic_cast<Cell*>(cell)->impl_->InvalidateCache();
    }
}

CellInterface::Value Cell::GetValue() const {
    return impl_->GetValue();
}

std::string Cell::GetText() const {
    return impl_->GetText();
}

std::vector<Position> Cell::GetReferencedCells() const {
    return impl_->GetReferencedCells();
}

void Cell::PrintValue(std::ostream& output) {
    const auto& value = GetValue();
    if (std::holds_alternative<double>(value)) {
        output << std::get<double>(value);
    } else if (std::holds_alternative<FormulaError>(value)) {
        output << std::get<FormulaError>(value);
    } else {
        output << std::get<std::string>(value);
    }
}

std::vector<Position> Cell::Impl::GetReferencedCells() const {
    return {};
}

Cell::Value Cell::EmptyImpl::GetValue() const  {
    return {};
}

std::string Cell::EmptyImpl::GetText() const  {
    return {};
}

Cell::TextImpl::TextImpl(std::string text)
    : text_(std::move(text))
{}

Cell::Value Cell::TextImpl::GetValue() const {
    if (text_.front() == ESCAPE_SIGN) return text_.substr(1);
    return text_;
}

std::string Cell::TextImpl::GetText() const {
    return text_;
}

Cell::FormulaImpl::FormulaImpl(const std::string& text,
                               const SheetInterface &sheet)
    : formula_(ParseFormula(text)),
      sheet_{sheet}
{}

Cell::Value Cell::FormulaImpl::GetValue() const {
    if (cache_.is_valid) {
        return cache_.value;
    }

    const FormulaInterface::Value value {formula_->Evaluate(sheet_)};
    Cell::Value ret {};
    if (std::holds_alternative<double>(value)) {
        ret = std::get<double>(value);
    } else {
        ret = std::get<FormulaError>(value);
    }

    cache_.value = ret;
    cache_.is_valid = true;
    return ret;
}

std::string Cell::FormulaImpl::GetText() const {
    return FORMULA_SIGN + formula_->GetExpression();
}

std::vector<Position> Cell::FormulaImpl::GetReferencedCells() const {
    return formula_->GetReferencedCells();
}

void Cell::FormulaImpl::InvalidateCache() {
    cache_.is_valid = false;
}

