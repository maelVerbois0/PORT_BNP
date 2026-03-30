#include "BranchAndPriceSolver.h"
#include "SearchTree.h"
#include "PricingProblem.h"
#include "MasterProblem.h"
#include <map>
#include <cmath>
#include <algorithm>
#include <vector>
#include <numeric>
#include <stdexcept>
#include <stack>

BranchingDecision BranchAndPriceSolver::choose_branching_variable() const {
    try {
        return service_split_branching();
        
    } catch (const std::runtime_error& e) {
        std::cerr << "[Branching] Service split a echoue (" << e.what() 
                  << ") -> Fallback sur Quantum Train Branching." << std::endl;
        return quantum_train_branching();
    }
}

BranchingDecision BranchAndPriceSolver::service_split_branching() const {
    struct FractionalPath {
        const Column* col;
        double flow;
    };

    // 1. Récupération des chemins fractionnaires triés par train
    std::map<int, std::vector<FractionalPath>> train_fractional_paths;
    
    std::vector<int> active_ids = master_.get_active_columns_ids();
    for (int id : active_ids) {
        double val = master_.get_column_value(id);
        if (val > 1e-5 && val < 1.0 - 1e-5) {
            const Column& col = column_pool_.get_column(id);
            train_fractional_paths[col.train_id].push_back({&col, val});
        }
    }

    if (train_fractional_paths.empty()) {
        throw std::runtime_error("Trying to apply branching on an integer solution !");
    }

    BranchingDecision decision;
    decision.right_train_ids.resize(1);
    decision.left_train_ids.resize(1);
    
    // Variables pour garder la trace de la meilleure coupure trouvée
    int best_train_id = -1;
    int best_service_id = -1;
    int best_split_time = -1;
    double best_score = 1e9; // On cherche à minimiser ce score

    // 2. Recherche du meilleur candidat pour le branchement temporel
    for (const auto &[train_id, paths] : train_fractional_paths) {
        
        // Structure: Service_ID -> (Start_Time -> Cumulative Flow)
        // std::map trie naturellement les clés (les temps), ce qui est parfait pour nous !
        std::map<int, std::map<int, double>> service_times_flow;

        for (const auto& path : paths) {
            for (int arc_id : path.col->arc_ids) {
                const auto& arc = graph_.get_arc(arc_id);
                // Si c'est un arc de service, on enregistre la quantité de flux à cet instant
                // (Assure-toi que type enum SERVICE correspond bien à ton implémentation)
                if (arc.get_type() == SERVICE) { 
                    int s_id = arc.get_service();
                    // Assure-toi d'avoir un getter type get_start_time() sur ton Arc
                    service_times_flow[s_id][arc.get_start_time()] += path.flow; 
                }
            }
        }

        // On évalue la qualité de chaque coupure possible pour chaque service
        for (const auto& [s_id, time_flow] : service_times_flow) {
            // S'il n'y a qu'un seul instant de départ, on ne peut pas couper !
            if (time_flow.size() > 1) {
                
                double total_flow = 0.0;
                for (const auto& [time, flow] : time_flow) total_flow += flow;
                
                double cumulative_flow = 0.0;
                
                // On s'arrête à l'avant-dernier élément car on évalue la coupure "après" cet élément
                for (auto it = time_flow.begin(); std::next(it) != time_flow.end(); ++it) {
                    cumulative_flow += it->second;
                    
                    // 1er Objectif : Être proche de 50/50 (déséquilibre le plus proche de 0)
                    double balance_penalty = std::abs(cumulative_flow - (total_flow / 2.0));
                    
                    // 2ème Objectif : Maximiser le gap temporel entre cette option et la suivante
                    int next_time = std::next(it)->first;
                    int time_gap = next_time - it->first;
                    
                    // Le score final (on divise par le gap pour que les grands gaps réduisent le score)
                    double score = balance_penalty / static_cast<double>(time_gap);
                    
                    if (score < best_score) {
                        best_score = score;
                        best_train_id = train_id;
                        best_service_id = s_id;
                        best_split_time = it->first;
                    }
                }
            }
        }
    }

    // Sécurité : Si aucun service n'est fractionné temporellement
    if (best_train_id == -1) {
        throw std::runtime_error("Aucun service fractionne temporellement trouve. Il faut utiliser une autre regle (ex: quantum_train_branching) en secours !");
    }

// 3. Application de la décision
    decision.left_train_ids = {best_train_id};
    decision.right_train_ids = {best_train_id};
    
    std::vector<int> partition_A; // Arcs interdits pour la branche Gauche
    std::vector<int> partition_B; // Arcs interdits pour la branche Droite

    // OPTIMISATION : On cible directement et uniquement les arcs du service concerné
    const std::vector<int>& target_arcs = graph_.get_arcs_providing_service(best_service_id);
    
    for (int arc_id : target_arcs) {
        const Arc& arc = graph_.get_arc(arc_id);
        
        // BRANCHE GAUCHE : Le train DOIT faire le service <= best_split_time
        // -> On lui interdit tous les arcs de ce service qui partent STRICTEMENT APRES.
        if (arc.get_start_time() > best_split_time) {
            partition_A.push_back(arc_id);
        }
        
        // BRANCHE DROITE : Le train DOIT faire le service > best_split_time
        // -> On lui interdit tous les arcs de ce service qui partent AVANT OU PENDANT.
        if (arc.get_start_time() <= best_split_time) {
            partition_B.push_back(arc_id);
        }
    }

    decision.left_forbidden_arcs[best_train_id] = partition_A;
    decision.right_forbidden_arcs[best_train_id] = partition_B;

    return decision;
}


