#include <iostream>
#include <string>
#include <vector>

#include "GraphParser.h"
#include "GraphBuilder.h"
#include "TimeSpaceGraph.h"
#include "PricingProblem.h"
#include "MasterProblem.h"

int main(int argc, char** argv){
    std::string directory_path = (argc > 1) ? argv[1] : "../instances/small station/scenario 1/";

        std::cerr << "===========================================" << std::endl;
    std::cerr << "   TEST DE LA GENERATION DE COLONNES    " << std::endl;
    std::cerr << "===========================================" << std::endl;
    std::cerr << "Dossier cible : " << directory_path << "\n" << std::endl;

    int max_iter = 100;

    try{
        std::cerr << "[1/3] Parsing des fichiers CSV de l'instance..." << std::endl;
        InstanceParser parser(directory_path);
        InstanceData data = parser.parse();

        // --- ÉTAPE 2 : INITIALISATION DU BUILDER ---
        std::cerr << "\n[2/] Chargement des données dans le GraphBuilder..." << std::endl;
        GraphBuilder builder(data);

        // --- ÉTAPE 3 : CONSTRUCTION DU GRAPHE---
        std::cerr << "\n[3/] Application des règles métier et création du Graphe..." << std::endl;
        TimeSpaceGraph graph = builder.build();

        // --- ÉTAPE 4 : CONSTRUCTION DU MasterProblem---
        std::cerr << "\n[4/] Construction du Restricted Master Problem" << std::endl;
        MasterProblem rmp(graph, data.trains);

        // --- ÉTAPE 5 : CONSTRUCTION DU PRICER---
        std::cerr << "\n[4/] Construction du Pricer" << std::endl;
        PricingProblem pricer(graph, data.trains);

        // --- ÉTAPE 5 : GENERATION DE LA POOL DE COLONNES---
        ColumnPool pool = ColumnPool();

        //ETAPE 6 : GENERATION DE COLONNES
        std::vector<double> pi;
        std::vector<std::vector<double>> alpha;
        std::vector<std::vector<double>> mu;
        std::vector<Column> added_columns;
        for(int i = 0; i < max_iter; i++){
            rmp.solve();
            std::cerr << "Tentative de lecture des variables duales de flots " << endl;
            pi = rmp.get_flow_duals();
            std::cerr << "Tentative de lecture des variables duales de service" << endl;
            alpha = rmp.get_service_duals();
            std::cerr << "Tentative de lecture des variables duales de conflit" << endl;
            mu = rmp.get_conflict_duals();
            std::cerr << "Réussite de la lecture des variables" << endl;
            std::cerr << "Tentative d'ajout de colonnes" << endl;
            added_columns = pricer.solve(pi, alpha, mu);
            std::cerr << "Réussite d'ajout de colonnes" << endl;
            if(added_columns.size() == 0){
                break;
            }
            for(auto& col : added_columns){
                int new_id = pool.add_column(col);
                rmp.add_column(pool.get_column(new_id), graph);
            }

        }
        rmp.solve();
        double continuous_value = rmp.get_objective_value();
        // Résolution entière
        rmp.convert_to_integer();
        rmp.solve();
        double integer_value = rmp.get_objective_value();

        std::cerr << "Valeur de la relaxation continue à la fin de l'algorithme" << continuous_value << std::endl;
        std::cerr << "Valeur entière à la fin de l'algorithme" << integer_value << std::endl;
        std::cerr << "Gap : " << (integer_value - continuous_value)/integer_value << std::endl;


    }catch (const GRBException& e) {
    std::cerr << "\n💥 GUROBI EXCEPTION DETECTEE :" << std::endl;
    std::cerr << "Code d'erreur : " << e.getErrorCode() << std::endl;
    std::cerr << "Message : " << e.getMessage() << std::endl;
    return 1;
}

    return 0;

}