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

// Une petite structure claire pour stocker la décision de branchement
struct BranchingDecision {
    int train_id;
    // Les arcs à interdire dans l'enfant de gauche (ex: le premier groupe de 50%)
    std::vector<int> left_forbidden_arcs;  
    // Les arcs à interdire dans l'enfant de droite (ex: le deuxième groupe de 50%)
    std::vector<int> right_forbidden_arcs; 
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
    void run_column_generation(BPNode* node);
    bool is_current_solution_integer() const;
    BranchingDecision choose_branching_variable() const;
    void branch_on_node(BPNode* parent, const BranchingDecision& decision);
    int add_Column(Column& col);
    void switch_state(BPNode* target);

    const TimeSpaceGraph& graph_;
    const std::vector<Train>& trains_;

    MasterProblem master_;
    PricingProblem pricing_;
    GlobalStateManager state_manager_;
    SearchTree tree_;
    ColumnPool column_pool_;

    constexpr static double INF = 1e+9;
};