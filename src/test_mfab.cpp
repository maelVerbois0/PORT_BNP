#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>

#include "GraphParser.h"
#include "GraphBuilder.h"
#include "TimeSpaceGraph.h"
#include "BranchAndPriceSolver.h"
#include "MFABSolver.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::milliseconds;

static void print_separator(char c = '-', int width = 60) {
    std::cerr << std::string(width, c) << std::endl;
}

static void print_solution_summary(const std::string& name,
                                   double value,
                                   int    nodes,
                                   int    columns,
                                   long   elapsed_ms)
{
    std::cerr << std::left  << std::setw(20) << name
              << std::right << std::setw(12) << std::fixed << std::setprecision(4) << value
              << std::setw(10) << nodes
              << std::setw(10) << columns
              << std::setw(10) << elapsed_ms << " ms"
              << std::endl;
}

// Vérifie que la solution retournée est cohérente :
// - une colonne par train
// - pas de chemin fictif (dummy) sauf si aucune autre solution
static bool validate_solution(const std::vector<Column>& sol,
                               const TimeSpaceGraph& graph,
                               int num_trains)
{
    if (sol.empty()) {
        std::cerr << "[VALIDATION] Solution vide." << std::endl;
        return false;
    }

    // Vérifier qu'on a exactement num_trains colonnes
    if (static_cast<int>(sol.size()) != num_trains) {
        std::cerr << "[VALIDATION] Nombre de colonnes incorrect : "
                  << sol.size() << " attendu " << num_trains << std::endl;
        return false;
    }

    // Vérifier qu'aucune colonne fictive n'est présente
    bool has_dummy = false;
    for (const Column& col : sol) {
        if (col.arc_ids.size() == 1) {
            int arc_id = col.arc_ids[0];
            if (graph.get_arc(arc_id).get_type() == DUMMY) {
                std::cerr << "[VALIDATION] Colonne fictive dans la solution pour le train "
                          << col.train_id << std::endl;
                has_dummy = true;
            }
        }
    }

    // Vérifier que chaque train est représenté une seule fois
    std::vector<int> train_count(num_trains + 1, 0);
    for (const Column& col : sol) {
        if (col.train_id < 1 || col.train_id > num_trains) {
            std::cerr << "[VALIDATION] train_id hors bornes : " << col.train_id << std::endl;
            return false;
        }
        train_count[col.train_id]++;
    }
    for (int k = 1; k <= num_trains; ++k) {
        if (train_count[k] != 1) {
            std::cerr << "[VALIDATION] Train " << k
                      << " apparaît " << train_count[k] << " fois dans la solution." << std::endl;
            return false;
        }
    }

    return !has_dummy;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {

    // ── Arguments ─────────────────────────────────────────────────────────
    std::string directory_path = (argc > 1)
        ? argv[1]
        : "../instances/small station/scenario 2/";

    // Mode de test :
    //   "mfab"    → MFAB uniquement
    //   "compare" → MFAB + Quantum côte à côte (plus long)
    std::string mode = (argc > 2) ? argv[2] : "mfab";

    print_separator('=');
    std::cerr << "  TEST MFAB — Most Fractional Arc-Flow Branching" << std::endl;
    std::cerr << "  Instance : " << directory_path << std::endl;
    std::cerr << "  Mode     : " << mode << std::endl;
    print_separator('=');

    try {
        // ── Parsing ───────────────────────────────────────────────────────
        std::cerr << "\n[1/3] Parsing..." << std::endl;
        InstanceParser parser(directory_path);
        InstanceData data = parser.parse();

        std::cerr << "      Trains    : " << data.trains.size()    << std::endl;
        std::cerr << "      Services  : " << data.services.size()  << std::endl;
        std::cerr << "      Nœuds phys: " << data.pnodes.size()    << std::endl;
        std::cerr << "      Liens     : " << data.plinks.size()    << std::endl;

        // ── Construction du graphe ─────────────────────────────────────────
        std::cerr << "\n[2/3] Construction du graphe temps-espace..." << std::endl;
        GraphBuilder builder(data);
        TimeSpaceGraph graph = builder.build();

        std::cerr << "      Nœuds virtuels : " << graph.get_nodes().size() << std::endl;
        std::cerr << "      Arcs virtuels  : " << graph.get_arcs().size()  << std::endl;

        // ─────────────────────────────────────────────────────────────────
        // BLOC 1 : MFAB Solver
        // ─────────────────────────────────────────────────────────────────
        std::cerr << "\n[3/3] Lancement du MFABSolver..." << std::endl;
        print_separator();

        double mfab_value   = -1.0;
        int    mfab_nodes   = 0;
        int    mfab_cols    = 0;
        long   mfab_time_ms = 0;

        {
            auto t0 = Clock::now();
            MFABSolver mfab_solver(graph, data.trains);
            mfab_solver.solve();
            auto t1 = Clock::now();

            mfab_value   = mfab_solver.get_optimal_value();
            mfab_nodes   = mfab_solver.get_node_count();
            mfab_cols    = mfab_solver.get_column_count();
            mfab_time_ms = std::chrono::duration_cast<Ms>(t1 - t0).count();

            std::cerr << "\n[MFAB] Valeur optimale    : " << mfab_value   << std::endl;
            std::cerr << "[MFAB] Nœuds B&P explorés : " << mfab_nodes   << std::endl;
            std::cerr << "[MFAB] Colonnes générées   : " << mfab_cols    << std::endl;
            std::cerr << "[MFAB] Temps total         : " << mfab_time_ms << " ms" << std::endl;

            // Validation de la solution
            const auto sol = mfab_solver.get_optimal_solution();
            bool valid = validate_solution(sol, graph, static_cast<int>(data.trains.size()));
            std::cerr << "[MFAB] Solution valide : " << (valid ? "OUI" : "NON") << std::endl;
        }

        // ─────────────────────────────────────────────────────────────────
        // BLOC 2 (optionnel) : Comparaison avec BranchAndPriceSolver
        // ─────────────────────────────────────────────────────────────────
        if (mode == "compare") {
            print_separator();
            std::cerr << "\n[COMPARE] Lancement du BranchAndPriceSolver (quantum)..." << std::endl;
            print_separator();

            double qbp_value   = -1.0;
            long   qbp_time_ms = 0;

            {
                auto t0 = Clock::now();
                BranchAndPriceSolver qbp_solver(graph, data.trains);
                qbp_solver.solve();
                auto t1 = Clock::now();

                qbp_value   = qbp_solver.get_optimal_value();
                qbp_time_ms = std::chrono::duration_cast<Ms>(t1 - t0).count();

                std::cerr << "\n[QBP] Valeur optimale : " << qbp_value   << std::endl;
                std::cerr << "[QBP] Temps total     : " << qbp_time_ms << " ms" << std::endl;
            }

            // ── Tableau récapitulatif ──────────────────────────────────────
            print_separator('=');
            std::cerr << std::left  << std::setw(20) << "Solveur"
                      << std::right << std::setw(12) << "Valeur opt."
                      << std::setw(10) << "Nœuds"
                      << std::setw(10) << "Colonnes"
                      << std::setw(13) << "Temps"
                      << std::endl;
            print_separator();
            print_solution_summary("MFAB",   mfab_value, mfab_nodes, mfab_cols, mfab_time_ms);
            print_solution_summary("Quantum", qbp_value, -1,         -1,        qbp_time_ms);
            print_separator('=');

            // Vérification de cohérence des valeurs optimales
            if (std::abs(mfab_value - qbp_value) > 1e-4) {
                std::cerr << "\n[ERREUR] Les valeurs optimales different de plus de 1e-4 !" << std::endl;
                std::cerr << "  MFAB   = " << mfab_value << std::endl;
                std::cerr << "  Quantum= " << qbp_value  << std::endl;
                return 2;
            } else {
                std::cerr << "\n[OK] Les deux solveurs trouvent la meme valeur optimale." << std::endl;
                if (mfab_time_ms < qbp_time_ms)
                    std::cerr << "[PERF] MFAB est "
                              << std::fixed << std::setprecision(1)
                              << static_cast<double>(qbp_time_ms) / std::max(1L, mfab_time_ms)
                              << "x plus rapide que Quantum." << std::endl;
                else
                    std::cerr << "[PERF] Quantum est "
                              << std::fixed << std::setprecision(1)
                              << static_cast<double>(mfab_time_ms) / std::max(1L, qbp_time_ms)
                              << "x plus rapide que MFAB." << std::endl;
            }
        }

    } catch (const std::runtime_error& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION inattendue] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}