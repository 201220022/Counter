import matplotlib.pyplot as plt
import numpy as np

values = np.loadtxt("distribution/friendster_sym.txt")

plt.figure(figsize=(8,5))
plt.hist(values, bins=200, log=True) 
plt.xlabel("Counter value")
plt.ylabel("Frequency (log scale)")
plt.title("Counter Value Distribution")
plt.savefig("figure/friendster_sym.png", dpi=300, bbox_inches='tight')

counts, bins = np.histogram(values, bins=200)
plt.figure()
plt.loglog(bins[1:], counts, marker='.')
plt.xlabel("Value (log scale)")
plt.ylabel("Count (log scale)")
plt.title("Log-Log Distribution")
plt.savefig("figure/friendster_sym1.png", dpi=300, bbox_inches='tight')