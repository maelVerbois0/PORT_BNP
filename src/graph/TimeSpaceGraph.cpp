#include "TimeSpaceGraph.h"
#include "Node.h"
#include "Arc.h"
#include <vector>

using namespace std;

TimeSpaceGraph::TimeSpaceGraph(const int time_horizon, const int time_step, const int num_physical_nodes):T_(time_horizon),t_s_(time_step),num_pnodes_(num_physical_nodes)
{   
}


void TimeSpaceGraph::add_node(const Node& node){
    nodes_.push_back(node);
}

void TimeSpaceGraph::add_arc(const Arc& arc){
    arcs_.push_back(arc);
}


const vector<Node>& TimeSpaceGraph::get_nodes() const{
    return nodes_;
}

const std::vector<Arc>& TimeSpaceGraph::get_arcs() const{
    return arcs_;
}

const Node& TimeSpaceGraph::get_node(int id) const{
    return nodes_.at(id);
}

const Arc& TimeSpaceGraph::get_arc(int id) const{
    return arcs_.at(id);
}

int TimeSpaceGraph::compute_virtual_node_id(int p, int t, int l) const{
    if (p == 0)
    {
        return l - 1;
    }
    return (p - 1) * 3 + (t / t_s_) * num_pnodes_ * 3 + l + 2;
}

int TimeSpaceGraph::compute_virtual_node_id(int p, int t, int l, int time_step, int num_pnodes) {
    if (p == 0) {
        return l - 1;
    }
    return (p - 1) * 3 + (t / time_step) * num_pnodes * 3 + l + 2;
}


