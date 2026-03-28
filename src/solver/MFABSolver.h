#pragma once

#include <vector>
#include <map>
#include <string>

#include "TimeSpaceGraph.h"
#include "Train.h"
#include "MasterProblem.h"
#include "PricingProblem.h"
#include "GlobalStateManager.h"
#include "SearchTree.h"
#include "BPNode.h"
#include "Column.h"
#include "BranchAndPriceSolver.h"  // pour BranchingDecision


// ─────────────────────────────────────────────────────────────────────────────
// MFABSolver — Most Fractional Arc-Flow Branching
//
// Solveur Branch & Price alternatif au BranchAndPriceSolver.
// La seule différence est la règle de branchement :
//
//   Au lieu du quantum_train_branching (premier point de divergence par train,
//   partition 50/50 heuristique), ce solveur utilise le critère "most fractional"
//   global sur toutes les paires (train k, arc a) :
//
//     (k*, a*) = argmin_{k,a} | F(k,a) - 0.5 |
//
//   où F(k,a) = somme des valeurs LP des colonnes de k contenant a.
//
//   Fils gauche : a* interdit pour k*
//   Fils droit  : tous les arcs b ≠ a* partant du même nœud source que a*
//                 sont interdits pour k*  (force a* si le train passe par from(a*))
//
// Toutes les autres méthodes (génération de colonnes, switch_state, arbre de
// recherche, gestion de l'état) sont identiques au BranchAndPriceSolver.
//
// Pour comparer les deux solveurs, utiliser test_MFAB.cpp.
// ─────────────────────────────────────────────────────────────────────────────

class MFABSolver {
public:
    // Construit le solveur et initialise les colonnes dummy (une par train)
    MFABSolver(const TimeSpaceGraph& graph, const std::vector<Train>& trains);

    // Lance la résolution complète (B&P)
    void solve();

    // Accesseurs sur la solution finale
    double get_optimal_value() const;
    std::vector<Column> get_optimal_solution() const;

    // Statistiques utiles pour comparer avec BranchAndPriceSolver
    int get_node_count()   const { return node_count_; }
    int get_column_count() const { return static_cast<int>(column_pool_.size()); }

private:
    // ── Méthode de branchement ────────────────────────────────────────────
    // Unique règle de branchement de ce solveur : Most Fractional Arc-Flow
    BranchingDecision most_fractional_arc_branching() const;

    // Critère de sélection : score de fractionnarité + tie-breaking par type et impact
    // Retourne le score (plus petit = meilleur candidat)
    double arc_branching_score(int k_0idx, int a_id, double flow) const;

    // ── Méthodes du B&P (identiques à BranchAndPriceSolver) ──────────────
    void    run_column_generation(BPNode* node);
    bool    is_current_solution_integer() const;
    void    branch_on_node(BPNode* parent, const BranchingDecision& decision);
    int     add_Column(Column& col);
    void    switch_state(BPNode* target);

    // ── Membres ───────────────────────────────────────────────────────────
    const TimeSpaceGraph&     graph_;
    const std::vector<Train>& trains_;

    MasterProblem      master_;
    PricingProblem     pricing_;
    GlobalStateManager state_manager_;
    SearchTree         tree_;
    ColumnPool         column_pool_;

    int node_count_{0};  // compteur de nœuds explorés

    static constexpr double INF = 1e+9;
};