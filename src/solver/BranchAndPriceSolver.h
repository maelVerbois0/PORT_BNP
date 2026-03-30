#pragma once

#include <vector>
#include <tuple>

#include "TimeSpaceGraph.h"
#include "Train.h"
#include "MasterProblem.h"
#include "PricingProblem.h"
#include "GlobalStateManager.h"
#include "SearchTree.h"
#include "BPNode.h"


struct BranchingDecision {
    std::vector<int> left_train_ids;
    std::vector<int> right_train_ids;
    std::map<int,std::vector<int>> left_forbidden_arcs;  
    std::map<int, std::vector<int>> right_forbidden_arcs; 
};

enum class SolutionStatus{
    OPTIMAL,
    NOT_FOUND,
    FEASIBLE,
    UNFEASIBLE
};

enum class SolveStatus{
    OPTIMAL,
    TIME_LIMIT_REACHED,
    GAP_TOLERANCE_REACHED,
    INTERRUPTED
};

class BranchAndPriceSolver {
public:
    BranchAndPriceSolver(const TimeSpaceGraph& graph, const std::vector<Train>& trains);
    void solve();
    double get_optimal_value() const;
    std::vector<Column> get_optimal_solution() const;
private:
    void run_column_generation(BPNode* node, double termination_gap);
    bool is_current_solution_integer() const;
    BranchingDecision choose_branching_variable() const;
    BranchingDecision physical_node_branching() const;
    BranchingDecision quantum_train_branching() const;
    BranchingDecision service_split_branching() const;
    void branch_on_node(BPNode* parent, const BranchingDecision& decision);
    int add_Column(Column& col);
    void switch_state(BPNode* target);

    void run_global_mip_heuristic(double time_limit_seconds);
    void run_diving_heuristic(BPNode base_node);
    const TimeSpaceGraph& graph_;
    const std::vector<Train>& trains_;

    MasterProblem master_;
    PricingProblem pricing_;
    GlobalStateManager state_manager_;
    SearchTree tree_;
    ColumnPool column_pool_;

    constexpr static double INF = 1e+9;
};