#include <iostream>
#include <string>
#include <vector>

#include "GraphParser.h"
#include "GraphBuilder.h"
#include "TimeSpaceGraph.h"

int main(int argc, char** argv) {
    // Par défaut, cherche le scenario 1, sinon prend l'argument passé en ligne de commande
    std::string directory_path = (argc > 1) ? argv[1] : "../instances/small station/scenario 1/";

    std::cout << "===========================================" << std::endl;
    std::cout << "   TEST DE LA PIPELINE TIME-SPACE GRAPH    " << std::endl;
    std::cout << "===========================================" << std::endl;
    std::cout << "Dossier cible : " << directory_path << "\n" << std::endl;

    try {
        // --- ÉTAPE 1 : PARSING ---
        std::cout << "[1/3] Parsing des fichiers CSV de l'instance..." << std::endl;
        InstanceParser parser(directory_path);
        InstanceData data = parser.parse();

        // --- ÉTAPE 2 : INITIALISATION DU BUILDER ---
        std::cout << "\n[2/3] Chargement des données dans le GraphBuilder..." << std::endl;
        GraphBuilder builder(data);

        // --- ÉTAPE 3 : CONSTRUCTION ---
        std::cout << "\n[3/3] Application des règles métier et création du Graphe..." << std::endl;
        TimeSpaceGraph graph = builder.build();

        // --- ÉTAPE 4 : BILAN ---
        std::cout << "\n✅ SUCCES : Graphe généré et sécurisé en mémoire !\n" << std::endl;
        
        std::cout << "--- Statistiques Finales ---" << std::endl;
        std::cout << "Pas de temps (t_s)  : " << graph.get_time_step() << std::endl;
        std::cout << "Horizon exact (T)   : " << graph.get_horizon() << std::endl;
        std::cout << "Nombre de Nœuds     : " << graph.get_nodes().size() << std::endl;
        std::cout << "Nombre d'Arcs       : " << graph.get_arcs().size() << std::endl;

        // Petit test de cohérence (optionnel)
        if (!graph.get_nodes().empty()) {
            std::cout << "\n--- Test de cohérence des données ---" << std::endl;
            std::cout << "Nœud Initial (Source) : ID = " << graph.get_nodes().front().getID() 
                      << ", Temps = " << graph.get_nodes().front().getTime() << std::endl;
            std::cout << "Nœud Final (Puits)    : ID = " << graph.get_nodes()[1].getID() 
                      << ", Temps = " << graph.get_nodes()[1].getTime() << std::endl;
            std::cout << "Dernier arc généré    : ID = " << graph.get_arcs().back().get_id() 
                      << " (Type: " << graph.get_arcs().back().get_type() << ")" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "\n❌ ERREUR CRITIQUE PENDANT L'EXECUTION :\n" << e.what() << std::endl;
        return 1;
    }

    return 0;
}