#pragma once

#include <vector>
#include <map>
#include <optional>
#include <string>

#include "TimeSpaceGraph.h"
#include "Train.h"
#include "MasterProblem.h"
#include "PricingProblem.h"
#include "GlobalStateManager.h"
#include "SearchTree.h"
#include "BPNode.h"
#include "Column.h"
#include "BranchAndPriceSolver.h"

// =============================================================================
// MFABSolver  —  Service-Location Branching + Common-Prefix Arc Branching
//
// Stratégie inspirée du schéma Ryan & Foster (Bin Packing with Conflicts,
// Sadykov & Vanderbeck 2010), adapté au TUSP.
//
// NIVEAU 1 — Service-Location Branching (SLB) [primaire, inspiré R&F]
//   Une solution LP fractionnaire peut affecter le service s du train k à
//   plusieurs nœuds physiques fractionnellement : flow(k,s,p1)=0.4, flow(k,s,p2)=0.6.
//   On sélectionne la paire (k, s, p) la plus fractionnaire (|flow - 0.5| minimal)
//   et on branche dessus :
//     Fils G : service s de k NE SE FAIT PAS à p  → interdit arcs SERVICE-s à p
//     Fils D : service s de k SE FAIT à p         → interdit arcs SERVICE-s ailleurs
//   Analogue R&F direct : "items i et j dans le même bin / séparés".
//   Convergence garantie en O(K × S × P) décisions de branchement max.
//
// NIVEAU 2 — Common-Prefix Arc Branching (CPB) [fallback]
//   Quand toutes les affectations service-location sont entières mais que
//   le routage est encore fractionnaire, on trouve le PREMIER nœud universel
//   du DAG (tous les chemins fractionnaires du train y passent et y divergent)
//   et on branche dessus :
//     Fils G : arc dominant interdit
//     Fils D : tous les autres arcs depuis ce nœud interdits (via getFromArcs)
//   Universalité du nœud garantit l'exhaustivité et évite le blocage infini.
// =============================================================================

class MFABSolver {
public:
    MFABSolver(const TimeSpaceGraph& graph, const std::vector<Train>& trains);
    void solve();

    double              get_optimal_value()   const;
    std::vector<Column> get_optimal_solution() const;
    int get_node_count()   const { return node_count_; }
    int get_column_count() const { return static_cast<int>(column_pool_.size()); }

private:
    // ── Branchement hiérarchique ───────────────────────────────────────────
    BranchingDecision choose_branching() const;   // dispatcher SLB → CPB

    // Niveau 1 : Ryan & Foster adapté TUSP
    std::optional<BranchingDecision> service_location_branching() const;

    // Niveau 2 : Common-Prefix Arc Branching (fallback routage)
    BranchingDecision most_fractional_arc_branching() const;

    static double frac_score(double f) { return std::abs(f - 0.5); }

    // ── B&P standard ──────────────────────────────────────────────────────
    void run_column_generation(BPNode* node);
    bool is_current_solution_integer() const;
    void branch_on_node(BPNode* parent, const BranchingDecision& decision);
    int  add_Column(Column& col);
    void switch_state(BPNode* target);

    // ── Membres ───────────────────────────────────────────────────────────
    const TimeSpaceGraph&     graph_;
    const std::vector<Train>& trains_;
    MasterProblem      master_;
    PricingProblem     pricing_;
    GlobalStateManager state_manager_;
    SearchTree         tree_;
    ColumnPool         column_pool_;
    int node_count_{0};
    static constexpr double INF = 1e+9;
};