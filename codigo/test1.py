import subprocess
import sys
import random

clientes = 1

while clientes % 2 != 0:
	clientes = 50

nros = ""
caracter = ord('a')
for x in range(clientes):
	iters = random.randint(50, 60)
	cmptime = 10
	critical = 10
	nros += '"{0}" {1} {2} {3} '.format(chr(caracter), iters, cmptime, critical)
	caracter+= 1

nros = nros.strip()
print "CLIENTES:", clientes
print "NROS:", nros
process = subprocess.Popen('mpiexec -np {0} /home/ccuneo/Facu/SO/so-tp3/codigo/tp3 {1} > /dev/null'.format(clientes, nros), stderr=subprocess.PIPE, stdout=None, shell=True)
for line in iter(process.stderr.readline, ''):
	if len(set(line.strip())) == 1:
		print ".",
	else:
		print "F", line,

