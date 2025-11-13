import matplotlib.pyplot as plt
import numpy as np

# 读取数据
values = np.load("counter_distribution/friendster_sym.npy")

# ========= 图1：线性坐标直方图 =========
plt.figure(figsize=(8,5))
plt.hist(values, bins=200)  # 不加 log 参数 -> X、Y 都是线性刻度
plt.xlabel("Counter value")
plt.ylabel("Frequency")
plt.title("Counter Value Distribution (Linear Scale)")
plt.savefig("figure/friendster_sym_linear.png", dpi=300, bbox_inches='tight')

# ========= 图2：log-log 分布图 =========
counts, bins = np.histogram(values, bins=200)
plt.figure(figsize=(8,5))
plt.loglog(bins[1:], counts, marker='.')
plt.xlabel("Value (log scale)")
plt.ylabel("Count (log scale)")
plt.title("Log-Log Distribution")
plt.savefig("figure/friendster_sym_loglog.png", dpi=300, bbox_inches='tight')