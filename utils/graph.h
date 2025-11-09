#ifndef GRAPH_H
#define GRAPH_H

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

#include "parlay/io.h"
#include "parlay/parallel.h"
#include "parlay/sequence.h"
#include "parlay/utilities.h"
#include "utils/utils.h"

class Empty {};

template <class NodeId, class EdgeTy>
class WEdge {
 public:
  NodeId v;
  [[no_unique_address]] EdgeTy w;
  WEdge() {}
  WEdge(NodeId _v) : v(_v) {}
  WEdge(NodeId _v, EdgeTy _w) : v(_v), w(_w) {}
  bool operator<(const WEdge &rhs) const {
    if constexpr (std::is_same_v<EdgeTy, Empty>) {
      return v < rhs.v;
    } else {
      if (v != rhs.v) {
        return v < rhs.v;
      }
      return w < rhs.w;
    }
  }
  bool operator==(const WEdge &rhs) const {
    if constexpr (std::is_same_v<EdgeTy, Empty>) {
      return v == rhs.v;
    } else {
      return (v == rhs.v && w == rhs.w);
    }
  }
};

template <class _NodeId = uint32_t, class _EdgeId = uint64_t,
          class _EdgeTy = Empty>
class Graph {
 public:
  using NodeId = _NodeId;
  using EdgeId = _EdgeId;
  using EdgeTy = _EdgeTy;
  using Edge = WEdge<NodeId, EdgeTy>;
  size_t n;
  size_t m;
  bool symmetrized;
  bool weighted;
  parlay::sequence<EdgeId> offsets;
  parlay::sequence<Edge> edges;
  parlay::sequence<EdgeId> in_offsets;
  parlay::sequence<Edge> in_edges;

  auto in_neighors(NodeId u) const {
    if (symmetrized) {
      return edges.cut(offsets[u], offsets[u + 1]);
    } else {
      return in_edges.cut(in_offsets[u], in_offsets[u + 1]);
    }
  }

  void make_inverse() {
    parlay::sequence<std::pair<NodeId, Edge>> edgelist(m);
    parlay::parallel_for(0, n, [&](NodeId u) {
      parlay::parallel_for(offsets[u], offsets[u + 1], [&](EdgeId i) {
        edgelist[i] = std::make_pair(edges[i].v, Edge(u, edges[i].w));
      });
    });
    parlay::sort_inplace(parlay::make_slice(edgelist), [](auto &a, auto &b) {
      if (a.first != b.first) return a.first < b.first;
      return a.second.v < b.second.v;
    });
    in_offsets = parlay::sequence<EdgeId>(n + 1, m);
    in_edges = parlay::sequence<Edge>(m);
    parlay::parallel_for(0, m, [&](size_t i) {
      in_edges[i] = edgelist[i].second;
      if (i == 0 || edgelist[i].first != edgelist[i - 1].first) {
        in_offsets[edgelist[i].first] = i;
      }
    });
    parlay::scan_inclusive_inplace(
        parlay::make_slice(in_offsets.rbegin(), in_offsets.rend()),
        parlay::minm<EdgeId>());
  }

  void read_pbbs_format(char const *filename) {
    auto chars = parlay::chars_from_file(std::string(filename));
    auto tokens_seq = tokens(chars);
    auto header = tokens_seq[0];
    n = chars_to_ulong_long(tokens_seq[1]);
    m = chars_to_ulong_long(tokens_seq[2]);
    bool weighted_input;
    if (header == parlay::to_chars("WeightedAdjacencyGraph")) {
      weighted_input = true;
      assert(tokens_seq.size() == n + m + m + 3);
    } else if (header == parlay::to_chars("AdjacencyGraph")) {
      weighted_input = false;
      assert(tokens_seq.size() == n + m + 3);
    } else {
      std::cerr << "Unrecognized header in PBBS format" << std::endl;
      abort();
    }
    offsets = parlay::sequence<EdgeId>(n + 1);
    edges = parlay::sequence<Edge>(m);
    parlay::parallel_for(0, n, [&](size_t i) {
      offsets[i] = parlay::internal::chars_to_int_t<EdgeId>(
          make_slice(tokens_seq[i + 3]));
    });
    offsets[n] = m;
    parlay::parallel_for(0, m, [&](size_t i) {
      edges[i].v = parlay::internal::chars_to_int_t<NodeId>(
          make_slice(tokens_seq[i + n + 3]));
    });
    if (weighted_input) {
      if constexpr (std::is_same_v<EdgeTy, Empty>) {
        std::cout << "Warning: skipping edge weights in file" << std::endl;
      } else {
        weighted = true;
        parlay::parallel_for(0, m, [&](size_t i) {
          if constexpr (std::is_integral_v<EdgeTy>) {
            edges[i].w = parlay::internal::chars_to_int_t<EdgeTy>(
                make_slice(tokens_seq[i + n + m + 3]));
          } else if constexpr (std::is_floating_point_v<EdgeTy>) {
            edges[i].w = parlay::chars_to_double(tokens_seq[i + n + m + 3]);
          } else {
            std::cerr << "Error: EdgeTy is not arithmetic" << std::endl;
            abort();
          }
        });
      }
    }
  }

