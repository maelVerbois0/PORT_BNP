#pragma once
#include "TimeSpaceGraph.h"
#include "Train.h"
#include "Column.h"
#include "GlobalStateManager.h"
#include <vector>
#include <optional>

class PricingProblem{
    public:
        PricingProblem(const TimeSpaceGraph& graph, const std::vector<Train>& trains);
        std::vector<Column> solve(const std::vector<double>& service_duals, 
                              const std::vector<double>& conflict_duals,
                              const GlobalStateManager& state_manager);
    private:
        const TimeSpaceGraph& graph_;
        const std::vector<Train>& trains_;
        std::optional<Column> find_shortest_path_for_train(
        int train_id,
        double pi_k, 
        const std::vector<double>& capacity_duals,
        const GlobalStateManager& state_manager
    ) const;
};