BranchingDecision BranchAndPriceSolver::quantum_train_branching() const {
        
    // Structure locale pour stocker temporairement nos chemins fractionnaires
    struct FractionalPath {
        const Column* col;
        double flow;
    };
    
    // 1. Récupération des chemins fractionnaires triés par train
    std::map<int, std::vector<FractionalPath>> train_fractional_paths;
    
    std::vector<int> active_ids = master_.get_active_columns_ids();
    for (int id : active_ids) {
        double val = master_.get_column_value(id);
        // On cible strictement les variables fractionnaires (exclut les 0 et les 1)
        if (val > 1e-5 && val < 1.0 - 1e-5) {
            const Column& col = column_pool_.get_column(id);
            train_fractional_paths[col.train_id].push_back({&col, val});
        }
    }

    // Si tout est entier (ou vide), on s'arrête là !
    if (train_fractional_paths.empty()) {
        throw std::runtime_error("Trying to apply branching on an integer solution !");
    }

    BranchingDecision best_decision;
    best_decision.right_train_ids.resize(1);
    best_decision.left_train_ids.resize(1);
    std::vector<int> best_partition_A;
    std::vector<int> best_partition_B;
    int best_train_id;


    // 2. Recherche du noeud de divergence pour chaque train
    double best_train_score = 1e9;
    for (const auto& [train_id, paths] : train_fractional_paths) {
        if (paths.empty()) continue;

        double total_fractional_flow = 0.0;
        // Parcours chronologique des arcs pour trouver la première divergence
        int divergence_idx = 0;
        bool diverged = false;
        
        for (int i = 0; i < paths[0].col->arc_ids.size(); ++i) {
            int first_arc = paths[0].col->arc_ids[i];
            // On vérifie si tous les chemins passent par cet arc à l'étape i
            for (const auto& p : paths) {
                if (p.col->arc_ids[i] != first_arc) {
                    diverged = true;
                    break;
                }
            }
            if (diverged) {
                divergence_idx = i;
                break;
            }
        }

        // 3. Collecte des arcs sortants au point de divergence et de leurs flux
        std::map<int, double> outgoing_arc_flows;
        if (diverged) {
            for (const auto& p : paths) {
                outgoing_arc_flows[p.col->arc_ids[divergence_idx]] += p.flow;
            }
        } else {
            throw std::runtime_error("Solution fractionnaire sans divergence, il y a probablement plusieurs fois la même colonne qui a été ajoutée !");
        }

        
        

        // 4. Partitionnement 50/50 avec Heuristique de Packing (Gloutonne)
        
        // On stocke les paires {flux, arc_id} pour pouvoir les trier
        std::vector<std::pair<double, int>> sorted_arcs;
        for (const auto& [arc, f] : outgoing_arc_flows) {
            sorted_arcs.push_back({f, arc});
        }

        // Étape A : On trie les arcs par flux décroissant (les plus gros blocs en premier)
        std::sort(sorted_arcs.begin(), sorted_arcs.end(), 
                  [](const std::pair<double, int>& a, const std::pair<double, int>& b) {
                      return a.first > b.first; 
                  });

        double current_sum_A = 0.0;
        double current_sum_B = 0.0;
        std::vector<int> partition_A;
        std::vector<int> partition_B;

        // Étape B : On distribue chaque arc dans le tas le plus "léger" à l'instant T
        for (const auto& item : sorted_arcs) {
            double flow = item.first;
            int arc_id = item.second;

            if (current_sum_A <= current_sum_B) {
                current_sum_A += flow;
                partition_A.push_back(arc_id);
            } else {
                current_sum_B += flow;
                partition_B.push_back(arc_id);
            }
        }

        // On évalue à quel point l'équilibre est parfait (idéalement l'écart est 0)
        double score = std::abs(current_sum_A - current_sum_B);

        // On sauvegarde si c'est la meilleure partition trouvée 
        if (score < best_train_score) {
            best_train_score = score;
            best_train_id = train_id;
            best_partition_A = partition_A;
            best_partition_B = partition_B;
        }

    }
    best_decision.left_train_ids = {best_train_id};
    best_decision.right_train_ids = {best_train_id};
    best_decision.left_forbidden_arcs[best_train_id] = best_partition_A;
    best_decision.right_forbidden_arcs[best_train_id] = best_partition_B;

    return best_decision;
}

