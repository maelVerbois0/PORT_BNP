#include "MFAB2solver.h"   // ← à corriger en "MFABSolver.h"#include <unordered_map>
#include <unordered_map> 

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <map>
#include <vector>
// =============================================================================
// Construction / colonnes
// =============================================================================






// ── Paramètres de recherche (ajustables) ─────────────────────────────────────
// MAX_DFS_DEPTH : profondeur maximale de plongée DFS avant de basculer en BBF.
// Le DFS est efficace pour trouver rapidement une première solution entière
// (incumbent) mais sur les grandes instances, le LP reste fractionnaire très
// profondément car la génération de colonnes produit toujours de nouvelles
// colonnes réelles évitant les arcs interdits. Limiter la profondeur force
// le basculement en BBF qui explore des nœuds moins contraints (moins profonds)
// où la structure LP converge plus vite vers une solution entière.
static constexpr int  MAX_DFS_DEPTH = 60;
static constexpr int  MAX_NODES         = 5000; // sécurité anti-boucle infinie

// =============================================================================
// Construction / colonnes
// =============================================================================

MFABSolver::MFABSolver(const TimeSpaceGraph& graph, const std::vector<Train>& trains)
    : graph_(graph), trains_(trains),
      master_(graph, trains),
      pricing_(graph, trains),
      state_manager_(static_cast<int>(trains.size()),
                     static_cast<int>(graph.get_arcs().size())),
      tree_(), column_pool_()
{
    for (size_t k = 0; k < trains_.size(); ++k) {
        Column dummy(
            static_cast<int>(k),
            { graph_.get_dummy_arc_id(static_cast<int>(k)) },
            std::vector<bool>(graph_.get_nb_services(), true),
            graph_.get_arc(graph_.get_dummy_arc_id(static_cast<int>(k))).get_cost(static_cast<int>(k)),
            static_cast<int>(k + 1)
        );
        add_Column(dummy);
    }
}

int MFABSolver::add_Column(Column& col) {
    int id = column_pool_.add_column(col);
    master_.add_column(col, graph_);
    state_manager_.register_column(col);
    return id;
}

// =============================================================================
// Switch d'état dans l'arbre
// =============================================================================

void MFABSolver::switch_state(BPNode* target) {
    Trajectory traj = tree_.trajectory_to_node(target);
    for (BPNode* n : traj.nodes_to_revert) {
        if (n->train_ids.empty())
            throw std::runtime_error("[MFABSolver] switch_state: revert root node");
        master_.enable_columns(state_manager_.revert_delta(n->train_ids, n->forbidden_arcs_ids));
    }
    for (BPNode* n : traj.nodes_to_apply)
        master_.disable_columns(state_manager_.apply_delta(n->train_ids, n->forbidden_arcs_ids));
}

// =============================================================================
// Génération de colonnes
// =============================================================================

void MFABSolver::run_column_generation(BPNode* node) {
    if (node->status != NodeStatus::UNPROCESSED)
        throw std::runtime_error("[MFABSolver] CG: node already processed");
    node->status = NodeStatus::PROCESSING;

    while (true) {
        master_.solve();
        if (!master_.is_infeasible() && !master_.is_optimal())
            throw std::runtime_error("[MFABSolver] CG: unexpected Gurobi status");

        if (master_.is_infeasible()) {
            node->status = NodeStatus::INFEASIBLE;
            node->lower_bound = -INF;
            break;
        }

        auto new_cols = pricing_.solve(
            master_.get_flow_duals(),
            master_.get_service_duals(),
            master_.get_conflict_duals(),
            state_manager_
        );

        if (new_cols.empty()) {
            node->lower_bound = master_.get_objective_value();
            if (master_.is_sol_integer()) {
                node->status = NodeStatus::INTEGER;
                if (tree_.get_best_upper_bound() > node->lower_bound)
                    tree_.set_incumbent_solution(node->lower_bound,
                        column_pool_.get_columns(master_.get_active_columns_ids()));
            } else {
                node->status = NodeStatus::FRACTIONAL;
                if (node->lower_bound > tree_.get_best_upper_bound() - 1e-5)
                    node->status = NodeStatus::PRUNED;
            }
            break;
        }
        for (auto& col : new_cols) add_Column(col);
    }
}

bool MFABSolver::is_current_solution_integer() const {
    switch (tree_.get_current_node()->status) {
        case NodeStatus::INTEGER:    return true;
        case NodeStatus::FRACTIONAL: return false;
        case NodeStatus::INFEASIBLE: return false;
        default: throw std::runtime_error("[MFABSolver] bad node status");
    }
}

