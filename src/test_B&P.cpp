#include <iostream>
#include <string>
#include <vector>

#include "GraphParser.h"
#include "GraphBuilder.h"
#include "TimeSpaceGraph.h"
#include "PricingProblem.h"
#include "MasterProblem.h"
#include "SearchTree.h"
#include "BranchAndPriceSolver.h"


int main(int argc, char** argv){
    std::string directory_path = (argc > 1) ? argv[1] : "../instances/small station/scenario 4/";

        std::cerr << "===========================================" << std::endl;
    std::cerr << "   TEST B&P    " << std::endl;
    std::cerr << "===========================================" << std::endl;
    std::cerr << "Dossier cible : " << directory_path << "\n" << std::endl;

        try{
        std::cerr << "[1/] Parsing des fichiers CSV de l'instance..." << std::endl;
        InstanceParser parser(directory_path);
        InstanceData data = parser.parse();

        // --- ÉTAPE 2 : INITIALISATION DU BUILDER ---
        std::cerr << "\n[2/] Chargement des données dans le GraphBuilder..." << std::endl;
        GraphBuilder builder(data);

        // --- ÉTAPE 3 : CONSTRUCTION DU GRAPHE---
        std::cerr << "\n[3/] Application des règles métier et création du Graphe..." << std::endl;
        TimeSpaceGraph graph = builder.build();

        // --- ÉTAPE 4 : CONSTRUCTION DU SOLVEUR---
        std::cerr << "\n[4/] Construction du solveur à partir des données..." << std::endl;
        BranchAndPriceSolver solver(graph, data.trains);
        
        // --- ÉTAPE 5 : LANCEMENT DU SOLVEUR---
        std::cerr << "\n[5/] Lancement du solveur..." << std::endl;
        solver.solve();
      

    }catch (const std::runtime_error& e) {
    std::cerr << "\n EXCEPTION DETECTEE :" << std::endl;
    std::cerr << "Erreur : " << e.what() << std::endl;
    return 1;
    }

    return 0;
}