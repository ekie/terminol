// vi:noai:sw=4

#ifndef COMMON__BUFFER__HXX
#define COMMON__BUFFER__HXX

#include "terminol/common/support.hxx"
#include "terminol/common/cell.hxx"

#include <vector>
#include <deque>

class Buffer {
    class Line {
        std::vector<Cell> _cells;
        uint16_t          _damageBegin;
        uint16_t          _damageEnd;

    public:
        explicit Line(uint16_t cols) :
            _cells(cols, Cell::blank())
        {
            damageAll();
        }

        uint16_t getCols() const { return static_cast<uint16_t>(_cells.size()); }
        const Cell & getCell(uint16_t col) const { return _cells[col]; }

        void getDamage(uint16_t & colBegin, uint16_t & colEnd) const {
            colBegin = _damageBegin;
            colEnd   = _damageEnd;
        }

        void insert(uint16_t beforeCol, uint16_t n) {
            std::copy_backward(&_cells[beforeCol],
                               &_cells[_cells.size() - n],
                               &_cells[_cells.size()]);
            std::fill(&_cells[beforeCol], &_cells[beforeCol + n], Cell::blank());

            damageAdd(beforeCol, getCols());
        }

        void erase(uint16_t col, uint16_t n) {
            ASSERT(col + n <= getCols(), "");

            std::copy(&_cells[col] + n, &_cells[_cells.size()], &_cells[col]);
            std::fill(&_cells[_cells.size()] - n, &_cells[_cells.size()], Cell::blank());

            damageAdd(col, getCols());
        }

        void setCell(uint16_t col, const Cell & cell) {
            ASSERT(col < getCols(), "");
            _cells[col] = cell;

            damageAdd(col, col + 1);
        }

        void resize(uint16_t cols) {
            uint16_t oldCols = getCols();
            _cells.resize(cols, Cell::blank());

            if (cols > oldCols) {
                damageAdd(oldCols, cols);
            }
            else {
                _damageBegin = std::min(_damageBegin, cols);
                _damageEnd   = std::min(_damageEnd,   cols);
            }
        }

        void clear() {
            std::fill(_cells.begin(), _cells.end(), Cell::blank());
            damageAll();
        }

        void resetDamage() {
            _damageBegin = _damageEnd = 0;
        }

        void damageAll() {
            _damageBegin = 0;
            _damageEnd   = getCols();
        }

        void damageAdd(uint16_t begin, uint16_t end) {
            ASSERT(begin <  end, "");
            ASSERT(end   <= getCols(), "");

            if (_damageBegin == _damageEnd) {
                // No damage yet.
                _damageBegin = begin;
                _damageEnd   = end;
            }
            else {
                _damageBegin = std::min(_damageBegin, begin);
                _damageEnd   = std::max(_damageEnd,   end);
            }
        }
    };

    //
    //
    //

    std::deque<Line> _lines;
    uint16_t         _marginBegin;
    uint16_t         _marginEnd;

public:
    Buffer(uint16_t rows, uint16_t cols, size_t UNUSED(maxHistory)) :
        _lines(rows, Line(cols)),
        _marginBegin(0),
        _marginEnd(rows)
    {
        ASSERT(rows != 0, "");
        ASSERT(cols != 0, "");
    }

    uint16_t getRows()        const { return _lines.size(); }
    uint16_t getCols()        const { return _lines.front().getCols(); }

    uint16_t getMarginBegin() const { return _marginBegin; }
    uint16_t getMarginEnd()   const { return _marginEnd;   }

    void setMargins(uint16_t begin, uint16_t end) {
        ASSERT(begin < end, "");
        ASSERT(end <= getRows(), "");
        _marginBegin = begin;
        _marginEnd   = end;
    }

    void resetMargins() {
        _marginBegin = 0;
        _marginEnd   = getRows();
    }

    const Cell & getCell(uint16_t row, uint16_t col) const {
        ASSERT(row < getRows(), "");
        ASSERT(col < getCols(), "");
        return _lines[row].getCell(col);
    }

    void getDamage(uint16_t row, uint16_t & colBegin, uint16_t & colEnd) const {
        ASSERT(row < getRows(), "");
        _lines[row].getDamage(colBegin, colEnd);
    }

    void insertCells(uint16_t row, uint16_t beforeCol, uint16_t n) {
        ASSERT(row < getRows(), "");
        ASSERT(beforeCol <= getCols(), "");
        _lines[row].insert(beforeCol, n);
    }

    void eraseCells(uint16_t row, uint16_t col, uint16_t n) {
        ASSERT(row < getRows(), "");
        ASSERT(col < getCols(), "");
        _lines[row].erase(col, n);
    }

    void setCell(uint16_t row, uint16_t col, const Cell & cell) {
        ASSERT(row < getRows(), "");
        ASSERT(col < getCols(), "");
        _lines[row].setCell(col, cell);
    }

    void resize(uint16_t rows, uint16_t cols) {
        ASSERT(rows != 0, "");
        ASSERT(cols != 0, "");

        if (rows != getRows()) {
            _lines.resize(rows, Line(cols));
        }

        if (cols != getCols()) {
            for (auto & line : _lines) {
                line.resize(cols);
            }
        }

        _marginBegin = 0;
        _marginEnd   = rows;
    }

    void addLine() {
        //PRINT("Add line");
        _lines.insert(_lines.begin() + _marginEnd, Line(getCols()));
        _lines.erase(_lines.begin() + _marginBegin);

        for (uint16_t i = _marginBegin; i != _marginEnd; ++i) {
            _lines[i].damageAll();
        }
    }

    void insertLines(uint16_t beforeRow, uint16_t n) {
        /*
        PRINT("eraseLines. beforeRow=" << beforeRow << ", n=" << n <<
              ", rows=" << getRows() << ", scrollBegin=" << _marginBegin <<
              ", scrollEnd=" << _marginEnd);
              */
        ASSERT(beforeRow < getRows() + 1, "");
        _lines.erase(_lines.begin() + _marginEnd - n, _lines.begin() + _marginEnd);
        _lines.insert(_lines.begin() + beforeRow, n, Line(getCols()));

        for (uint16_t i = _marginBegin; i != _marginEnd; ++i) {
            _lines[i].damageAll();
        }
    }

    void eraseLines(uint16_t row, uint16_t n) {
        /*
        PRINT("eraseLines. row=" << row << ", n=" << n << ", rows=" <<
              getRows() << ", scrollBegin=" << _marginBegin <<
              ", scrollEnd=" << _marginEnd);
              */
        ASSERT(row + n < getRows() + 1, "");
        _lines.insert(_lines.begin() + _marginEnd, n, Line(getCols()));
        _lines.erase(_lines.begin() + row, _lines.begin() + row + n);

        for (uint16_t i = _marginBegin; i != _marginEnd; ++i) {
            _lines[i].damageAll();
        }
    }

    void clearLine(uint16_t row) {
        ASSERT(row < getRows(), "");
        _lines[row].clear();
    }

    void clearAll() {
        for (auto & line : _lines) {
            line.clear();
        }
    }

    void damageCell(uint16_t row, uint16_t col) {
        ASSERT(row < getRows(), "");
        ASSERT(col < getCols(), "");
        _lines[row].damageAdd(col, col + 1);
    }

    void resetDamage() {
        for (auto & line : _lines) {
            line.resetDamage();
        }
    }

    void damageAll() {
        for (auto & line : _lines) {
            line.damageAll();
        }
    }
};

void dump(std::ostream & ost, const Buffer & buffer);

#endif // COMMON__BUFFER__HXX