void MFABSolver::branch_on_node(BPNode* parent, const BranchingDecision& dec) {
    tree_.create_child_node(parent, dec.left_train_ids,  dec.left_forbidden_arcs);
    tree_.create_child_node(parent, dec.right_train_ids, dec.right_forbidden_arcs);
}

// =============================================================================
// NIVEAU 1 — Service-Location Branching (SLB) [inspiré Ryan & Foster]
//
// Une solution LP fractionnaire peut affecter le service s du train k
// fractionnellement entre plusieurs nœuds physiques p1, p2, ...
// On sélectionne la paire (k, s, p) la plus fractionnaire et on branche :
//   Fils G : service s de k NE SE FAIT PAS à p  → forbid SERVICE arcs AT p
//   Fils D : service s de k SE FAIT à p         → forbid SERVICE arcs NOT at p
//
// Analogue Ryan & Foster : "items i et j dans le même bin / séparés".
// Convergence garantie en O(K × S × P) décisions max (P fini).
// =============================================================================

std::optional<BranchingDecision> MFABSolver::service_location_branching() const {

    const int n_trains = static_cast<int>(trains_.size());
    const int n_arcs   = static_cast<int>(graph_.get_arcs().size());

    using SP = std::pair<int,int>;  // (service_id 1-idx, phys_node 1-idx)
    std::vector<std::map<SP, double>> slflow(n_trains);

    for (int col_id : master_.get_active_columns_ids()) {
        double val = master_.get_column_value(col_id);
        if (val < 1e-5) continue;
        const Column& col = column_pool_.get_column(col_id);
        int k = col.train_id - 1;
        for (int a_id : col.arc_ids) {
            const Arc& arc = graph_.get_arc(a_id);
            if (arc.get_type() != SERVICE) continue;
            int s = arc.get_service();
            int p = graph_.get_node(arc.get_from()).getPNode();
            slflow[k][{s, p}] += val;
        }
    }

    double best_score = INF;
    int    best_k = -1, best_s = -1, best_p = -1;
    double best_flow = 0.0;

    for (int k = 0; k < n_trains; ++k) {
        for (const auto& [sp, f] : slflow[k]) {
            if (f < 1e-5 || f > 1.0 - 1e-5) continue;
            double score = frac_score(f);
            if (score < best_score) {
                best_score = score; best_k = k;
                best_s = sp.first; best_p = sp.second;
                best_flow = f;
            }
        }
    }

    if (best_k < 0) return std::nullopt;

    const int train_id = best_k + 1;

    std::vector<int> left_forbidden, right_forbidden;
    for (int a_id = 0; a_id < n_arcs; ++a_id) {
        const Arc& arc = graph_.get_arc(a_id);
        if (arc.get_type() != SERVICE)   continue;
        if (arc.get_service() != best_s) continue;
        if (!graph_.arc_accessible_by_train(best_k, a_id)) continue;
        if (state_manager_.is_arc_forbidden(train_id, a_id)) continue;
        int phys = graph_.get_node(arc.get_from()).getPNode();
        if (phys == best_p) left_forbidden.push_back(a_id);
        else                right_forbidden.push_back(a_id);
    }

    if (left_forbidden.empty() && right_forbidden.empty()) return std::nullopt;

    std::cerr << "[SLB] svc=" << best_s << " train=" << train_id
              << " @p=" << best_p << " f=" << best_flow
              << " G:" << left_forbidden.size() << " D:" << right_forbidden.size()
              << std::endl;

    BranchingDecision dec;
    dec.left_train_ids  = { train_id };
    dec.right_train_ids = { train_id };
    dec.left_forbidden_arcs[train_id]  = std::move(left_forbidden);
    dec.right_forbidden_arcs[train_id] = std::move(right_forbidden);
    return dec;
}

// =============================================================================
// NIVEAU 2 — Common-Prefix Arc Branching (CPB) [fallback routage]
//
// Trouve le premier nœud UNIVERSEL du DAG (tous les chemins fractionnaires
// d'un train le traversent et y divergent). Brancher là est exhaustif.
// Fils G : arc dominant interdit.
// Fils D : TOUS les autres arcs depuis ce nœud interdits (getFromArcs —
//          arcs réels, pas seulement actifs → évite les nouvelles colonnes
//          de contournement générées par la CG).
// =============================================================================

