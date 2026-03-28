#include "BranchAndPriceSolver.h"
#include "SearchTree.h"
#include "PricingProblem.h"
#include "MasterProblem.h"
#include <map>
#include <cmath>
#include <algorithm>
#include <stdexcept>

BranchingDecision BranchAndPriceSolver::choose_branching_variable() const {
    return quantum_train_branching();
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

    BranchingDecision decision;
    decision.right_train_ids.resize(1);
    decision.left_train_ids.resize(1);
    std::vector<int> partition_A;
    std::vector<int> partition_B;
    
    // 2. On cherche un train qui effectue ces services à deux instants différents
    for(const auto &[train_id, paths] : train_fractional_paths){
        const Train& current_train = trains_.at(train_id);
        std::vector<int> first_known_time_for_service(current_train.get_nb_services_required(), -1);
        for(const auto& path: paths){
            for(int arc_id : path.col->arc_ids){
                const auto& arc = graph_.get_arc(arc_id);
                if(arc.get_service() != -1){
                    first_known_time_for_service[arc.get_service()] = arc.get_start_time();
                }
            }
        }

    }

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
 void BranchAndPriceSolver::run_column_generation(BPNode* node){
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
        std::vector<Column> add_cols = pricing_.solve(flow_duals, service_duals, conflict_duals, state_manager_);

        if(add_cols.empty()){  
            std::cerr << "The solve ended with status: " << master_.get_gurobi_status() << std::endl;
            if (master_.is_sol_integer()){
                node->status=NodeStatus::INTEGER;
                node->lower_bound = master_.get_objective_value();
                if(tree_.get_best_upper_bound() > master_.get_objective_value()){
                    const std::vector<int> active_id = master_.get_active_columns_ids();
                    const std::vector<Column> incumbent = column_pool_.get_columns(active_id);
                    tree_.set_incumbent_solution(master_.get_objective_value(), incumbent);
                }
            }
            if(master_.is_sol_fractional()){
                node->lower_bound = master_.get_objective_value();
                node->status = NodeStatus::FRACTIONAL;
                node->lower_bound = master_.get_objective_value();
                if(node->lower_bound > tree_.get_best_upper_bound() - 1e-5){
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
    int iter_DFS;
    while(current_node != nullptr){
        iter++;

        if(iter%100 == 0 && tree_.get_current_strategy() == SearchStrategy::BBF){
            std::cerr << "Let's try a little dive to find a new solution" << std::endl;
            tree_.set_strategy_DFS();
            iter_DFS = 0;
        }
        if(tree_.get_current_strategy() == SearchStrategy::DFS){
            iter_DFS++;
            if(iter_DFS > 50){
                tree_.set_strategy_BBF();
            }
        }

        std::cerr << "Nombre d'itérations : " << iter << std::endl;
        std::cerr << "Current depth : " << current_node->depth << std::endl;
        std:cerr << "Best known lower bound : " << tree_.get_best_lower_bound() << std::endl;
        std::cerr << "Best known upper bound : " << tree_.get_best_upper_bound()<< std::endl;
        std::cerr << "Gap : " << (tree_.get_best_upper_bound() - tree_.get_best_lower_bound())/tree_.get_best_lower_bound() * 100<<"%"<<std::endl;
        if(current_node->lower_bound >= tree_.get_best_upper_bound()){
            tree_.prune(current_node);
            current_node = tree_.get_next_node();
            continue;
        }

        switch_state(current_node);
        //Running the integer model to get some integer solution
        if(current_node->depth %20 == 0 && current_node->depth >=19){
            master_.convert_to_integer();
            master_.solve();
            if(master_.is_infeasible()){
                continue;
            }
            if(master_.get_objective_value() < tree_.get_best_upper_bound()){
                const std::vector<int> active_id = master_.get_active_columns_ids();
                const std::vector<Column> incumbent = column_pool_.get_columns(active_id);
                tree_.set_incumbent_solution(master_.get_objective_value(), incumbent);

            }
            master_.convert_to_continuous();
        }
        std::cerr << "Running Column Generation..." << std::endl;
        run_column_generation(current_node);
        
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
    }
}

double BranchAndPriceSolver::get_optimal_value() const {
    return tree_.get_best_upper_bound();
}

std::vector<Column> BranchAndPriceSolver::get_optimal_solution() const{
    return tree_.get_incumbent_solution();
}