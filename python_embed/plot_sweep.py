import numpy as np
import os, sys
import matplotlib.pyplot as plt

bb_freqs, freq_step = np.linspace(-225.1213e6, 225.21234e6, 800, retstep = True)
path = sys.argv[1]
divisor = (2**20 - 1)/512.

def openStored(path):
	files = sorted(os.listdir(path))
	lo_freqs = np.sort(np.array([np.float(fname[1:-4]) for fname in files]))
	chan_I = np.zeros((len(lo_freqs),len(bb_freqs)))
	chan_Q = np.zeros((len(lo_freqs),len(bb_freqs)))
	
	for i in range(len(files)):
		I, Q = np.loadtxt(os.path.join(path,fname), dtype = "<i", delimiter = ",", usecols = (1,2), unpack = True, skiprows = 17) 
		I, Q = I[:len(bb_freqs)], Q[:len(bb_freqs)]
		chan_I[i] = I
		chan_Q[i] = Q
	return lo_freqs, chan_I, chan_Q

def filter_trace():
	lo_freqs, chan_I, chan_Q = openStored(path)
	channels = xrange(np.shape(chan_I)[1])
	sorted_freqs = np.zeros((len(channels),len(lo_freqs)))
	rf_freqs = np.sort(bb_freqs + 750.0e6)
	mag = np.zeros((len(channels),len(lo_freqs)))
	chan_freqs = np.zeros((len(channels),len(lo_freqs)))
	for chan in channels:
		mag[chan] = 20*np.log10(np.sqrt(chan_I[:,chan]**2 + chan_Q[:,chan]**2)) 
		chan_freqs[chan] = np.sort(lo_freqs + bb_freqs[chan])
	for chan in channels:
		if chan > 1:
			end_piece = mag[chan - 1][-1]
			offset = mag[chan][0] - end_piece
			mag[chan] = mag[chan] - offset
	mags = np.hstack(mag)
	chan_freqs = np.hstack(chan_freqs)
	return chan_freqs, mags, mag

chan_freqs,mags,mag = filter_trace()

plt.ion()
plt.figure(1)
plt.clf()
plt.plot(chan_freqs,mags)
plt.ylabel('dB')
plt.xlabel('freq')