int BranchAndPriceSolver::add_Column(Column& col){
    int new_id = column_pool_.add_column(col);
    master_.add_column(col, graph_);
    state_manager_.register_column(col);
    return new_id;
}

//Initialisation des différentes parties ensemble. Pour être sur que tout est à jour à l'initialisation on fait bien attention à intialiser
//les colonnes correspondant au dummy_var dans le global state manager et la column pool (elles se font à la construction du rmp)
BranchAndPriceSolver::BranchAndPriceSolver(const TimeSpaceGraph& graph, const std::vector<Train>& trains)
    : 
    graph_(graph),
    trains_(trains),
    master_(graph, trains),
    pricing_(graph, trains),
    state_manager_(trains.size(), graph.get_arcs().size()),
    tree_(),
    column_pool_() 
{

    for(size_t k = 0; k < trains_.size(); k++){
        Column dummy_col = Column(k, {graph_.get_dummy_arc_id(k)}, std::vector<bool>(graph_.get_nb_services(), true), graph_.get_arc(graph_.get_dummy_arc_id(k)).get_cost(k), k);
        add_Column(dummy_col);
    }
}

void BranchAndPriceSolver::branch_on_node(BPNode* parent, const BranchingDecision& decision){
    tree_.create_child_node(parent, decision.left_train_ids, decision.left_forbidden_arcs);
    tree_.create_child_node(parent, decision.right_train_ids, decision.right_forbidden_arcs);
}

void BranchAndPriceSolver::switch_state(BPNode* target){
    Trajectory trajectory = tree_.trajectory_to_node(target);
    for(BPNode* to_rev: trajectory.nodes_to_revert){
        if(to_rev->train_ids.size() == 0){
            throw std::runtime_error("Trying to revert the root node. There is likely a problem with the computation of the trajectory to switch state in SearchTree");
        }
        ColumnList enable = state_manager_.revert_delta(to_rev->train_ids, to_rev->forbidden_arcs_ids);
        master_.enable_columns(enable);
    }
    for(BPNode* to_app: trajectory.nodes_to_apply){
        ColumnList disable = state_manager_.apply_delta(to_app->train_ids, to_app->forbidden_arcs_ids);
        master_.disable_columns(disable);
    }

}


bool BranchAndPriceSolver::is_current_solution_integer() const{
    switch(tree_.get_current_node()->status)
    {
        case(NodeStatus::FRACTIONAL):
            return false;
        case(NodeStatus::INTEGER):
            return true;
        case(NodeStatus::INFEASIBLE):
            return false;
        case(NodeStatus::PROCESSING):
            throw std::runtime_error("Trying to determine the state of the solution of a processing node");
        case(NodeStatus::PRUNED):
            throw std::runtime_error("Trying to determine the state of a pruned node");
        case(NodeStatus::UNPROCESSED):
            throw std::runtime_error("Trying to determine the state of an unprocessed node");
    }
}


