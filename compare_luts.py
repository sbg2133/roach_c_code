import define_LUTs as lut
import numpy as np
import matplotlib.pyplot as plt

#luts = lut.roach_LUT()
#luts.make_luts(luts.test_freq)

#DACI = np.loadtxt('/home/muchacho/roach_c_code/DACI.txt',dtype=float)
#DACQ = np.loadtxt('/home/muchacho/roach_c_code/DACQ.txt',dtype=float)
#DDSI = np.loadtxt('/home/muchacho/roach_c_code/DDSI.txt',dtype=float)
#DDSQ = np.loadtxt('/home/muchacho/roach_c_code/DDSQ.txt',dtype=float)
QDRI = np.loadtxt('/home/muchacho/roach_c_code/QDRI.txt',dtype = ">i2")
QDRQ = np.loadtxt('/home/muchacho/roach_c_code/QDRQ.txt',dtype = ">i2")


QDRI = np.array(QDRI)
QDRQ = np.array(QDRQ)
I = QDRI.reshape(len(QDRI)/4.,4.)
Q = QDRQ.reshape(len(QDRQ)/4.,4.)
I_dac = np.hstack(zip(I[:,1],I[:,0]))
Q_dac = np.hstack(zip(Q[:,1],Q[:,0]))
I_dds = np.hstack(zip(I[:,3],I[:,2]))
Q_dds = np.hstack(zip(Q[:,3],Q[:,2]))

#I_dac[I_dac > 2**15] -= 2**16
print I_dac[:1024] 

#dac_spec = np.loadtxt('/home/muchacho/roach_c_code/dac_spec.txt',dtype=float, usecols = [0,1], delimiter = ',')
dds_spec = np.loadtxt('/home/muchacho/roach_c_code/dds_spec.txt',dtype=float, usecols = [0,1], delimiter = ',')
dac_wave = np.loadtxt('/home/muchacho/roach_c_code/dac_wave.txt',dtype=float, usecols = [0,1], delimiter = ',')

"""
plt.ion()
plt.figure(1)
plt.clf()
plt.plot(DACI[:100], c = 'g', label = 'Idac C')
plt.plot(luts.I_dac[:100], c = 'r', label = 'Idac Py')
plt.legend()

plt.figure(2)
plt.clf()
#plt.plot((np.sqrt(dac_spec[:,0]**2 + dac_spec[:,1]**2)), c = 'g', label = 'C')
#plt.plot(luts.spec, c = 'r', label = 'Py')
plt.plot(DDSI[::1024][:1024], c = 'g', label = 'DDSI C')
plt.plot(luts.I_dds[::1024][:1024], c = 'r', label = 'DDSI Py')
plt.legend()

plt.figure(3)
plt.clf()
#plt.plot(dac_wave[:,0])
#plt.plot(dac_wave[:,1], label = 'dac_wav C')
plt.plot((np.sqrt(dds_spec[:,0]**2 + dds_spec[:,1]**2)), c = 'g', label = 'C')
plt.plot(luts.dds_spec, c = 'r', label = 'Py')
plt.legend()

#plt.plot(DACI[:100], c = 'g')
#plt.plot(DACQ[:100], c = 'r')
"""
plt.ion()
plt.figure(4)
plt.clf()
#plt.plot(I_dac[:100], c = 'g', label = 'Idac_packed')
#plt.plot(Q_dac[:100], c = 'r', label = 'Qdac_packed')
plt.plot(Q_dds[::1024], c = 'r', label = 'Qdds_packed')
plt.legend()
