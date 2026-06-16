#pragma once

#include <cstdint>
#include <vector>
#include <mutex>
#include <set>
#include <string>

// Result of a single query search.
struct SearchResult {
    std::vector<uint32_t> ids;  // nearest neighbor IDs (sorted by distance)
    uint32_t dist_cmps;         // number of distance computations
    double latency_us;          // search latency in microseconds
};

// Vamana graph-based approximate nearest neighbor index.
class VamanaIndex {
  public:
    VamanaIndex() = default;
    ~VamanaIndex();

    // ---- Build ----
    void build(const std::string& data_path, uint32_t R, uint32_t L,
               float alpha, float gamma);

    // ---- Search ----
    // Standard single-start search.
    SearchResult search(const float* query, uint32_t K, uint32_t L) const;

    // Multi-probe search V1 (split budget): k independent searches each with
    // search list size ceil(L/k), merge results. Same total compute budget.
    SearchResult search_multiprobe_split(const float* query, uint32_t K, uint32_t L,
                                         uint32_t num_probes, uint32_t seed) const;

    // Multi-probe search V2 (full budget): k independent searches each with
    // full search list size L, merge results. k× more compute.
    SearchResult search_multiprobe_full(const float* query, uint32_t K, uint32_t L,
                                        uint32_t num_probes, uint32_t seed) const;

    // Multi-probe search V3 (shared state): k probes sequentially into one
    // shared candidate set and visited array. Each probe injects a random
    // entry point and continues greedy expansion. No redundant evaluations.
    SearchResult search_multiprobe_shared(const float* query, uint32_t K, uint32_t L,
                                          uint32_t num_probes, uint32_t seed) const;

    // ---- Persistence ----
    void save(const std::string& path) const;
    void load(const std::string& index_path, const std::string& data_path);

    uint32_t get_npts() const { return npts_; }
    uint32_t get_dim()  const { return dim_; }
    uint32_t get_start_node() const { return start_node_; }

  private:
    using Candidate = std::pair<float, uint32_t>;

    // Greedy search from a specific entry node (generalized).
    std::pair<std::vector<Candidate>, uint32_t>
    greedy_search_from(const float* query, uint32_t L, uint32_t entry_node) const;

    // Greedy search with shared state: injects a new entry point into an
    // existing candidate set and continues expanding. Returns new dist cmps.
    uint32_t greedy_search_inject(const float* query, uint32_t L,
                                  uint32_t entry_node,
                                  std::set<Candidate>& candidate_set,
                                  std::vector<bool>& visited,
                                  std::set<uint32_t>& expanded) const;

    // Original greedy search (delegates to greedy_search_from with start_node_).
    std::pair<std::vector<Candidate>, uint32_t>
    greedy_search(const float* query, uint32_t L) const;

    void robust_prune(uint32_t node, std::vector<Candidate>& candidates,
                      float alpha, uint32_t R);

    // ---- Data ----
    float*   data_    = nullptr;
    uint32_t npts_    = 0;
    uint32_t dim_     = 0;
    bool     owns_data_ = false;

    // ---- Graph ----
    std::vector<std::vector<uint32_t>> graph_;
    uint32_t start_node_ = 0;

    // ---- Concurrency ----
    mutable std::vector<std::mutex> locks_;

    // ---- Helpers ----
    const float* get_vector(uint32_t id) const {
        return data_ + (size_t)id * dim_;
    }
};
