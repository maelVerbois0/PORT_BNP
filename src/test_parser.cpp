#include <iostream>
#include <string>
#include "GraphParser.h" // Assure-toi que le chemin d'inclusion est correct

int main(int argc, char** argv) {
    // Par défaut, on utilise un chemin de test, ou on prend l'argument passé en ligne de commande
    std::string directory_path = (argc > 1) ? argv[1] : "../instances/small station/scenario 1/";

    std::cout << "=== Démarrage du test du Parser ===" << std::endl;
    std::cout << "Dossier cible : " << directory_path << std::endl;

    try {
        InstanceParser parser(directory_path);
        InstanceData data = parser.parse();

        std::cout << "\n✅ Parsing réussi avec succès !" << std::endl;
        std::cout << "--- Résumé des données extraites ---" << std::endl;
        std::cout << "Nombre de services    : " << data.services.size() << std::endl;
        std::cout << "Nombre de types de nœud: " << data.node_types.size() << std::endl;
        std::cout << "Nombre de nœuds (pnodes): " << data.pnodes.size() << std::endl;
        std::cout << "Nombre de liens (plinks): " << data.plinks.size() << std::endl;
        std::cout << "Nombre de trains      : " << data.trains.size() << std::endl;
        std::cout << "Horizon de temps (T)  : " << data.max_time << std::endl;

    } catch (const std::runtime_error& e) {
        std::cerr << "\n❌ Erreur critique de parsing :\n" << e.what() << std::endl;
        return 1; // Code d'erreur
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Erreur inattendue :\n" << e.what() << std::endl;
        return 1;
    }

    return 0;
}