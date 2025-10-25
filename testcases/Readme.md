convert.py 能够把 .bin 转化成 .txt

cd 到 gbbs 文件夹之后，

用这个指令把.txt转化成.adjcent: 
bazel run //utils:snap_converter -- -s \
    -i /home/csgrads/xjian140/Counter/testcases/soc-LiveJournal1.txt \
    -o /home/csgrads/xjian140/Counter/testcases/soc-LiveJournal1.adjgraph

用这个指令运行gbbs:
bazel-bin/benchmarks/MaximalIndependentSet/RandomGreedy/MaximalIndependentSet_main -s \
    /home/csgrads/xjian140/Counter/testcases/gbbs/soc-LiveJournal1.adjgraph
bazel-bin/benchmarks/MaximalIndependentSet/Yoshida/MaximalIndependentSet_main -s \
    /home/csgrads/xjian140/Counter/testcases/gbbs/soc-LiveJournal1.adjgraph
