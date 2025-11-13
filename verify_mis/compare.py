# 假设两个文件分别是 file1.txt 和 file2.txt

def read_numbers(filename):
    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            yield int(line)

file1 = "test.txt"
file2 = "WikiTalk_sym.txt"

# 使用集合存储数据
set1 = set(read_numbers(file1))
set2 = set(read_numbers(file2))

# 计算差集
only_in_1 = sorted(set1 - set2)
only_in_2 = sorted(set2 - set1)

# 输出结果
print("【1中有而2中没有的】共", len(only_in_1), "个：")
print(only_in_1)

print("\n【2中有而1中没有的】共", len(only_in_2), "个：")
print(only_in_2)
