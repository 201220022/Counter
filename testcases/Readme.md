python3 convert.py 能够把 .bin 转化成 .txt

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

这个testcases文件夹的文件结构是：
[xjian140@xe-15 Counter]$ ls /home/csgrads/xjian140/Counter/testcases
bin  convert.py  gbbs  Readme.md  txt
[xjian140@xe-15 Counter]$ ls /home/csgrads/xjian140/Counter/testcases/bin
com-orkut_sym.bin  eu-2015-host_sym.bin  friendster_sym.bin  hugebubbles-00020_sym.bin  sd_arc_sym.bin  soc-LiveJournal1_sym.bin
[xjian140@xe-15 Counter]$ ls /home/csgrads/xjian140/Counter/testcases/txt
com-friendster.ungraph.txt  com-orkut.txt  eu-2015-host.txt  hugebubbles.txt  sd_arc.txt  soc-LiveJournal1.txt
[xjian140@xe-15 Counter]$ ls /home/csgrads/xjian140/Counter/testcases/gbbs
com-friendster.adjgraph  com-orkut.adjgraph  eu-2015-host.adjgraph  hugebubbles.adjgraph  sd_arc.adjgraph  soc-LiveJournal1.adjgraph