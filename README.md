## Projet du Cours PORT 2026 - MPRO

### 1. Build (Gurobi required)
Hello everybody

Prereqs:
- `g++` and `make` available in PATH
- `GUROBI_HOME` set (headers/libs must be discoverable)
  - If you have a different Gurobi version, update `CPPLIB` in `Makefile` to match your library name (e.g. `-lgurobi120`, `-lgurobi130`, etc.).

Build (Legacy):
```
make clean
make
```
Build (Prefered)
```
mkdir build
cd build
cmake ..
```

Headers are located under `src/header/` and the `Makefile` already adds this include path.

### Run (column generation)
```
tusp_solver "<station name>" <scenario_number> [time_budget_cg_seconds] [max_iters] [time_budget_binary_ilp_seconds]
```
Arguments (optional):
- `time_budget_cg_seconds`: time limit for column generation (default `7200`)
- `max_iters`: maximum CG iterations (default `1000`)
- `time_budget_binary_ilp_seconds`: time limit for the final binary master solve (default `0` = no limit)

Examples:
```
./tusp_solver "small station" 1 7200 1000 300
```

## Main entry point
The executable is built from `src/tusp_column_generation.cpp`. It:
- parses CLI arguments and resolves the instance CSV paths
- reads station + scenario inputs 
- builds a 3-layer time-space network (nodes + arcs) with incompatibility sets
- runs column generation (arc-path formulation) with per-train shortest-path pricing using Gurobi
- solves a final binary master on generated columns and writes logs/summaries for visualization

## Code map and extension points
Use this map to locate where to implement the guideline improvements.

| File | What it does | Where to implement improvements |
| --- | --- | --- |
| `src/tusp_column_generation.cpp` | Program entry point, CLI parsing, orchestration of input, network build, CG loop, final binary solve, outputs. | Add new CLI flags or experiment switches (e.g., time limits, pricing options, stabilization parameters). |
| `src/header/input_reader.h`, `src/input_reader.cpp` | CSV parsing and validation for services, node types, nodes, links, train types, trains, and operations. |  |
| `src/header/network_builder.h`, `src/network_builder.cpp` | Builds the time-space network: nodes, arcs, incompatibilities, and train-specific arc availability. | Modeling changes that alter the network structure, arc costs or incompatibility logic. |
| `src/header/Arc.h`, `src/Arc.cpp` | Arc type definitions, costs, reduced-cost storage. |  |
| `src/header/Path.h`, `src/Path.cpp` | Path representation and Gurobi variable construction. | Column management features (e.g., bounds, metadata for pruning, stabilized pricing). |
| `src/header/Arc-path_formulation.h`, `src/Arc-path_formulation.cpp` | Master problem, dual extraction, reduced-cost network, pricing, and binary solve. | Most algorithmic changes: exactness (branch-and-price scaffolding), initialization heuristics, pricing speed (multi-column per train), stabilization, column removal, model tightening and parallel pricing. |
| `src/header/path_builder.h`, `src/path_builder.cpp` | Rebuilds train paths from chosen columns, verifies flow, checks service completion, prints paths. | Post-solve validation and reporting. Adjust checks if allowing partial service completion. |
| `src/header/params.h` | Global constants: safety headway and cost coefficients. |  |
| `src/header/Logger.h`, `src/Logger.cpp` | Logging to file for visualization and debugging. | Add metrics for performance analysis. |
| `python/visualize_results.py` | Post-run visualization from logs and instance CSVs. | Extend plots for new metrics. |

Recommended starting points for the guidelines:
- Exactness (branch-and-price): `src/Arc-path_formulation.cpp` (master/pricing), plus orchestration in `src/tusp_column_generation.cpp`.
- Initialization heuristics: `initialize_master_AP` in `src/Arc-path_formulation.cpp`, or a new helper invoked from `src/tusp_column_generation.cpp`.
- Pricing speed and multiple columns per train: `pricing_algorithm` and `shortest_path_algorithm_rcsp` in `src/Arc-path_formulation.cpp`.
- Column management and stabilization: master construction and dual handling in `src/Arc-path_formulation.cpp`, plus column metadata in `src/Path.cpp`.
- Model tightening or valid inequalities: constraint definitions in `initialize_master_AP` (`src/Arc-path_formulation.cpp`).
- Parallelization: the per-train pricing loop in `pricing_algorithm` (`src/Arc-path_formulation.cpp`).
- Partial service completion: service constraints in `initialize_master_AP` and pricing logic in `src/Arc-path_formulation.cpp`, with validation adjustments in `src/path_builder.cpp`.


## Outputs
Each run generates the following files (paths are relative to the repo root):
- `results_<station>_scenario_<scenario>.txt` (summary of instance + solution)
- `iterations_pricing.csv` (per-iteration summary: new paths, time, stop reason)
- `iterations_pricing_reduced_costs.csv` (per-iteration best reduced cost per train)
- `mip1.log` (Gurobi log)
- `solutions_log/<station>_scenario_<scenario>/logfile.log` (logger output for visualization)
- `solutions_log/<station>_scenario_<scenario>/output.txt` (stdout used by the visualization scripts)

## Results visualisation
Use the helper script to visualize the paths and station layout from the generated logs:
```
python python/visualize_results.py "small station" 1
```
This reads:
`solutions_log/<station>_scenario_<scenario>/logfile.log` and
`solutions_log/<station>_scenario_<scenario>/output.txt`,
and also uses the station CSVs from `instances/<station>/` to build the
station graph (Node/Link/Node types) for plotting.

Optional override:
```
python python/visualize_results.py "small station" 1 --instance-dir "instances/small station"
```
and saves PDFs under:
`visualizations/<station>_scenario_<scenario>/`.
