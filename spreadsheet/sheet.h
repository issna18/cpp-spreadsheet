#pragma once

#include "cell.h"
#include "common.h"

#include <vector>
#include <unordered_map>
#include <unordered_set>

class Sheet : public SheetInterface {
public:
    ~Sheet();

    void SetCell(Position pos, std::string text) override;

    const CellInterface* GetCell(Position pos) const override;
    CellInterface* GetCell(Position pos) override;

    void ClearCell(Position pos) override;

    Size GetPrintableSize() const override;

    void PrintValues(std::ostream& output) const override;
    void PrintTexts(std::ostream& output) const override;

private:
    std::unordered_map<Position, std::unique_ptr<Cell>, PositionHasher> sheet_;
    //const Cell empty_ {*this};
};