BranchingDecision MFABSolver::most_fractional_arc_branching() const {

    const int n_trains = static_cast<int>(trains_.size());

    struct FracPath {
        double flow;
        std::unordered_map<int,int> node_to_arc;
    };

    std::vector<std::vector<FracPath>> frac(n_trains);

    for (int col_id : master_.get_active_columns_ids()) {
        double val = master_.get_column_value(col_id);
        if (val < 1e-5 || val > 1.0 - 1e-5) continue;
        const Column& col = column_pool_.get_column(col_id);
        int k = col.train_id - 1;
        FracPath fp; fp.flow = val;
        for (int a_id : col.arc_ids)
            fp.node_to_arc[graph_.get_arc(a_id).get_from()] = a_id;
        frac[k].push_back(std::move(fp));
    }

    double best_score    = INF;
    int    best_k        = -1;
    int    best_div_node = -1;
    int    best_arc_left = -1;

    for (int k = 0; k < n_trains; ++k) {
        if (frac[k].size() < 2) continue;
        const auto& paths = frac[k];
        int cur = 0;

        while (cur != 1) {
            std::map<int,double> arc_flow;
            bool all_pass = true;
            for (const auto& p : paths) {
                auto it = p.node_to_arc.find(cur);
                if (it == p.node_to_arc.end()) { all_pass = false; break; }
                arc_flow[it->second] += p.flow;
            }
            if (!all_pass) break;
            if (arc_flow.size() >= 2) {
                double total = 0;
                for (auto& [a,f] : arc_flow) total += f;
                for (auto& [a,f] : arc_flow) {
                    double sc = frac_score(f / total);
                    if (sc < best_score) {
                        best_score = sc; best_k = k;
                        best_div_node = cur; best_arc_left = a;
                    }
                }
                break;
            }
            cur = graph_.get_arc(arc_flow.begin()->first).get_to();
        }
    }

    if (best_k < 0)
        throw std::runtime_error(
            "[CPB] Aucun noeud de divergence universel. "
            "La solution devrait etre entiere ou SLB aurait du s'en charger.");

    const int train_id = best_k + 1;

    std::vector<int> right_forbidden;
    for (int b : graph_.get_node(best_div_node).getFromArcs()) {
        if (b == best_arc_left) continue;
        if (!graph_.arc_accessible_by_train(best_k, b)) continue;
        if (state_manager_.is_arc_forbidden(train_id, b)) continue;
        right_forbidden.push_back(b);
    }

    std::cerr << "[CPB] noeud=" << best_div_node << " train=" << train_id
              << " arc=" << best_arc_left << " score=" << best_score
              << " D:" << right_forbidden.size() << std::endl;

    BranchingDecision dec;
    dec.left_train_ids  = { train_id };
    dec.right_train_ids = { train_id };
    dec.left_forbidden_arcs[train_id]  = { best_arc_left };
    dec.right_forbidden_arcs[train_id] = std::move(right_forbidden);
    return dec;
}

// =============================================================================
// Dispatcher
// =============================================================================

BranchingDecision MFABSolver::choose_branching() const {
    auto slb = service_location_branching();
    if (slb.has_value()) return slb.value();
    return most_fractional_arc_branching();
}

// =============================================================================
// Boucle principale solve()
//
// Stratégie adaptative :
// ─────────────────────
// Phase 1 (DFS limité à MAX_DFS_DEPTH) : plonger rapidement pour trouver
//   un premier incumbent (UB). Limiter la profondeur est CRUCIAL pour les
//   grandes instances où le LP reste fractionnaire très profondément car la
//   génération de colonnes produit continuellement de nouvelles colonnes réelles
//   évitant les arcs interdits (la colonne dummy — toujours disponible et jamais
//   désactivable — ne force jamais le LP à être integer/infeasible sur le chemin
//   droit du DFS).
//
// Phase 2 (BBF après dépassement de depth OU après premier incumbent) :
//   Traiter les nœuds dans l'ordre de leur borne inférieure.
//   set_strategy_BBF() transfère TOUS les nœuds DFS en attente vers la file BBF.
//   Les nœuds peu profonds (faible LB) sont traités en priorité — ils ont moins
//   de contraintes et convergent plus vite vers des solutions entières.
// =============================================================================

