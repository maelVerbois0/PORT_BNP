#pragma once

#include <vector>
#include <optional>
#include "TimeSpaceGraph.h"
#include "Train.h"
#include "Column.h"


class PricingProblem {
private:
    const TimeSpaceGraph& graph_;
    const std::vector<Train>& trains_;

    // Constante pour l'algorithme de plus court chemin
    static constexpr double INF = 1e9; 

    // Calcule le coût réduit d'un arc à la volée définit comme étant le cout de l'arc - le cout des conflits associées à l'arc (la logique de service est implémenté dans l'algo de recherche)
    double get_arc_reduced_cost(
        int train_id, 
        const Arc& arc, 
        const std::vector<std::vector<double>>& conflict_duals
    ) const;

public:
    PricingProblem(const TimeSpaceGraph& graph, const std::vector<Train>& trains);

    std::vector<Column> solve(
        const std::vector<double>& flow_duals, 
        const std::vector<std::vector<double>>& service_duals, 
        const std::vector<std::vector<double>>& conflict_duals
    ) const;

    // Résout le pricing pour UN train spécifique (le cœur de l'algo)
    std::optional<Column> find_shortest_path_for_train(
        int train_id,
        double pi_k, 
        const std::vector<double>& service_duals_k,
        const std::vector<std::vector<double>>& conflict_duals
    ) const;
};