  // GBBS-style adjacency format reader (same as GBBS)
  void read_gbbs_adj_format(const char *filename) {
    std::cout << "Reading GBBS adjacency format: " << filename << std::endl;

    std::ifstream file(filename);
    if (!file.is_open()) {
      std::cerr << "Error: Cannot open file " << filename << std::endl;
      abort();
    }

    // Skip comments (lines starting with #)
    std::string line;
    while (std::getline(file, line)) {
      if (line.empty() || line[0] == '#') {
        continue;
      }
      break;
    }

    // Check header
    if (line != "AdjacencyGraph" && line != "WeightedAdjacencyGraph") {
      std::cerr << "Error: Invalid header. Expected 'AdjacencyGraph' or "
                   "'WeightedAdjacencyGraph', got: "
                << line << std::endl;
      abort();
    }

    bool weighted_input = (line == "WeightedAdjacencyGraph");

    // Read n and m
    if (!(file >> n >> m)) {
      std::cerr << "Error: Cannot read n and m" << std::endl;
      abort();
    }

    std::cout << "Graph: " << n << " vertices, " << m << " edges" << std::endl;

    // Read offsets
    offsets = parlay::sequence<EdgeId>(n + 1);
    for (size_t i = 0; i < n; i++) {
      if (!(file >> offsets[i])) {
        std::cerr << "Error: Cannot read offset " << i << std::endl;
        abort();
      }
    }
    offsets[n] = m;  // Last offset should be m

    // Read edges
    edges = parlay::sequence<Edge>(m);
    for (size_t i = 0; i < m; i++) {
      NodeId v;
      if (!(file >> v)) {
        std::cerr << "Error: Cannot read edge " << i << std::endl;
        abort();
      }
      edges[i] = Edge(v);
    }

    // Read weights if weighted (for now, just skip them)
    if (weighted_input) {
      std::cout << "Warning: Weighted graph detected, but weights are not "
                   "supported yet"
                << std::endl;
      // TODO: Implement weight reading
    }

    file.close();

    // Set graph properties
    symmetrized = false;
    weighted = weighted_input;

    std::cout << "GBBS adjacency format read successfully!" << std::endl;
  }

