import numpy as np
import os

path = '/home/lazarus/sam_git/roach_c_code/iqstream/r2/vna/test'
bb_freqs, freq_step = np.linspace(-200.0e6, 200.0e6, 500, retstep = True)
def parse(path):
	
	files = sorted(os.listdir(path))
	lo_freqs = np.array([np.float(fname[1:-4]) for fname in files])
	print lo_freqs
	chan_I = np.zeros((len(lo_freqs),1024))
	chan_Q = np.zeros((len(lo_freqs),1024))
	
	for i in range(len(files)):
		chan_I[i], chan_Q[i] = np.loadtxt(os.path.join(path,fname), dtype = "float", delimiter = ",", usecols = (1,2), unpack = True, skiprows = 17) 

	return lo_freqs, chan_I, chan_Q

