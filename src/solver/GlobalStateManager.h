#pragma once
#include <vector>
#include <mdspan>
#include <Column.h>
/*
A class to deal with the physical/mathematical change on the graph/RMP dictated by the position on the search tree (decided by the Search Tree Object)
*/
using ColumnList = std::vector<int>;

class GlobalStateManager{
    public :
        GlobalStateManager(int num_trains, int num_arcs, MasterProblem* master_problem);
        void register_column_to_arc(int train_id, int arc_id, int column_id);
        void apply_delta(int train_id, const std::vector<int>& forbidden_arcs);
        void revert_delta(int train_id, const std::vector<int>& forbidden_arcs);
        bool is_arc_forbidden(int train_id, int arc_id) const;
    private :
        int num_trains_;
        int num_arcs_;
        
        std::vector<ColumnList> flat_arc_to_columns;
        std::mdspan<ColumnList, std::dextents<size_t, 2>> arc_to_columns;
        
        void register_column(const Column& col);
        
        std::vector<bool> flat_is_forbidden_;
        std::mdspan<bool, std::dextents<size_t, 2>> is_forbidden_;

        std::vector<int> conflict_count_;

};