//The column generation algorithm suppose the state has been altered to reflect the node trying to be processed
 void BranchAndPriceSolver::run_column_generation(BPNode* node, double termination_gap = 0.0){
    if(node->status != NodeStatus::UNPROCESSED){
        throw std::runtime_error("Trying to run column generation on an already processed or processing node !");
    }
    node->status = NodeStatus::PROCESSING;
    while(true){
        master_.solve();
        if(!master_.is_infeasible() && !master_.is_optimal()){
            throw std::runtime_error("Error while trying to solve the master problem at a given node");
        }

        if(master_.is_infeasible()){
            std::cerr << "Master problem is genuinely INFEASIBLE at this node." << std::endl;
            node->status = NodeStatus::INFEASIBLE;
            node->lower_bound = -INF;
            break;
        }

        
        const std::vector<double> flow_duals = master_.get_flow_duals();
        const std::vector<std::vector<double>> service_duals = master_.get_service_duals();
        const std::vector<std::vector<double>> conflict_duals = master_.get_conflict_duals();
        auto [add_cols, best_reduced_costs] = pricing_.solve(flow_duals, service_duals, conflict_duals, state_manager_);
        double lagrangian_lower_bound = master_.get_objective_value() + std::accumulate(best_reduced_costs.begin(), best_reduced_costs.end(), 0.0);
        double lagrangian_gap = std::abs((master_.get_objective_value() - lagrangian_lower_bound)/ lagrangian_lower_bound);
        if(lagrangian_lower_bound > tree_.get_best_upper_bound() + 1e-5){
            std::cerr << "La borne lagrangienne est au dessus de la meilleure solution connue." << std::endl;
            node->status = NodeStatus::PRUNED;
            node->lower_bound = lagrangian_lower_bound;
            return;
        }

        std::cerr << "Borne inférieure de la génération de colonne actuelle :" << lagrangian_lower_bound << endl;
        std::cerr << "Gap par rapport à la valeur courante du rmp : " << lagrangian_gap * 100 << "%" << std::endl;
        if(add_cols.empty() || lagrangian_gap <= termination_gap){  
            std::cerr << "The solve ended with status: " << master_.get_gurobi_status() << std::endl;
            if (master_.is_sol_integer()){
                node->status=NodeStatus::INTEGER;
                node->lower_bound = lagrangian_lower_bound;
                if(tree_.get_best_upper_bound() > master_.get_objective_value()){
                    const std::vector<int> active_id = master_.get_active_columns_ids();
                    const std::vector<Column> incumbent = column_pool_.get_columns(active_id);
                    tree_.set_incumbent_solution(master_.get_objective_value(), incumbent);
                }
            }
            if(master_.is_sol_fractional()){
                node->lower_bound = lagrangian_lower_bound;
                node->status = NodeStatus::FRACTIONAL;
                if(node->lower_bound > tree_.get_best_upper_bound() + 1e-5){
                    node->status = NodeStatus::PRUNED;
                }
            }
            break;
        }

        for(auto& col : add_cols){
            add_Column(col);
        }

    }
}


