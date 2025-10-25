import struct
file_in  = "bin/sd_arc_sym.bin"
file_out = "txt/sd_arc.txt"

with open(file_in, "rb") as f:
    n = struct.unpack("Q", f.read(8))[0]
    m = struct.unpack("Q", f.read(8))[0]
    sizes = struct.unpack("Q", f.read(8))[0]
    print(f"n={n}, m={m}, sizes={sizes}")
    offsets = list(struct.unpack(f"{n+1}Q", f.read((n+1)*8)))
    edges = list(struct.unpack(f"{m}I", f.read(m*4))) 

with open(file_out, "w") as out:
    out.write("# FromNodeId\tToNodeId\n")
    for u in range(n):
        start, end = offsets[u], offsets[u+1]
        for i in range(start, end):
            out.write(f"{u}\t{edges[i]}\n")

'''

bazel run //utils:snap_converter -- -s \
  -i /home/csgrads/xjian140/Counter/testcases/txt/sd_arc.txt \
  -o /home/csgrads/xjian140/Counter/testcases/gbbs/sd_arc.adjgraph

/home/csgrads/xjian140/baselines/gbbs/bazel-bin/benchmarks/MaximalIndependentSet/RandomGreedy/MaximalIndependentSet_main -s \
/home/csgrads/xjian140/Counter/testcases/gbbs/eu-2015-host.adjgraph

'''