  void read_binary_format(char const *filename) {
    struct stat sb;
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
      std::cerr << "Error: Cannot open file " << filename << std::endl;
      abort();
    }
    if (fstat(fd, &sb) == -1) {
      std::cerr << "Error: Unable to acquire file stat" << std::endl;
      abort();
    }
    char *data =
        static_cast<char *>(mmap(0, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
    size_t len = sb.st_size;
    n = reinterpret_cast<uint64_t *>(data)[0];
    m = reinterpret_cast<uint64_t *>(data)[1];
    size_t sizes = reinterpret_cast<uint64_t *>(data)[2];
    assert(sizes == (n + 1) * 8 + m * 4 + 3 * 8 &&
           "File size mismatch for out-edge binary");
    offsets = parlay::sequence<EdgeId>::uninitialized(n + 1);
    edges = parlay::sequence<Edge>::uninitialized(m);
    parlay::parallel_for(0, n + 1, [&](size_t i) {
      offsets[i] = reinterpret_cast<uint64_t *>(data + 3 * 8)[i];
    });
    parlay::parallel_for(0, m, [&](size_t i) {
      edges[i].v = reinterpret_cast<uint32_t *>(data + 3 * 8 + (n + 1) * 8)[i];
    });
    if (data) {
      munmap(data, len);
    }
    close(fd);
  }

  void read_directed_binary_format(char const *filename) {
    struct stat sb;
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
      std::cerr << "Error: Cannot open file " << filename << std::endl;
      abort();
    }
    if (fstat(fd, &sb) == -1) {
      std::cerr << "Error: Unable to acquire file stat" << std::endl;
      abort();
    }
    char *data = static_cast<char *>(
        mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (data == MAP_FAILED) {
      std::cerr << "Error: mmap failed" << std::endl;
      abort();
    }
    size_t len = sb.st_size;
    n = reinterpret_cast<uint64_t *>(data)[0];
    m = reinterpret_cast<uint64_t *>(data)[1];
    size_t file_sizes = reinterpret_cast<uint64_t *>(data)[2];
    size_t expected_size = 3 * 8 + (n + 1) * 8 + m * 4;
    if (file_sizes != expected_size) {
      std::cerr << "Error: File size does not match expectation!\n"
                << "  file_sizes: " << file_sizes << "\n"
                << "  expected_size: " << expected_size << "\n";
      abort();
    }
    auto in_offsets_ptr = reinterpret_cast<uint64_t *>(data + 3 * 8);
    in_offsets = parlay::sequence<EdgeId>::uninitialized(n + 1);
    for (size_t i = 0; i < n + 1; i++) {
      in_offsets[i] = in_offsets_ptr[i];
    }
    auto in_edges_ptr =
        reinterpret_cast<uint32_t *>(data + 3 * 8 + (n + 1) * 8);
    in_edges = parlay::sequence<Edge>(m);
    for (size_t i = 0; i < m; i++) {
      in_edges[i].v = in_edges_ptr[i];
    }
    parlay::sequence<size_t> out_degree(n, 0);
    for (size_t dest = 0; dest < n; dest++) {
      for (size_t j = in_offsets[dest]; j < in_offsets[dest + 1]; j++) {
        NodeId src = in_edges[j].v;
        out_degree[src]++;
      }
    }
    offsets = parlay::sequence<EdgeId>(n + 1);
    offsets[0] = 0;
    for (size_t i = 1; i <= n; i++) {
      offsets[i] = offsets[i - 1] + out_degree[i - 1];
    }
    edges = parlay::sequence<Edge>(m);
    parlay::sequence<size_t> out_pos(n, 0);
    for (size_t dest = 0; dest < n; dest++) {
      for (size_t j = in_offsets[dest]; j < in_offsets[dest + 1]; j++) {
        NodeId src = in_edges[j].v;
        size_t pos = offsets[src] + out_pos[src]++;
        edges[pos].v = dest;
      }
    }
    munmap(data, len);
    close(fd);
  }

  void read_hyperlink2012(const char *filename) {
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs.is_open()) {
      std::cerr << "Error: Cannot open file " << filename << std::endl;
      abort();
    }
    size_t sizes;
    ifs.read(reinterpret_cast<char *>(&n), sizeof(size_t));
    ifs.read(reinterpret_cast<char *>(&m), sizeof(size_t));
    ifs.read(reinterpret_cast<char *>(&sizes), sizeof(size_t));
    assert(sizes == (n + 1) * 8 + m * 4 + 3 * 8);
    offsets = parlay::sequence<EdgeId>::uninitialized(n + 1);
    edges = parlay::sequence<Edge>(m);
    ifs.read(reinterpret_cast<char *>(offsets.begin()), (n + 1) * 8);
    ifs.read(reinterpret_cast<char *>(edges.begin()), m * 4);
    ifs.read(reinterpret_cast<char *>(&n), sizeof(size_t));
    ifs.read(reinterpret_cast<char *>(&m), sizeof(size_t));
    ifs.read(reinterpret_cast<char *>(&sizes), sizeof(size_t));
    in_offsets = parlay::sequence<EdgeId>::uninitialized(n + 1);
    in_edges = parlay::sequence<Edge>(m);
    ifs.read(reinterpret_cast<char *>(in_offsets.begin()), (n + 1) * 8);
    ifs.read(reinterpret_cast<char *>(in_edges.begin()), m * 4);
    if (ifs.peek() != EOF) {
      std::cerr << "Error: Bad input graph (extraneous data)" << std::endl;
      abort();
    }
    ifs.close();
  }

