#include "vamana_index.h"
#include "distance.h"
#include "io_utils.h"
#include "timer.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <unordered_map>
#include <stdexcept>
#include <cstdlib>

// ============================================================================
// Destructor
// ============================================================================

VamanaIndex::~VamanaIndex() {
    if (owns_data_ && data_) {
        std::free(data_);
        data_ = nullptr;
    }
}

// ============================================================================
// Greedy Search (generalized to start from any entry node)
// ============================================================================

std::pair<std::vector<VamanaIndex::Candidate>, uint32_t>
VamanaIndex::greedy_search_from(const float* query, uint32_t L, uint32_t entry_node) const {
    std::set<Candidate> candidate_set;
    std::vector<bool> visited(npts_, false);
    uint32_t dist_cmps = 0;

    float start_dist = compute_l2sq(query, get_vector(entry_node), dim_);
    dist_cmps++;
    candidate_set.insert({start_dist, entry_node});
    visited[entry_node] = true;

    std::set<uint32_t> expanded;

    while (true) {
        uint32_t best_node = UINT32_MAX;
        for (const auto& [dist, id] : candidate_set) {
            if (expanded.find(id) == expanded.end()) {
                best_node = id;
                break;
            }
        }
        if (best_node == UINT32_MAX)
            break;

        expanded.insert(best_node);

        std::vector<uint32_t> neighbors;
        {
            std::lock_guard<std::mutex> lock(locks_[best_node]);
            neighbors = graph_[best_node];
        }
        for (uint32_t nbr : neighbors) {
            if (visited[nbr])
                continue;
            visited[nbr] = true;

            float d = compute_l2sq(query, get_vector(nbr), dim_);
            dist_cmps++;

            if (candidate_set.size() < L) {
                candidate_set.insert({d, nbr});
            } else {
                auto worst = std::prev(candidate_set.end());
                if (d < worst->first) {
                    candidate_set.erase(worst);
                    candidate_set.insert({d, nbr});
                }
            }
        }
    }

    std::vector<Candidate> results(candidate_set.begin(), candidate_set.end());
    return {results, dist_cmps};
}

// Original greedy_search delegates to greedy_search_from
std::pair<std::vector<VamanaIndex::Candidate>, uint32_t>
VamanaIndex::greedy_search(const float* query, uint32_t L) const {
    return greedy_search_from(query, L, start_node_);
}

// ============================================================================
// Greedy Search with Shared State (inject new entry into existing search)
// ============================================================================

uint32_t VamanaIndex::greedy_search_inject(const float* query, uint32_t L,
                                            uint32_t entry_node,
                                            std::set<Candidate>& candidate_set,
                                            std::vector<bool>& visited,
                                            std::set<uint32_t>& expanded) const {
    uint32_t dist_cmps = 0;

    if (!visited[entry_node]) {
        visited[entry_node] = true;
        float d = compute_l2sq(query, get_vector(entry_node), dim_);
        dist_cmps++;

        if (candidate_set.size() < L) {
            candidate_set.insert({d, entry_node});
        } else {
            auto worst = std::prev(candidate_set.end());
            if (d < worst->first) {
                candidate_set.erase(worst);
                candidate_set.insert({d, entry_node});
            }
        }
    }

    while (true) {
        uint32_t best_node = UINT32_MAX;
        for (const auto& [dist, id] : candidate_set) {
            if (expanded.find(id) == expanded.end()) {
                best_node = id;
                break;
            }
        }
        if (best_node == UINT32_MAX)
            break;

        expanded.insert(best_node);

        std::vector<uint32_t> neighbors;
        {
            std::lock_guard<std::mutex> lock(locks_[best_node]);
            neighbors = graph_[best_node];
        }
        for (uint32_t nbr : neighbors) {
            if (visited[nbr])
                continue;
            visited[nbr] = true;

            float d = compute_l2sq(query, get_vector(nbr), dim_);
            dist_cmps++;

            if (candidate_set.size() < L) {
                candidate_set.insert({d, nbr});
            } else {
                auto worst = std::prev(candidate_set.end());
                if (d < worst->first) {
                    candidate_set.erase(worst);
                    candidate_set.insert({d, nbr});
                }
            }
        }
    }

    return dist_cmps;
}

// ============================================================================
// Robust Prune (Alpha-RNG Rule)
// ============================================================================

void VamanaIndex::robust_prune(uint32_t node, std::vector<Candidate>& candidates,
                               float alpha, uint32_t R) {
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
                       [node](const Candidate& c) { return c.second == node; }),
        candidates.end());

    std::sort(candidates.begin(), candidates.end());

    std::vector<uint32_t> new_neighbors;
    new_neighbors.reserve(R);

    for (const auto& [dist_to_node, cand_id] : candidates) {
        if (new_neighbors.size() >= R)
            break;

        bool keep = true;
        for (uint32_t selected : new_neighbors) {
            float dist_cand_to_selected =
                compute_l2sq(get_vector(cand_id), get_vector(selected), dim_);
            if (dist_to_node > alpha * dist_cand_to_selected) {
                keep = false;
                break;
            }
        }

        if (keep)
            new_neighbors.push_back(cand_id);
    }

    graph_[node] = std::move(new_neighbors);
}