void MFABSolver::solve() {
    BPNode* cur = tree_.get_next_node();

    while (cur != nullptr && node_count_ < MAX_NODES) {
        ++node_count_;

        // ── Log de progression ─────────────────────────────────────────────
        if (node_count_ % 50 == 0) {
            double ub = tree_.get_best_upper_bound();
            double lb = tree_.get_best_lower_bound();
            std::string mode = (tree_.get_current_strategy() == SearchStrategy::DFS)
                               ? "DFS" : "BBF";
            std::cerr << "[MFAB|" << mode << "] n=" << node_count_
                      << " depth=" << cur->depth
                      << " cols=" << column_pool_.size()
                      << " LB_local=" << cur->lower_bound  // ← LB réel du nœud courant
                      << " LB_global=" << lb
                      << " UB=" << ub;
            if (ub < INF/2 && lb > -INF/2 && lb > 1e-6)
                std::cerr << " gap=" << (ub-lb)/lb*100.0 << "%";
            std::cerr << std::endl;
        }

        // ── Élagage précoce ────────────────────────────────────────────────
        if (cur->lower_bound >= tree_.get_best_upper_bound() - 1e-5) {
            tree_.prune(cur);
            cur = tree_.get_next_node();
            continue;
        }

        switch_state(cur);
        run_column_generation(cur);

        switch (cur->status) {

            case NodeStatus::FRACTIONAL: {
                branch_on_node(cur, choose_branching());

                // ── Gestion de la stratégie ────────────────────────────────
                //
                // RÈGLE CRITIQUE : ne JAMAIS appeler set_strategy_DFS() pendant
                // la phase DFS initiale. set_strategy_DFS() VIDE la pile (SearchTree.cpp:52),
                // ce qui détruit tous les nœuds frères en attente → profondeur infinie.
                //
                // On bascule en BBF dans deux cas seulement :
                //   a) On a trouvé un premier incumbent (après INTEGER) → optimisation
                //   b) La profondeur DFS dépasse MAX_DFS_DEPTH sans incumbent.
                //      Raison : sur les grandes instances, le LP reste fractionnaire
                //      très profondément (nouvelles colonnes réelles générées à chaque
                //      nœud). BBF explore des nœuds peu profonds (moins contraints)
                //      qui convergent plus vite vers des solutions entières.

                bool no_incumbent = (tree_.get_best_upper_bound() > INF / 2.0);
                bool dfs_too_deep = (tree_.get_current_strategy() == SearchStrategy::DFS
                                     && cur->depth >= MAX_DFS_DEPTH
                                     && no_incumbent);

                if (dfs_too_deep) {
                    std::cerr << "[MFAB] DFS trop profond (depth=" << cur->depth
                              << " > " << MAX_DFS_DEPTH << "), bascule en BBF. "
                              << "Les noeuds DFS en attente sont transferes en BBF "
                              << "et traites par ordre de borne inferieure." << std::endl;
                    tree_.set_strategy_BBF();
                }
                // Plongée DFS périodique seulement en BBF (pour diversifier)
                else if (tree_.get_current_strategy() == SearchStrategy::BBF
                         && node_count_ % 150 == 0) {
                    std::cerr << "[MFAB] Plongee DFS periodique (BBF mode)..." << std::endl;
                    tree_.set_strategy_DFS();
                }
                break;
            }

            case NodeStatus::INTEGER:
                std::cerr << "[MFAB] Solution entiere! valeur=" << cur->lower_bound
                          << "  noeuds=" << node_count_
                          << "  colonnes=" << column_pool_.size() << std::endl;
                // Passer en BBF pour améliorer la borne et prouver l'optimalité
                tree_.set_strategy_BBF();
                break;

            case NodeStatus::PRUNED:
            case NodeStatus::INFEASIBLE:
                break;

            default:
                throw std::runtime_error("[MFABSolver] solve: statut inattendu apres CG");
        }

        cur = tree_.get_next_node();
    }

    if (node_count_ >= MAX_NODES)
        std::cerr << "[MFAB] Limite de " << MAX_NODES << " noeuds atteinte." << std::endl;

    std::cerr << "[MFAB] Termine. noeuds=" << node_count_
              << " colonnes=" << column_pool_.size()
              << " UB=" << tree_.get_best_upper_bound()
              << " LB=" << tree_.get_best_lower_bound() << std::endl;
}

// =============================================================================
// Accesseurs
// =============================================================================

double MFABSolver::get_optimal_value() const {
    return tree_.get_best_upper_bound();
}

std::vector<Column> MFABSolver::get_optimal_solution() const {
    return tree_.get_incumbent_solution();
}