  void read_graph(const char *filename) {
    std::string str_filename(filename);
    if (str_filename.find("hyperlink2012.bin") != std::string::npos) {
      read_hyperlink2012(filename);
      return;
    }
    size_t idx = str_filename.find_last_of('.');
    if (idx == std::string::npos) {
      std::cerr << "Error: No graph extension provided" << std::endl;
      abort();
    }
    std::string subfix = str_filename.substr(idx + 1);
    if (subfix == "adj") {
      read_gbbs_adj_format(filename);
    } else if (subfix == "bin" &&
               str_filename.find("sym") != std::string::npos) {
      read_binary_format(filename);
    } else if (subfix == "bin" &&
               str_filename.find("sym") == std::string::npos) {
      read_directed_binary_format(filename);
    } else {
      std::cerr << "Error: Invalid graph extension or format: " << filename
                << std::endl;
      abort();
    }
  }

  // New function for reading bi-core graphs (similar to GBBS approach)
  void read_bicore_graph(const char *filename, size_t left_partition_size) {
    std::cout << "Reading bi-core graph with left partition size: "
              << left_partition_size << std::endl;

    // First read the graph using the standard method
    read_graph(filename);

    // Validate that the partition size makes sense
    if (left_partition_size >= n) {
      std::cerr << "Error: Left partition size (" << left_partition_size
                << ") must be less than total vertices (" << n << ")"
                << std::endl;
      abort();
    }

    // For bi-core decomposition, we typically want a bipartite graph
    // Check if the graph structure is suitable for bi-core
    size_t left_edges = 0, right_edges = 0, cross_edges = 0;

    for (size_t u = 0; u < n; u++) {
      for (size_t es = offsets[u]; es < offsets[u + 1]; es++) {
        NodeId v = edges[es].v;
        if (u < left_partition_size && v < left_partition_size) {
          left_edges++;
        } else if (u >= left_partition_size && v >= left_partition_size) {
          right_edges++;
        } else {
          cross_edges++;
        }
      }
    }

    std::cout << "Graph structure analysis:" << std::endl;
    std::cout << "  Left partition (0 to " << (left_partition_size - 1)
              << "): " << left_partition_size << " vertices" << std::endl;
    std::cout << "  Right partition (" << left_partition_size << " to "
              << (n - 1) << "): " << (n - left_partition_size) << " vertices"
              << std::endl;
    std::cout << "  Edges within left partition: " << left_edges << std::endl;
    std::cout << "  Edges within right partition: " << right_edges << std::endl;
    std::cout << "  Cross-partition edges: " << cross_edges << std::endl;

    // Warn if the graph doesn't look bipartite
    if (left_edges > 0 || right_edges > 0) {
      std::cout << "Warning: Graph contains edges within partitions. "
                << "This may not be optimal for bi-core decomposition."
                << std::endl;
    }

    // Ensure the graph is symmetrized for bi-core decomposition
    if (!symmetrized) {
      std::cout << "Symmetrizing graph for bi-core decomposition..."
                << std::endl;
      *this = make_symmetrized(*this);
      symmetrized = true;
    }

    std::cout << "Bi-core graph loaded successfully!" << std::endl;
  }