// ============================================================================
// Build
// ============================================================================

void VamanaIndex::build(const std::string& data_path, uint32_t R, uint32_t L,
                        float alpha, float gamma) {
    std::cout << "Loading data from " << data_path << "..." << std::endl;
    FloatMatrix mat = load_fbin(data_path);
    npts_ = mat.npts;
    dim_  = mat.dims;
    data_ = mat.data.release();
    owns_data_ = true;

    std::cout << "  Points: " << npts_ << ", Dimensions: " << dim_ << std::endl;

    if (L < R) {
        std::cerr << "Warning: L (" << L << ") < R (" << R
                  << "). Setting L = R." << std::endl;
        L = R;
    }

    graph_.resize(npts_);
    locks_ = std::vector<std::mutex>(npts_);

    std::mt19937 rng(42);
    start_node_ = rng() % npts_;
    std::cout << "  Start node: " << start_node_ << std::endl;

    std::vector<uint32_t> perm(npts_);
    std::iota(perm.begin(), perm.end(), 0);
    std::shuffle(perm.begin(), perm.end(), rng);

    uint32_t gamma_R = static_cast<uint32_t>(gamma * R);
    std::cout << "Building index (R=" << R << ", L=" << L
              << ", alpha=" << alpha << ", gamma=" << gamma
              << ", gammaR=" << gamma_R << ")..." << std::endl;

    Timer build_timer;

    #pragma omp parallel for schedule(dynamic, 64)
    for (size_t idx = 0; idx < npts_; idx++) {
        uint32_t point = perm[idx];
        auto [candidates, _dist_cmps] = greedy_search(get_vector(point), L);
        robust_prune(point, candidates, alpha, R);

        for (uint32_t nbr : graph_[point]) {
            std::lock_guard<std::mutex> lock(locks_[nbr]);
            graph_[nbr].push_back(point);
            if (graph_[nbr].size() > gamma_R) {
                std::vector<Candidate> nbr_candidates;
                nbr_candidates.reserve(graph_[nbr].size());
                for (uint32_t nn : graph_[nbr]) {
                    float d = compute_l2sq(get_vector(nbr), get_vector(nn), dim_);
                    nbr_candidates.push_back({d, nn});
                }
                robust_prune(nbr, nbr_candidates, alpha, R);
            }
        }

        if (idx % 10000 == 0) {
            #pragma omp critical
            std::cout << "\r  Inserted " << idx << " / " << npts_
                      << " points" << std::flush;
        }
    }

    double build_time = build_timer.elapsed_seconds();
    size_t total_edges = 0;
    for (uint32_t i = 0; i < npts_; i++)
        total_edges += graph_[i].size();

    std::cout << "\n  Build complete in " << build_time << " seconds." << std::endl;
    std::cout << "  Average out-degree: " << (double)total_edges / npts_ << std::endl;
}

// ============================================================================
// Search — Standard (single start)
// ============================================================================

SearchResult VamanaIndex::search(const float* query, uint32_t K, uint32_t L) const {
    if (L < K) L = K;
    Timer t;
    auto [candidates, dist_cmps] = greedy_search(query, L);
    double latency = t.elapsed_us();

    SearchResult result;
    result.dist_cmps = dist_cmps;
    result.latency_us = latency;
    result.ids.reserve(K);
    for (uint32_t i = 0; i < K && i < candidates.size(); i++)
        result.ids.push_back(candidates[i].second);
    return result;
}

// ============================================================================
// Search — Multi-Probe V1 (split budget)
// ============================================================================

SearchResult VamanaIndex::search_multiprobe_split(const float* query, uint32_t K, uint32_t L,
                                                   uint32_t num_probes, uint32_t seed) const {
    if (L < K) L = K;
    if (num_probes < 1) num_probes = 1;
    Timer t;

    uint32_t L_per_probe = (L + num_probes - 1) / num_probes;
    if (L_per_probe < K) L_per_probe = K;

    std::mt19937 rng(seed);
    std::vector<uint32_t> entry_points(num_probes);
    entry_points[0] = start_node_;
    for (uint32_t i = 1; i < num_probes; i++)
        entry_points[i] = rng() % npts_;

    std::unordered_map<uint32_t, float> merged;
    merged.reserve(L);
    uint32_t total_dist_cmps = 0;

    for (uint32_t p = 0; p < num_probes; p++) {
        auto [candidates, cmps] = greedy_search_from(query, L_per_probe, entry_points[p]);
        total_dist_cmps += cmps;
        for (const auto& [dist, id] : candidates)
            merged.emplace(id, dist);
    }

    std::vector<Candidate> all;
    all.reserve(merged.size());
    for (const auto& [id, dist] : merged)
        all.push_back({dist, id});
    std::sort(all.begin(), all.end());

    double latency = t.elapsed_us();
    SearchResult result;
    result.dist_cmps = total_dist_cmps;
    result.latency_us = latency;
    result.ids.reserve(K);
    for (uint32_t i = 0; i < K && i < all.size(); i++)
        result.ids.push_back(all[i].second);
    return result;
}