void BranchAndPriceSolver::solve(){
    BPNode* current_node = tree_.get_next_node();
    int iter = 0;
    std::vector<double> termination_gaps_column_generation = {0.0, 0.00, 0.00, 0.0};
    double time_limit_static_heuristic = 20.0;
    while(current_node != nullptr){
        if(current_node->lower_bound + 1e-6 >= tree_.get_best_upper_bound()){
            tree_.prune(current_node);
            current_node = tree_.get_next_node();
            continue;
        }

        
        //Remarque il est essentiel de lancer les heuristiques avant la génération de colonnes et le branchement car ces heuristiques modifient l'état du master_problem (activation désactivation de colonnes valeurs duales différentes pour les contraintes etc)
        //Cela rend donc caduque l'accès aux attributs du solveur qui ne peuvent être récupérés que après un solve 
        if(iter % 5 == 0 && iter >= 10){
            run_global_mip_heuristic(time_limit_static_heuristic);
        }
        if((iter - 1) % 5 == 0 && iter >=1){
            run_diving_heuristic(*current_node);
        }


        std::cerr << "Nombre d'itérations : " << iter << std::endl;
        std::cerr << "Current depth : " << current_node->depth << std::endl;
        std::cerr << "Best known lower bound : " << tree_.get_best_lower_bound() << std::endl;
        std::cerr << "Best known upper bound : " << tree_.get_best_upper_bound()<< std::endl;
        std::cerr << "Gap : " << (tree_.get_best_upper_bound() - tree_.get_best_lower_bound())/tree_.get_best_lower_bound() * 100<<"%"<<std::endl;

        switch_state(current_node);
        std::cerr << "Running Column Generation..." << std::endl;
        if(current_node->depth == 0){
            run_column_generation(current_node, termination_gaps_column_generation[0]);
        }
        else if(current_node->depth <= 5){
            run_column_generation(current_node, termination_gaps_column_generation[1]);
        }
        else if(current_node->depth <= 10){
            run_column_generation(current_node, termination_gaps_column_generation[2]);
        }
        else{
            run_column_generation(current_node,termination_gaps_column_generation[3]);
        }
        
        
        if(current_node->status == NodeStatus::FRACTIONAL){
            std::cerr << "Found a fractionnal solution branching" << std::endl;
            BranchingDecision decision = choose_branching_variable();
            branch_on_node(current_node, decision);
        }
        else if (current_node -> status == NodeStatus::PRUNED){
            std::cerr << "Pruning a node..." << std::endl;
        }
        else if (current_node -> status == NodeStatus::INTEGER){
            std::cerr << "Found an integer solution !!!" << std::endl;
            std::cerr << "It's value : "<<current_node->lower_bound << std::endl;
            std::cerr << "We'll stop the dive and start trying to improve the bound" << std::endl;
            tree_.set_strategy_BBF();
        }
        current_node = tree_.get_next_node();
        iter++;
    }
}

double BranchAndPriceSolver::get_optimal_value() const {
    return tree_.get_best_upper_bound();
}

std::vector<Column> BranchAndPriceSolver::get_optimal_solution() const{
    return tree_.get_incumbent_solution();
}


void BranchAndPriceSolver::run_global_mip_heuristic(double time_limit_seconds) {
    std::cerr << "\n--- [HEURISTIQUE GLOBALE] Lancement sur TOUT le pool de colonnes ---" << std::endl;

    // On récupère toutes les colonnes qui sont actuellement interdites par le noeud courant
    std::vector<int> cols_to_restore{};
    for(int column_id = 0; column_id < column_pool_.size(); column_id++){
        if(state_manager_.is_column_disabled(column_id)){
            cols_to_restore.push_back(column_id);
        }
    }

    master_.enable_columns(cols_to_restore);

    master_.convert_to_integer();
    master_.set_time_limit(time_limit_seconds);
    master_.set_focus_to_finding_sol();

    master_.solve();
    
    if (!master_.is_infeasible() &&  master_.get_current_nb_solutions() > 0) {
        double heuristic_val = master_.get_objective_value();
        std::cerr << "[HEURISTIQUE GLOBALE] Solution entiere trouvee ! Cout : " << heuristic_val << std::endl;
        
        if (heuristic_val < tree_.get_best_upper_bound()) {
            std::cerr << "[HEURISTIQUE GLOBALE] NOUVEL INCUMBENT !" << std::endl;
            // On extrait les variables à 1
            std::vector<int> current_sol_column_id = master_.get_active_columns_ids();
            std::vector<Column> best_sol= column_pool_.get_columns(current_sol_column_id);
            tree_.set_incumbent_solution(heuristic_val, best_sol);
        }
    } else {
        std::cerr << "[HEURISTIQUE GLOBALE] Aucune solution trouvee dans le temps imparti." << std::endl;
    }
    
    master_.set_time_limit(GRB_INFINITY);
    master_.set_focus_to_auto();
    master_.convert_to_continuous();
    master_.disable_columns(cols_to_restore);
    
    std::cerr << "--- [HEURISTIQUE GLOBALE] Fin. Etat du noeud courant restaure ---\n" << std::endl;
}


struct DiveStep {
    std::vector<int> train_ids;
    std::map<int, std::vector<int>> forbidden_arcs;
};