  // Helper function to create a bi-core graph from edge list
  // This is useful when you have a bipartite graph in edge list format
  void create_bicore_graph_from_edges(const char *filename,
                                      size_t left_partition_size) {
    std::cout
        << "Creating bi-core graph from edge list with left partition size: "
        << left_partition_size << std::endl;

    std::ifstream file(filename);
    if (!file.is_open()) {
      std::cerr << "Error: Cannot open file " << filename << std::endl;
      abort();
    }

    // Read edge list and determine graph size
    std::vector<std::pair<NodeId, NodeId>> edge_list;
    NodeId max_vertex = 0;
    NodeId u, v;

    while (file >> u >> v) {
      edge_list.push_back({u, v});
      max_vertex = std::max(max_vertex, std::max(u, v));
    }
    file.close();

    n = max_vertex + 1;
    m = edge_list.size();

    if (left_partition_size >= n) {
      std::cerr << "Error: Left partition size (" << left_partition_size
                << ") must be less than total vertices (" << n << ")"
                << std::endl;
      abort();
    }

    // Sort edges by source vertex for efficient offset calculation
    std::sort(edge_list.begin(), edge_list.end());

    // Build offsets array
    offsets = parlay::sequence<EdgeId>(n + 1, 0);
    for (const auto &edge : edge_list) {
      offsets[edge.first + 1]++;
    }

    // Convert to cumulative offsets
    for (size_t i = 1; i <= n; i++) {
      offsets[i] += offsets[i - 1];
    }

    // Build edges array
    edges = parlay::sequence<Edge>(m);
    parlay::parallel_for(
        0, m, [&](size_t i) { edges[i] = Edge(edge_list[i].second); });

    // Set graph properties
    symmetrized = false;
    weighted = false;

    // Analyze graph structure
    size_t left_edges = 0, right_edges = 0, cross_edges = 0;
    for (const auto &edge : edge_list) {
      if (edge.first < left_partition_size &&
          edge.second < left_partition_size) {
        left_edges++;
      } else if (edge.first >= left_partition_size &&
                 edge.second >= left_partition_size) {
        right_edges++;
      } else {
        cross_edges++;
      }
    }

    std::cout << "Graph structure analysis:" << std::endl;
    std::cout << "  Left partition (0 to " << (left_partition_size - 1)
              << "): " << left_partition_size << " vertices" << std::endl;
    std::cout << "  Right partition (" << left_partition_size << " to "
              << (n - 1) << "): " << (n - left_partition_size) << " vertices"
              << std::endl;
    std::cout << "  Edges within left partition: " << left_edges << std::endl;
    std::cout << "  Edges within right partition: " << right_edges << std::endl;
    std::cout << "  Cross-partition edges: " << cross_edges << std::endl;

    // Warn if the graph doesn't look bipartite
    if (left_edges > 0 || right_edges > 0) {
      std::cout << "Warning: Graph contains edges within partitions. "
                << "This may not be optimal for bi-core decomposition."
                << std::endl;
    }

    std::cout << "Bi-core graph created from edge list successfully!"
              << std::endl;
  }

  void write_pbbs_format(char const *filename) {
    parlay::chars chars;
    if constexpr (std::is_same_v<EdgeTy, Empty>) {
      chars.insert(chars.end(), parlay::to_chars("AdjacencyGraph\n"));
    } else {
      chars.insert(chars.end(), parlay::to_chars("WeightedAdjacencyGraph\n"));
    }
    chars.insert(chars.end(), parlay::to_chars(std::to_string(n) + "\n"));
    chars.insert(chars.end(), parlay::to_chars(std::to_string(m) + "\n"));
    chars.insert(chars.end(),
                 parlay::flatten(parlay::tabulate(n * 2, [&](size_t i) {
                   if (i % 2 == 0) {
                     return parlay::to_chars(offsets[i / 2]);
                   } else {
                     return parlay::to_chars('\n');
                   }
                 })));
    chars.insert(chars.end(),
                 parlay::flatten(parlay::tabulate(m * 2, [&](size_t i) {
                   if (i % 2 == 0) {
                     return parlay::to_chars(edges[i / 2].v);
                   } else {
                     return parlay::to_chars('\n');
                   }
                 })));
    if constexpr (!std::is_same_v<EdgeTy, Empty>) {
      chars.insert(chars.end(),
                   parlay::flatten(parlay::tabulate(m * 2, [&](size_t i) {
                     if (i % 2 == 0) {
                       return parlay::to_chars(std::to_string(edges[i / 2].w));
                     } else {
                       return parlay::to_chars('\n');
                     }
                   })));
    }
    chars_to_file(chars, std::string(filename));
  }

