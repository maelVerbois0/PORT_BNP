#include "SearchTree.h"
#include <stdexcept>

// Constructeur par défaut (Arbre vide)
SearchTree::SearchTree():
    best_upper_bound_(INF),
    best_lower_bound_(-INF),
    current_strategy_(SearchStrategy::DFS),
    current_node_(nullptr)
{

    auto root = std::make_unique<BPNode>(
        nullptr,                    // parent
        std::vector<BPNode*>(),     // children
        0,                          // depth
        -INF,                       // lower_bound
        NodeStatus::UNPROCESSED,    // status
        -1,                         // train_id
        std::vector<int>()          // forbidden_arcs_ids
    );
    
    current_node_ = root.get();
    add_node_to_queues(root.get());
    all_generated_node_.push_back(std::move(root)); // L'arbre prend possession de la racine
}

// Constructeur avec solution initiale (Warm start)
SearchTree::SearchTree(const std::vector<Column>& incumbent_solution, double value):
    incumbent_solution_(incumbent_solution),
    best_upper_bound_(value),
    best_lower_bound_(-INF),
    current_strategy_(SearchStrategy::DFS),
    current_node_(nullptr)
{
    std::unique_ptr<BPNode> root = std::make_unique<BPNode>(
        nullptr, std::vector<BPNode*>(), 0, -INF, NodeStatus::UNPROCESSED, -1, std::vector<int>()
    );
    
    current_node_ = root.get();
    add_node_to_queues(root.get());
    all_generated_node_.push_back(std::move(root));
}

// --- Navigation et Stratégie ---

void SearchTree::set_strategy_DFS() {
    current_strategy_ = SearchStrategy::DFS;
    open_node_DFS_ = std::stack<BPNode*>(); 
}

void SearchTree::set_strategy_BBF() {
    current_strategy_ = SearchStrategy::BBF;
}

BPNode* SearchTree::get_next_node() {
    if (current_strategy_ == SearchStrategy::DFS) {
        while (!open_node_DFS_.empty()) {
            BPNode* candidate = open_node_DFS_.top();
            open_node_DFS_.pop();
            
            // On ignore les nœuds élagués !
            if (candidate->status != NodeStatus::PRUNED) {
                current_node_ = candidate;
                return candidate;
            }
        }
    } else { // Stratégie BBF
        while (!open_node_BBF_.empty()) {
            BPNode* candidate = open_node_BBF_.top();
            open_node_BBF_.pop();
            
            if (candidate->status != NodeStatus::PRUNED) {
                current_node_ = candidate;
                return candidate;
            }
        }
    }
    
    current_node_ = nullptr;
    return nullptr; // L'arbre a été entièrement exploré
}

// --- Construction de l'arbre ---

BPNode* SearchTree::create_child_node(BPNode* parent, int train_id, const std::vector<int>& additional_forbidden_arcs) {
    if (parent == nullptr) throw std::runtime_error("Cannot create a child without a parent.");


    // 2. Création du nœud
    auto new_node = std::make_unique<BPNode>(
        parent,                     
        std::vector<BPNode*>(),     
        parent->depth + 1,          
        parent->lower_bound,        
        NodeStatus::UNPROCESSED,    
        train_id,                   
        additional_forbidden_arcs               
    );


    BPNode* raw_ptr = new_node.get();
    parent->children.push_back(raw_ptr);


    add_node_to_queues(raw_ptr);
    all_generated_node_.push_back(std::move(new_node)); 

    return raw_ptr;
}

void SearchTree::add_node_to_queues(BPNode* raw_node) {
    open_node_DFS_.push(raw_node);
    open_node_BBF_.push(raw_node);
}


void SearchTree::prune(BPNode* node) {
    if (node == nullptr || node->status == NodeStatus::PRUNED) return;

    node->status = NodeStatus::PRUNED;

    node->forbidden_arcs_ids.clear(); 
    node->forbidden_arcs_ids.shrink_to_fit();

    for (BPNode* child : node->children) {
        prune(child); 
    }
}

// --- Getters & Setters ---

double SearchTree::get_best_upper_bound() const { return best_upper_bound_; }
double SearchTree::get_best_lower_bound() const { return best_lower_bound_; }
SearchStrategy SearchTree::get_current_strategy() const { return current_strategy_; }
std::vector<Column> SearchTree::get_incumbent_solution() const { return incumbent_solution_; }

void SearchTree::set_best_upper_bound(double upper_bound) {
    if (upper_bound > best_upper_bound_) {
        throw std::runtime_error("Trying to set an upper bound that is worse than the current one.");
    }
    best_upper_bound_ = upper_bound;
}

void SearchTree::set_best_lower_bound(double lower_bound) {
    if (lower_bound < best_lower_bound_) {
        throw std::runtime_error("Trying to set a lower bound that is worse than the current one.");
    }
    best_lower_bound_ = lower_bound;
}

void SearchTree::set_incumbent_solution(double upper_bound, const std::vector<Column>& solution) {
    set_best_upper_bound(upper_bound);
    incumbent_solution_ = solution;
}


Trajectory SearchTree::trajectory_to_node(BPNode* target_node) {
    Trajectory traj;
    if (current_node_ == target_node) return traj;

    // 1. Chemins vers la racine
    std::vector<BPNode*> path_current;
    BPNode* temp = current_node_;
    while (temp != nullptr) {
        path_current.push_back(temp);
        temp = temp->parent;
    }

    std::vector<BPNode*> path_target;
    temp = target_node;
    while (temp != nullptr) {
        path_target.push_back(temp);
        temp = temp->parent;
    }

    // 2. Trouver le Plus Proche Ancêtre Commun (LCA)
    BPNode* lca = nullptr;
    int i = path_current.size() - 1;
    int j = path_target.size() - 1;
    
    while (i >= 0 && j >= 0 && path_current[i] == path_target[j]) {
        lca = path_current[i];
        i--; j--;
    }

    // 3. Remplir les nœuds à annuler (de current_node_ jusqu'au LCA exclu)
    temp = current_node_;
    while (temp != lca && temp != nullptr) {
        traj.nodes_to_revert.push_back(temp);
        temp = temp->parent;
    }

    // 4. Remplir les nœuds à appliquer (du LCA exclu jusqu'au target_node)
    for (int k = j; k >= 0; k--) {
        traj.nodes_to_apply.push_back(path_target[k]);
    }

    // 5. Mise à jour de l'état interne de l'arbre
    current_node_ = target_node;

    return traj;
}


BPNode* SearchTree::get_current_node() const {
    return current_node_;
}