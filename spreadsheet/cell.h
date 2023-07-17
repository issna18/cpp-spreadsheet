#pragma once

#include "common.h"
#include "formula.h"

#include <unordered_set>

struct CellCache {
    CellInterface::Value value {};
    bool is_valid {false};
};

class Cell : public CellInterface {
public:
    Cell(SheetInterface &sheet);
    ~Cell();

    void Set(std::string text);
    void Clear();

    Value GetValue() const override;
    std::string GetText() const override;
    std::vector<Position> GetReferencedCells() const override;

    bool IsReferenced() const;

    void PrintValue(std::ostream& output);

private:
    class Impl;
    class EmptyImpl;
    class TextImpl;
    class FormulaImpl;

    void UpdateDependencies(const std::vector<Position>& old);
    void InvalidateCache();
    void ThrowIfHasCycle(const CellInterface *cell) const;
    void SetFormula(std::string text);

    std::unique_ptr<Impl> impl_;
    SheetInterface& sheet_;
    std::unordered_set<CellInterface*> back_dependencies_;
};

class Cell::Impl {
public:
    virtual Value GetValue() const = 0;
    virtual std::string GetText() const = 0;
    virtual std::vector<Position> GetReferencedCells() const;
    virtual void InvalidateCache() {};
    virtual ~Impl() = default;
};

class Cell::EmptyImpl : public Cell::Impl {
public:
    Value GetValue() const override;
    std::string GetText() const override;
};

class Cell::TextImpl : public Cell::Impl {
public:
    TextImpl(std::string text);
    Value GetValue() const override;
    std::string GetText() const override;

private:
    std::string text_;
};

class Cell::FormulaImpl : public Cell::Impl {
public:
    FormulaImpl(const std::string& text, const SheetInterface& sheet);
    Value GetValue() const override;
    std::string GetText() const override;
    std::vector<Position> GetReferencedCells() const override;
    void InvalidateCache() override;

private:
    std::unique_ptr<FormulaInterface> formula_;
    const SheetInterface& sheet_;
    mutable CellCache cache_;
};
