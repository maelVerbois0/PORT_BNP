#pragma once

#include <tuple>
#include <vector>

enum class NodeStatus {
    UNPROCESSED, PROCESSING, FRACTIONAL, INTEGER, INFEASIBLE, PRUNED
};

struct BPNode {
    // Node depth in the branch and price tree
    BPNode* parent;
    //Node depth
    int depth;
    // Node lower bound - best lowerbound obtained on this node
    double lower_bound;
    //Status of the BP Node
    NodeStatus status;
    //Which train the restriction is applied on
    int train_id;
    // Subset of forbidden arcs decided in the parent node 
    std::vector<int> forbidden_arcs_ids;

}
;

