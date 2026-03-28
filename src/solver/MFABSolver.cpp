#include "MFABSolver.h"

#include <unordered_map>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <map>

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

MFABSolver::MFABSolver(const TimeSpaceGraph& graph, const std::vector<Train>& trains)
    : graph_(graph),
      trains_(trains),
      master_(graph, trains),
      pricing_(graph, trains),
      state_manager_(static_cast<int>(trains.size()), static_cast<int>(graph.get_arcs().size())),
      tree_(),
      column_pool_()
{
    // Initialisation identique à BranchAndPriceSolver :
    // une colonne dummy (arc fictif) par train, enregistrée dans les trois composants.
    for (size_t k = 0; k < trains_.size(); ++k) {
        Column dummy_col = Column(
            static_cast<int>(k),
            { graph_.get_dummy_arc_id(static_cast<int>(k)) },
            std::vector<bool>(graph_.get_nb_services(), true),
            graph_.get_arc(graph_.get_dummy_arc_id(static_cast<int>(k))).get_cost(static_cast<int>(k)),
            static_cast<int>(k + 1)   // train_id 1-indexé
        );
        add_Column(dummy_col);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Gestion des colonnes
// ─────────────────────────────────────────────────────────────────────────────

int MFABSolver::add_Column(Column& col) {
    int new_id = column_pool_.add_column(col);
    master_.add_column(col, graph_);
    state_manager_.register_column(col);
    return new_id;
}

// ─────────────────────────────────────────────────────────────────────────────
// Switch d'état dans l'arbre (identique à BranchAndPriceSolver)
// ─────────────────────────────────────────────────────────────────────────────

void MFABSolver::switch_state(BPNode* target) {
    Trajectory trajectory = tree_.trajectory_to_node(target);

    for (BPNode* to_rev : trajectory.nodes_to_revert) {
        if (to_rev->train_ids.empty())
            throw std::runtime_error("[MFABSolver] switch_state: attempting to revert root node");
        ColumnList enable = state_manager_.revert_delta(to_rev->train_ids, to_rev->forbidden_arcs_ids);
        master_.enable_columns(enable);
    }
    for (BPNode* to_app : trajectory.nodes_to_apply) {
        ColumnList disable = state_manager_.apply_delta(to_app->train_ids, to_app->forbidden_arcs_ids);
        master_.disable_columns(disable);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Génération de colonnes (identique à BranchAndPriceSolver)
// ─────────────────────────────────────────────────────────────────────────────

void MFABSolver::run_column_generation(BPNode* node) {
    if (node->status != NodeStatus::UNPROCESSED)
        throw std::runtime_error("[MFABSolver] run_column_generation: nœud déjà traité");

    node->status = NodeStatus::PROCESSING;

    while (true) {
        master_.solve();

        if (!master_.is_infeasible() && !master_.is_optimal())
            throw std::runtime_error("[MFABSolver] run_column_generation: statut Gurobi inattendu");

        if (master_.is_infeasible()) {
            node->status      = NodeStatus::INFEASIBLE;
            node->lower_bound = -INF;
            break;
        }

        const auto flow_duals    = master_.get_flow_duals();
        const auto service_duals = master_.get_service_duals();
        const auto conflict_duals= master_.get_conflict_duals();

        std::vector<Column> new_cols = pricing_.solve(flow_duals, service_duals, conflict_duals, state_manager_);

        if (new_cols.empty()) {
            // Optimalité du nœud courant
            node->lower_bound = master_.get_objective_value();

            if (master_.is_sol_integer()) {
                node->status = NodeStatus::INTEGER;
                if (tree_.get_best_upper_bound() > master_.get_objective_value()) {
                    const auto active_ids = master_.get_active_columns_ids();
                    const auto incumbent  = column_pool_.get_columns(active_ids);
                    tree_.set_incumbent_solution(master_.get_objective_value(), incumbent);
                }
            } else if (master_.is_sol_fractional()) {
                node->status = NodeStatus::FRACTIONAL;
                if (node->lower_bound > tree_.get_best_upper_bound() - 1e-5)
                    node->status = NodeStatus::PRUNED;
            }
            break;
        }

        for (auto& col : new_cols)
            add_Column(col);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Utilitaires de l'arbre (identiques à BranchAndPriceSolver)
// ─────────────────────────────────────────────────────────────────────────────

bool MFABSolver::is_current_solution_integer() const {
    switch (tree_.get_current_node()->status) {
        case NodeStatus::INTEGER:    return true;
        case NodeStatus::FRACTIONAL: return false;
        case NodeStatus::INFEASIBLE: return false;
        default:
            throw std::runtime_error("[MFABSolver] is_current_solution_integer: statut de nœud invalide");
    }
}

void MFABSolver::branch_on_node(BPNode* parent, const BranchingDecision& decision) {
    tree_.create_child_node(parent, decision.left_train_ids,  decision.left_forbidden_arcs);
    tree_.create_child_node(parent, decision.right_train_ids, decision.right_forbidden_arcs);
}

// ─────────────────────────────────────────────────────────────────────────────
// Règle de branchement MFAB — Most Fractional Arc-Flow Branching
// ─────────────────────────────────────────────────────────────────────────────

// Score d'un nœud de divergence : mesure à quel point le flux se répartit
// de manière équilibrée entre ses arcs sortants (idéal = ratio 0.5 = split 50/50).
// Plus le score est bas, meilleur est le candidat au branchement.
double MFABSolver::arc_branching_score(int /*k_0idx*/, int /*a_id*/, double ratio) const {
    return std::abs(ratio - 0.5);
}

BranchingDecision MFABSolver::most_fractional_arc_branching() const {

    const int n_trains = static_cast<int>(trains_.size());

    // ═════════════════════════════════════════════════════════════════════
    // ÉTAPE 1 — Accumuler le flux actif par arc et par train
    //
    // active_arc_flow[k][a_id] = somme des valeurs LP des colonnes du train k
    // qui contiennent l'arc a_id (et dont la valeur est significative).
    // On utilise une map pour n'allouer que les arcs effectivement utilisés.
    // ═════════════════════════════════════════════════════════════════════
    std::vector<std::map<int,double>> active_arc_flow(n_trains);

    for (int col_id : master_.get_active_columns_ids()) {
        double val = master_.get_column_value(col_id);
        if (val < 1e-5) continue;
        const Column& col = column_pool_.get_column(col_id);
        int k = col.train_id - 1;
        for (int a_id : col.arc_ids)
            active_arc_flow[k][a_id] += val;
    }

    // ═════════════════════════════════════════════════════════════════════
    // ÉTAPE 2 — Trouver le meilleur nœud de divergence (k*, u*)
    //
    // Un "nœud de divergence" pour le train k au nœud u est un endroit où
    // le flux actif du train k se répartit sur au moins 2 arcs sortants.
    // C'est là que le LP choisit "un peu des deux chemins" au lieu de s'engager.
    //
    // Critère de sélection : ratio = flux_max / flux_total à ce nœud.
    // Un ratio proche de 0.5 = split équilibré = meilleur candidat de branchement
    // (garantit une amélioration maximale de la borne dans les deux fils).
    // ═════════════════════════════════════════════════════════════════════
    double best_balance = INF;
    int    best_k       = -1;
    int    best_u       = -1;
    int    best_arc_left = -1;      // arc avec le plus de flux depuis best_u
    std::vector<int> best_arcs_right; // arcs concurrents ACTIFS depuis best_u

    for (int k = 0; k < n_trains; ++k) {
        // Regrouper les arcs actifs du train k par nœud source
        std::map<int, std::vector<std::pair<int,double>>> node_outflows;
        for (const auto& [a_id, f] : active_arc_flow[k]) {
            if (f < 1e-5) continue;
            node_outflows[graph_.get_arc(a_id).get_from()].push_back({a_id, f});
        }

        for (const auto& [u, outflows] : node_outflows) {
            // Pas de divergence si un seul arc a du flux
            if (outflows.size() < 2) continue;

            double total = 0.0, max_f = 0.0;
            int    max_a = -1;
            for (const auto& [a, f] : outflows) {
                total += f;
                if (f > max_f) { max_f = f; max_a = a; }
            }

            // ratio = part du flux portée par l'arc dominant
            double ratio   = max_f / total;
            double balance = arc_branching_score(k, max_a, ratio);

            if (balance < best_balance) {
                best_balance   = balance;
                best_k         = k;
                best_u         = u;
                best_arc_left  = max_a;

                // Arcs actifs concurrents (utilisés dans le calcul de balance,
                // conservés ici uniquement pour le log)
                best_arcs_right.clear();
                for (const auto& [a, f] : outflows)
                    if (a != max_a) best_arcs_right.push_back(a);
            }
        }
    }

    if (best_k < 0)
        throw std::runtime_error(
            "[MFABSolver] most_fractional_arc_branching: aucun noeud de divergence "
            "actif trouve. La solution courante semble entiere ou degenere.");

    const int train_id = best_k + 1;   // retour en 1-indexé

    // ═════════════════════════════════════════════════════════════════════
    // ÉTAPE 3 — Construire les deux fils (CORRECTION DU BUG D'EXHAUSTIVITE)
    //
    // Fils GAUCHE : interdit best_arc_left pour train k*.
    //   → Couvre toutes les solutions entières qui n'empruntent PAS best_arc_left.
    //
    // Fils DROIT  : interdit TOUS les arcs depuis best_u SAUF best_arc_left,
    //               y compris ceux qui n'ont pas de flux dans la solution courante.
    //
    // POURQUOI C'EST CRITIQUE : si on n'interdit que les arcs ACTIFS (ceux
    // dans best_arcs_right), la génération de colonnes dans le fils droit peut
    // créer de nouveaux chemins passant par best_u via des arcs non actifs.
    // Ces nouveaux arcs ne sont pas interdits → solution fractionnaire à nouveau
    // → branchement infini → profondeur infinie → LB bloqué à -1e+09.
    //
    // La correction itère sur graph_.get_node(best_u).getFromArcs() qui donne
    // TOUS les arcs du graphe partant de best_u (actifs ou non).
    // ═════════════════════════════════════════════════════════════════════

    std::vector<int> right_forbidden;
    for (int b_id : graph_.get_node(best_u).getFromArcs()) {
        if (b_id == best_arc_left) continue;                              // forcer best_arc_left
        if (!graph_.arc_accessible_by_train(best_k, b_id)) continue;     // non accessible
        if (state_manager_.is_arc_forbidden(train_id, b_id)) continue;   // déjà interdit
        right_forbidden.push_back(b_id);
    }

    // Log détaillé
    std::cerr << "[MFAB] Divergence au noeud " << best_u
              << " (train " << train_id << ")"
              << "  ratio=" << std::abs(active_arc_flow[best_k][best_arc_left]
                                        / [&]{ double t=0; for(auto a:best_arcs_right) t+=active_arc_flow[best_k][a]; return t+active_arc_flow[best_k][best_arc_left]; }())
              << "\n       Fils GAUCHE : arc " << best_arc_left
              << " interdit (F=" << active_arc_flow[best_k][best_arc_left] << ")"
              << "\n       Fils DROIT  : " << right_forbidden.size()
              << " arcs interdits depuis noeud " << best_u
              << " (actifs=" << best_arcs_right.size() << ")"
              << std::endl;

    BranchingDecision decision;
    decision.left_train_ids  = { train_id };
    decision.right_train_ids = { train_id };
    decision.left_forbidden_arcs[train_id]  = { best_arc_left };
    decision.right_forbidden_arcs[train_id] = std::move(right_forbidden);

    return decision;
}

// ─────────────────────────────────────────────────────────────────────────────
// Boucle principale solve() (identique à BranchAndPriceSolver, sauf l'appel
// à most_fractional_arc_branching au lieu de choose_branching_variable)
// ─────────────────────────────────────────────────────────────────────────────

void MFABSolver::solve() {
    BPNode* current_node = tree_.get_next_node();

    while (current_node != nullptr) {
        ++node_count_;

        // ── Affichage de progression ──────────────────────────────────────
        if (node_count_ % 50 == 0) {
            double ub = tree_.get_best_upper_bound();
            double lb = tree_.get_best_lower_bound();
            std::cerr << "[MFAB] Noeuds : " << node_count_
                      << "  profondeur : " << current_node->depth
                      << "  colonnes : " << column_pool_.size()
                      << "  LB : " << lb
                      << "  UB : " << ub;
            if (ub < INF / 2.0 && lb > -INF / 2.0 && lb > 1e-6)
                std::cerr << "  gap : "
                          << (ub - lb) / lb * 100.0 << "%";
            std::cerr << std::endl;
        }

        // ── Élagage précoce par borne ─────────────────────────────────────
        if (current_node->lower_bound >= tree_.get_best_upper_bound() - 1e-5) {
            tree_.prune(current_node);
            current_node = tree_.get_next_node();
            continue;
        }

        // ── Passage à l'état du nœud courant ─────────────────────────────
        switch_state(current_node);

        // ── Génération de colonnes ────────────────────────────────────────
        run_column_generation(current_node);

        // ── Décision après CG ─────────────────────────────────────────────
        switch (current_node->status) {

            case NodeStatus::FRACTIONAL: {
                BranchingDecision dec = most_fractional_arc_branching();
                branch_on_node(current_node, dec);

                // Plongée DFS périodique UNIQUEMENT si on est déjà en mode BBF
                // (= on a déjà trouvé une première solution entière).
                //
                // NE PAS appeler set_strategy_DFS() en phase initiale :
                // set_strategy_DFS() VIDE la pile DFS (voir SearchTree.cpp:52),
                // ce qui détruit toutes les branches sœurs non encore explorées
                // et condamne le solveur à plonger indéfiniment sans jamais
                // trouver de solution entière (UB reste à +∞, LB à -∞).
                if (tree_.get_current_strategy() == SearchStrategy::BBF
                    && node_count_ % 100 == 0) {
                    std::cerr << "[MFAB] Plongee DFS pour diversifier (mode BBF)..." << std::endl;
                    tree_.set_strategy_DFS();
                }
                break;
            }

            case NodeStatus::INTEGER:
                std::cerr << "[MFAB] Solution entiere trouvee ! Valeur = "
                          << current_node->lower_bound << std::endl;
                // On passe en BBF pour prouver l'optimalité (explorer les
                // nœuds restants par ordre de borne inférieure croissante).
                tree_.set_strategy_BBF();
                break;

            case NodeStatus::PRUNED:
                break;

            case NodeStatus::INFEASIBLE:
                break;

            default:
                throw std::runtime_error("[MFABSolver] solve: statut de noeud inattendu apres CG");
        }

        current_node = tree_.get_next_node();
    }

    std::cerr << "[MFAB] Resolution terminee."
              << "  Noeuds explores : " << node_count_
              << "  Colonnes generees : " << column_pool_.size()
              << "  Valeur optimale : " << tree_.get_best_upper_bound()
              << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Accesseurs
// ─────────────────────────────────────────────────────────────────────────────

double MFABSolver::get_optimal_value() const {
    return tree_.get_best_upper_bound();
}

std::vector<Column> MFABSolver::get_optimal_solution() const {
    return tree_.get_incumbent_solution();
}
