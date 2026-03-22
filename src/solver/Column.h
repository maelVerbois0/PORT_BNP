#pragma once
#include <vector>

struct Column
{
    int ID;                // unique id corresponds to the position of the Column 
    std::vector<int> arc_ids;   // Set of virtual arcs in the path 
    std::vector<bool> services; // Set of services performed by the path
    int cost;              // Total cost of the path
    int train_id;             // id of the associated train
};

class ColumnPool {
public:
    ColumnPool() = default;
    int add_column(Column col) {
        int new_id = columns_.size();
        col.ID = new_id; 
        columns_.push_back(std::move(col));
        return new_id;
    }
    const Column& get_column(int id) const {
        return columns_[id];
    }
    const std::vector<Column>& get_all_columns() const {
        return columns_;
    }

    size_t size() const {
        return columns_.size();
    }

private:
    std::vector<Column> columns_;
};