  void write_binary_format(char const *filename) {
    static_assert(sizeof(EdgeId) == sizeof(uint64_t));
    static_assert(sizeof(NodeId) == sizeof(uint32_t));
    size_t sizes = (n + 1) * 8 + m * 4 + 3 * 8;
    auto tmp_edges =
        parlay::tabulate<NodeId>(m, [&](size_t i) { return edges[i].v; });
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs.is_open()) {
      std::cerr << "Error: Cannot open file " << filename << std::endl;
      abort();
    }
    ofs.write(reinterpret_cast<const char *>(&n), sizeof(size_t));
    ofs.write(reinterpret_cast<const char *>(&m), sizeof(size_t));
    ofs.write(reinterpret_cast<const char *>(&sizes), sizeof(size_t));
    ofs.write(reinterpret_cast<const char *>(offsets.begin()),
              sizeof(EdgeId) * (n + 1));
    ofs.write(reinterpret_cast<const char *>(tmp_edges.begin()),
              sizeof(NodeId) * m);
    ofs.close();
  }

  void generate_random_weight(uint32_t l, uint32_t r) {
    static_assert(!std::is_same_v<EdgeTy, Empty>,
                  "Error: Graph instance does not have an edge weight field");
    if (weighted) {
      std::cerr << "Warning: Overwriting original edge weights\n" << std::endl;
    } else {
      weighted = true;
    }
    uint32_t range = r - l + 1;
    parlay::parallel_for(0, n, [&](NodeId u) {
      parlay::parallel_for(offsets[u], offsets[u + 1], [&](EdgeId i) {
        NodeId v = edges[i].v;
        edges[i].w = ((parlay::hash32(u) ^ parlay::hash32(v)) % range) + l;
      });
    });
  }

  void count_self_loop_and_parallel_edges() {
    size_t self_loops = 0;
    size_t parallel_edges_ct = 0;
    parlay::parallel_for(0, n, [&](size_t i) {
      size_t prev = n + 1;
      for (size_t j = offsets[i]; j < offsets[i + 1]; j++) {
        NodeId v = edges[j].v;
        if (i == v) {
          write_add(&self_loops, 1);
        }
        if (v == prev) {
          write_add(&parallel_edges_ct, 1);
        }
        prev = v;
      }
    });
    printf("num of self-loops: %zu\n", self_loops);
    printf("num of parallel edges: %zu\n", parallel_edges_ct);
  }

  void validate() {
    parlay::parallel_for(0, n + 1, [&](size_t i) {
      if (i == 0) {
        assert(offsets[i] == 0);
      } else if (i == n) {
        assert(offsets[i] == m);
      } else {
        assert(offsets[i] >= offsets[i - 1]);
      }
    });
    parlay::parallel_for(0, m, [&](size_t i) {
      NodeId u = edges[i].v;
      assert(u < n);
    });
    bool sorted = true;
    parlay::parallel_for(0, n, [&](NodeId u) {
      for (size_t i = offsets[u] + 1; i < offsets[u + 1]; i++) {
        NodeId curr_v = edges[i].v;
        NodeId prev_v = edges[i - 1].v;
        if (curr_v < prev_v && sorted == true) {
          sorted = false;
        }
      }
    });
    if (!sorted) {
      printf("Warning: Edges are not sorted by destination\n");
    }
    count_self_loop_and_parallel_edges();
    if (symmetrized) {
      parlay::parallel_for(0, n, [&](NodeId u) {
        for (size_t i = offsets[u]; i < offsets[u + 1]; i++) {
          NodeId v = edges[i].v;
          WEdge<NodeId, EdgeTy> e(u, edges[i].w);
          auto low_bound = std::lower_bound(edges.begin() + offsets[v],
                                            edges.begin() + offsets[v + 1], e);
          if (low_bound == edges.begin() + offsets[v + 1] || *low_bound != e) {
            if (symmetrized == true) {
              symmetrized = false;
            }
          }
        }
      });
      if (!symmetrized) {
        std::cerr << "Error: edges are not actually symmetrized" << std::endl;
        abort();
      }
    }
    printf("Passed graph validation!\n");
  }

  void generate_random_graph(size_t _n = 100000000, size_t _m = 8000000000) {
    n = _n;
    m = _m;
    auto edgelist =
        parlay::sequence<std::pair<NodeId, NodeId>>::uninitialized(m);
    parlay::parallel_for(0, m / 2, [&](size_t i) {
      NodeId u = parlay::hash32(i * 2) % n;
      NodeId v = parlay::hash32(i * 2 + 1) % n;
      if (symmetrized) {
        edgelist[i * 2 + 0] = std::make_pair(u, v);
        edgelist[i * 2 + 1] = std::make_pair(v, u);
      } else {
        edgelist[i * 2 + 0] = std::make_pair(u, v);
        edgelist[i * 2 + 1] =
            std::make_pair(parlay::hash32(u) % n, parlay::hash32(v) % n);
      }
    });
    parlay::sort_inplace(parlay::make_slice(edgelist), [](auto &a, auto &b) {
      if (a.first != b.first) return a.first < b.first;
      return a.second < b.second;
    });
    offsets = parlay::sequence<EdgeId>(n + 1, m);
    edges = parlay::sequence<Edge>(m);
    parlay::parallel_for(0, m, [&](size_t i) {
      edges[i].v = edgelist[i].second;
      if (i == 0 || edgelist[i].first != edgelist[i - 1].first) {
        offsets[edgelist[i].first] = i;
      }
    });
    parlay::scan_inclusive_inplace(
        parlay::make_slice(offsets.rbegin(), offsets.rend()),
        parlay::minm<EdgeId>());
  }
};

