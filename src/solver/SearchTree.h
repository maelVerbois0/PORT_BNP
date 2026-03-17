#pragma once

#include <vector>
#include <queue>
#include <stack>
#include <memory>
#include "Column.h"
#include "BPNode.h"


struct CompareNodeBound {
    bool operator()(const BPNode* a, const BPNode* b) const {
        return a->lower_bound > b->lower_bound;
    }
};

enum class SearchStrategy{
    DFS,
    BBF
};


class SearchTree{
    public:

        BPNode* get_next_node();
        void set_strategy_DFS();  //Apply a Depth First Search Strategy to quickly find an integer feasible solution
        void set_strategy_BBF(); //Apply a Best Bound First Strategy to try to find an optimal solution
        void process_node(BPNode* node); // Modify the graph structure to be compliant with the node and launch the Column generation algorithm on the node
        void branch(BPNode* node); // Branch on a processed node
        void prune(BPNode* node); //Prune a node (eg because its marked unfeasible or the incumbent is better than the LB)
        void add_dfs_node(std::unique_ptr<BPNode> new_node);
        void add_bbf_node(std::unique_ptr<BPNode> new_node);
        double get_best_upper_bound() const;
        double get_best_lower_bound() const;
        SearchStrategy get_current_strategy() const;
        std::vector<Column> get_incumbent_solution() const;
        void set_best_upper_bound(double upper_bound);
        void set_best_lower_bound(double lower_bound);
        void set_incumbent_solution(double upper_bound, const std::vector<Column>& solution);
    private:
        std::vector<std::unique_ptr<BPNode>> all_generated_node; //Manages the ownership of the nodes
        std::stack<BPNode*> open_node_DFS; //Raw pointers don't manage ownership no risk of memory leaks
        std::priority_queue<BPNode*, std::vector<BPNode*>, CompareNodeBound> open_node_BBF;
        BPNode* current_node;
        SearchStrategy current_strategy;
        std::vector<Column> incumbent_solution;
        double best_upper_bound;
        double best_lower_bound;

};  