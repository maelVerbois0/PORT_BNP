#pragma once
#include <vector>
#include "Column.h"
#include "gurobi_c++.h"

class MasterProblem{
    public:
        MasterProblem(int num_trains, int num_arcs);
        ~MasterProblem();

        void solve();
        bool is_optimal() const;
        bool is_infeasible() const;
        double get_objective_value() const;
        std::vector<double> get_service_duals() const; // Valeurs des variables duales pour chaque contraintes sur la réalisation d'un service s pour un train k
        std::vector<double> get_conflict_duals() const; //Valeurs des variables duales associées à l'incompatibilité entre les chemins pour une positition physique p à l'instant t
        void add_column(const Column& col);
        void set_column_upper_bound(int column_id, double upper_bound);
        void is_solution_integer() const;
        std::vector<int> get_active_column_id() const;
    private:
        int num_trains_;
        int num_arcs_;
        std::unique_ptr<GRBEnv> env_;
        std::unique_ptr<GRBModel> model_;
        std::vector<GRBVar> lp_columns_;
        std::vector<GRBVar> dummy_vars_;

        void build_initial_model();
};