// ============================================================================
// Search — Multi-Probe V2 (full budget per probe)
// ============================================================================

SearchResult VamanaIndex::search_multiprobe_full(const float* query, uint32_t K, uint32_t L,
                                                  uint32_t num_probes, uint32_t seed) const {
    if (L < K) L = K;
    if (num_probes < 1) num_probes = 1;
    Timer t;

    std::mt19937 rng(seed);
    std::vector<uint32_t> entry_points(num_probes);
    entry_points[0] = start_node_;
    for (uint32_t i = 1; i < num_probes; i++)
        entry_points[i] = rng() % npts_;

    std::unordered_map<uint32_t, float> merged;
    merged.reserve(L * num_probes);
    uint32_t total_dist_cmps = 0;

    for (uint32_t p = 0; p < num_probes; p++) {
        auto [candidates, cmps] = greedy_search_from(query, L, entry_points[p]);
        total_dist_cmps += cmps;
        for (const auto& [dist, id] : candidates)
            merged.emplace(id, dist);
    }

    std::vector<Candidate> all;
    all.reserve(merged.size());
    for (const auto& [id, dist] : merged)
        all.push_back({dist, id});
    std::sort(all.begin(), all.end());

    double latency = t.elapsed_us();
    SearchResult result;
    result.dist_cmps = total_dist_cmps;
    result.latency_us = latency;
    result.ids.reserve(K);
    for (uint32_t i = 0; i < K && i < all.size(); i++)
        result.ids.push_back(all[i].second);
    return result;
}

// ============================================================================
// Search — Multi-Probe V3 (shared state — best variant)
// ============================================================================

SearchResult VamanaIndex::search_multiprobe_shared(const float* query, uint32_t K, uint32_t L,
                                                    uint32_t num_probes, uint32_t seed) const {
    if (L < K) L = K;
    if (num_probes < 1) num_probes = 1;
    Timer t;

    std::mt19937 rng(seed);
    std::vector<uint32_t> entry_points(num_probes);
    entry_points[0] = start_node_;
    for (uint32_t i = 1; i < num_probes; i++)
        entry_points[i] = rng() % npts_;

    std::set<Candidate> candidate_set;
    std::vector<bool> visited(npts_, false);
    std::set<uint32_t> expanded;
    uint32_t total_dist_cmps = 0;

    for (uint32_t p = 0; p < num_probes; p++) {
        uint32_t cmps = greedy_search_inject(query, L, entry_points[p],
                                              candidate_set, visited, expanded);
        total_dist_cmps += cmps;
    }

    double latency = t.elapsed_us();
    SearchResult result;
    result.dist_cmps = total_dist_cmps;
    result.latency_us = latency;
    result.ids.reserve(K);
    uint32_t count = 0;
    for (const auto& [dist, id] : candidate_set) {
        if (count >= K) break;
        result.ids.push_back(id);
        count++;
    }
    return result;
}

// ============================================================================
// Save / Load
// ============================================================================

void VamanaIndex::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open())
        throw std::runtime_error("Cannot open file for writing: " + path);

    out.write(reinterpret_cast<const char*>(&npts_), 4);
    out.write(reinterpret_cast<const char*>(&dim_), 4);
    out.write(reinterpret_cast<const char*>(&start_node_), 4);

    for (uint32_t i = 0; i < npts_; i++) {
        uint32_t deg = graph_[i].size();
        out.write(reinterpret_cast<const char*>(&deg), 4);
        if (deg > 0)
            out.write(reinterpret_cast<const char*>(graph_[i].data()),
                      deg * sizeof(uint32_t));
    }
    std::cout << "Index saved to " << path << std::endl;
}

void VamanaIndex::load(const std::string& index_path, const std::string& data_path) {
    FloatMatrix mat = load_fbin(data_path);
    npts_ = mat.npts;
    dim_  = mat.dims;
    data_ = mat.data.release();
    owns_data_ = true;

    std::ifstream in(index_path, std::ios::binary);
    if (!in.is_open())
        throw std::runtime_error("Cannot open index file: " + index_path);

    uint32_t file_npts, file_dim;
    in.read(reinterpret_cast<char*>(&file_npts), 4);
    in.read(reinterpret_cast<char*>(&file_dim), 4);
    in.read(reinterpret_cast<char*>(&start_node_), 4);

    if (file_npts != npts_ || file_dim != dim_)
        throw std::runtime_error("Index/data mismatch");

    graph_.resize(npts_);
    locks_ = std::vector<std::mutex>(npts_);

    for (uint32_t i = 0; i < npts_; i++) {
        uint32_t deg;
        in.read(reinterpret_cast<char*>(&deg), 4);
        graph_[i].resize(deg);
        if (deg > 0)
            in.read(reinterpret_cast<char*>(graph_[i].data()), deg * sizeof(uint32_t));
    }
    std::cout << "Index loaded: " << npts_ << " points, " << dim_
              << " dims, start=" << start_node_ << std::endl;
}
