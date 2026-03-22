#pragma once
#include <vector>
#include "Column.h"
#include <cstdint>

/*
A class to deal with the physical/mathematical change on the graph/RMP dictated by the position on the search tree (decided by the Search Tree Object)
*/
using ColumnList = std::vector<int>;

class GlobalStateManager{
    public :
        GlobalStateManager(int num_trains, int num_arcs);
        void register_column(const Column& col); // To update the arc_to_columns vector and initialise its conflict count
        ColumnList apply_delta(int train_id, const std::vector<int>& forbidden_arcs); //Gives back the id of the Column to remove after applying restriction
        ColumnList revert_delta(int train_id, const std::vector<int>& forbidden_arcs);//Gives back the id of the Column to add back after removing restriction
        bool is_arc_forbidden(int train_id, int arc_id) const;
    private :
        int num_trains_;
        int num_arcs_;
        
        std::vector<ColumnList> flat_arc_to_columns_; // gives back for   given train_id and a given arc in the TimeSpaceGraph the list of all the id of generated columns possessing this arc in their path
                                                    //Indexing is : train_id * num_arcs + arc_id
        std::vector<uint8_t> flat_is_forbidden_; // dimension, nb_trains * nb_arc. To access  num_arcs_ * train_id + arc_id
        std::vector<int> conflict_count_; // For each column contains its number of restriction (ie number of forbiden arc contained in the path)

};