template <class NodeId = uint32_t>
class Forest {
 public:
  size_t num_trees;
  Graph<NodeId, NodeId> G;
  parlay::sequence<NodeId> vertex;
  parlay::sequence<NodeId> offsets;
};

template <class NodeId, class EdgeId, class EdgeTy>
Graph<NodeId, EdgeId, EdgeTy> edgelist2graph(
    parlay::sequence<std::pair<NodeId, WEdge<NodeId, EdgeTy>>> &edgelist,
    size_t n, size_t m) {
  using Edge = WEdge<NodeId, EdgeTy>;
  Graph<NodeId, EdgeId, EdgeTy> G;
  G.n = n;
  G.m = m;
  parlay::sort_inplace(parlay::make_slice(edgelist), [](auto &a, auto &b) {
    if (a.first != b.first) return a.first < b.first;
    return a.second.v < b.second.v;
  });
  G.offsets = parlay::sequence<EdgeId>(n + 1, m);
  G.edges = parlay::sequence<Edge>(m);
  parlay::parallel_for(0, m, [&](size_t i) {
    G.edges[i] = edgelist[i].second;
    if (i == 0 || edgelist[i].first != edgelist[i - 1].first) {
      G.offsets[edgelist[i].first] = i;
    }
  });
  parlay::scan_inclusive_inplace(
      parlay::make_slice(G.offsets.rbegin(), G.offsets.rend()),
      parlay::minm<EdgeId>());
  return G;
}

template <class Graph>
Graph make_symmetrized(const Graph &G) {
  size_t n = G.n;
  size_t m = G.m;
  using NodeId = typename Graph::NodeId;
  using EdgeId = typename Graph::EdgeId;
  using EdgeTy = typename Graph::EdgeTy;
  using Edge = typename Graph::Edge;
  parlay::sequence<std::pair<NodeId, Edge>> edgelist(m * 2);
  parlay::parallel_for(0, n, [&](NodeId u) {
    parlay::parallel_for(G.offsets[u], G.offsets[u + 1], [&](EdgeId i) {
      NodeId v = G.edges[i].v;
      EdgeTy w = G.edges[i].w;
      edgelist[i * 2 + 0] = std::make_pair(u, Edge(v, w));
      edgelist[i * 2 + 1] = std::make_pair(v, Edge(u, w));
    });
  });
  sort_inplace(make_slice(edgelist));
  auto pred = parlay::delayed_seq<bool>(m * 2, [&](size_t i) {
    if (i > 0 && edgelist[i].first == edgelist[i - 1].first &&
        edgelist[i].second.v == edgelist[i - 1].second.v) {
      return false;
    }
    if (edgelist[i].first == edgelist[i].second.v) {
      return false;
    }
    return true;
  });
  edgelist = parlay::pack(make_slice(edgelist), pred);
  return edgelist2graph<NodeId, EdgeId, EdgeTy>(edgelist, n, edgelist.size());
}

template <class Graph>
Graph Transpose(const Graph &G) {
  size_t n = G.n;
  size_t m = G.m;
  using NodeId = typename Graph::NodeId;
  using EdgeId = typename Graph::EdgeId;
  using EdgeTy = typename Graph::EdgeTy;
  using Edge = typename Graph::Edge;
  parlay::sequence<std::pair<NodeId, Edge>> edgelist(m);
  parlay::parallel_for(0, n, [&](NodeId u) {
    parlay::parallel_for(G.offsets[u], G.offsets[u + 1], [&](EdgeId i) {
      edgelist[i] = std::make_pair(G.edges[i].v, Edge(u, G.edges[i].w));
    });
  });
  return edgelist2graph<NodeId, EdgeId, EdgeTy>(edgelist, n, m);
}

#endif
