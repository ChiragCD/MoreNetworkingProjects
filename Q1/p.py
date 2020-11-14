import random

f = open("in", "w")

# n = int(random.random()*1000)
n=4
for i in range(n):
	f.write(str(int(random.random()*1000)) + " ")

f.close()