void BranchAndPriceSolver::run_diving_heuristic(BPNode base_node) {
    std::cerr << "\n[DIVING] Debut de la plongee heuristique..." << std::endl;
    
    //1. INTIALISATION
    std::stack<DiveStep> dive_history;
    int depth_dive = 0;

    while (true) {
        depth_dive++;
        std::cerr << "  -> Niveau de plongee : " << depth_dive << std::endl;

        // 2. GÉNÉRATION DE COLONNES (Mode Rapide) 
        run_column_generation(&base_node, 0.05); 
        //On est obligé de redeclarer le noeud comme non traité. C'est un peu moche il vaudrait mieux faire une fonction de génération de colonnes dédiée mais flemme pour l'instant
        base_node.status = NodeStatus::UNPROCESSED;
        if (base_node.lower_bound > tree_.get_best_upper_bound()){
            std::cerr << "Solution courante plus chère que la meilleur incumbent on arrête l'heuristique" << std::endl;
            break;
        }
        // 3. VÉRIFICATIONS DE FIN DE PLONGÉE
        if (master_.is_infeasible()) {
            std::cerr << "[DIVING] Echec : Infaisabilite atteinte. Fin de la plongee." << std::endl;
            break;
        }
        
        if (master_.is_sol_integer()) {
            double val = master_.get_objective_value();
            std::cerr << "[DIVING] SUCCES ! Solution entiere trouvee : " << val << std::endl;
            
            if (val < tree_.get_best_upper_bound()) {
                std::cerr << "[DIVING] Nouvel Incumbent valide !" << std::endl;
                std::vector<int> incumbent_cols_ids = master_.get_active_columns_ids();
                std::vector<Column> incumbent = column_pool_.get_columns(incumbent_cols_ids);
                tree_.set_incumbent_solution(val, incumbent);
            }
            break; 
        }
        
        // 4. SÉLECTION DE LA VARIABLE À ARRONDIR
        int best_col_id = -1;
        double best_val = -1;
        
        for (int id : master_.get_active_columns_ids()) {
            double val = master_.get_column_value(id);

            // On cherche la fraction la plus proche de 1.0 (on ignore les entiers purs)
            if (val > 1e-5 && val < 1.0 - 1e-5) {
                if (val > best_val) {
                    best_val = val;
                    best_col_id = id;
                }
            }
        }
        
        if (best_col_id == -1) {
            throw std::runtime_error (" [DIVING] Aucune variable fractionnaire exploitable alors que l'intégrité a été testé en amont."); 
            return;
        }
        
        // 5. APPLICATION DE LA RESTRICTION (Fixing)
        const Column& chosen_col = column_pool_.get_column(best_col_id);
        int target_train = chosen_col.train_id;
        
        std::vector<int> arcs_to_forbid;
        
        // On interdit au train cible tous les arcs qui ne font pas partie de la colonne choisie
        for (const Arc& arc : graph_.get_arcs()) {
            auto it = std::find(chosen_col.arc_ids.begin(), chosen_col.arc_ids.end(), arc.get_id());
            if (it == chosen_col.arc_ids.end()) {
                arcs_to_forbid.push_back(arc.get_id());
            }
        }
        
        // Préparation du delta pour cette étape précise
        std::vector<int> step_train_ids = {target_train};
        std::map<int, std::vector<int>> step_delta_map;
        step_delta_map[target_train] = arcs_to_forbid;
        
        // Application au StateManager et Master
        ColumnList disabled_cols = state_manager_.apply_delta(step_train_ids, step_delta_map);
        master_.disable_columns(disabled_cols);
        
        // SAUVEGARDE DANS LA PILE
        dive_history.push({step_train_ids, step_delta_map});
        
        std::cerr << "  -> Train " << target_train << " fixe sur la colonne " << best_col_id << std::endl;

    } 
    
    // 6. Dépilage des modifications
    std::cerr << "[DIVING] Nettoyage : Restauration de l'etat pas-a-pas..." << std::endl;
    
    while (!dive_history.empty()) {
        DiveStep last_step = dive_history.top();
        dive_history.pop();
        ColumnList enabled_cols = state_manager_.revert_delta(last_step.train_ids, last_step.forbidden_arcs);     
        master_.enable_columns(enabled_cols);
        
    }
    std::cerr << "[DIVING] Plongee terminee, compteurs restaurés a l'identique.\n" << std::endl;
}