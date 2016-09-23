import numpy as np
import sys, os
import matplotlib.pyplot as plt
import scipy.ndimage


path = sys.argv[1]
accum_len = 2**21
def openStored(path):
	files = sorted(os.listdir(path))
	I_list = [os.path.join(path, filename) for filename in files if filename.startswith('I')]
	Q_list = [os.path.join(path, filename) for filename in files if filename.startswith('Q')]
	chan_I = np.array([np.load(filename) for filename in I_list])
	chan_Q = np.array([np.load(filename) for filename in Q_list])
	return chan_I, chan_Q

def filter_trace(path, bb_freqs, lo_freqs):
	chan_I, chan_Q = openStored(path)
	channels = np.arange(np.shape(chan_I)[1])
	mag = np.zeros((len(channels),len(lo_freqs)))
	chan_freqs = np.zeros((len(channels),len(lo_freqs)))
	for chan in channels:
		mag[chan] = (np.sqrt(chan_I[:,chan]**2 + chan_Q[:,chan]**2)) 
		chan_freqs[chan] = (lo_freqs + bb_freqs[chan])/1.0e6
	mag = np.concatenate((mag[len(mag)/2:], mag[:len(mag)/2]))
	mags = np.hstack(mag)
	mags /= (2**17)
	mags /= (accum_len/ 512)
	mags = 20*np.log10(mags)
	chan_freqs = np.hstack(chan_freqs)
	chan_freqs = np.concatenate((chan_freqs[len(chan_freqs)/2:],chan_freqs[:len(chan_freqs)/2]))
	return chan_freqs, mags

def lowpass_cosine( y, tau, f_3db, width, padd_data=True):
	import numpy as nm
        # padd_data = True means we are going to symmetric copies of the data to the start and stop
	# to reduce/eliminate the discontinuities at the start and stop of a dataset due to filtering
	#
	# False means we're going to have transients at the start and stop of the data

	# kill the last data point if y has an odd length
	if nm.mod(len(y),2):
		y = y[0:-1]

	# add the weird padd
	# so, make a backwards copy of the data, then the data, then another backwards copy of the data
	if padd_data:
		y = nm.append( nm.append(nm.flipud(y),y) , nm.flipud(y) )

	# take the FFT
        import scipy
        import scipy.fftpack
	ffty=scipy.fftpack.fft(y)
	ffty=scipy.fftpack.fftshift(ffty)

	# make the companion frequency array
	delta = 1.0/(len(y)*tau)
	nyquist = 1.0/(2.0*tau)
	freq = nm.arange(-nyquist,nyquist,delta)
	# turn this into a positive frequency array
	pos_freq = freq[(len(ffty)/2):]

	# make the transfer function for the first half of the data
	i_f_3db = min( nm.where(pos_freq >= f_3db)[0] )
	f_min = f_3db - (width/2.0)
	i_f_min = min( nm.where(pos_freq >= f_min)[0] )
	f_max = f_3db + (width/2);
	i_f_max = min( nm.where(pos_freq >= f_max)[0] )

	transfer_function = nm.zeros(len(y)/2)
	transfer_function[0:i_f_min] = 1
	transfer_function[i_f_min:i_f_max] = (1 + nm.sin(-nm.pi * ((freq[i_f_min:i_f_max] - freq[i_f_3db])/width)))/2.0
	transfer_function[i_f_max:(len(freq)/2)] = 0

	# symmetrize this to be [0 0 0 ... .8 .9 1 1 1 1 1 1 1 1 .9 .8 ... 0 0 0] to match the FFT
	transfer_function = nm.append(nm.flipud(transfer_function),transfer_function)

	# apply the filter, undo the fft shift, and invert the fft
	filtered=nm.real(scipy.fftpack.ifft(scipy.fftpack.ifftshift(ffty*transfer_function)))

	# remove the padd, if we applied it
	if padd_data:
		filtered = filtered[(len(y)/3):(2*(len(y)/3))]

	# return the filtered data
        return filtered
	
def main(path):
	bb_freqs = np.load(os.path.join(path,'bb_freqs.npy'))
	lo_freqs = np.load(os.path.join(path,'sweep_freqs.npy'))
	
	sweep_step = 2.5 # kHz
	smoothing_scale = 100000.0 # kHz
	peak_threshold = 5. # mag units
	spacing_threshold = 1000.0 # kHz

	chan_freqs,mags = filter_trace(path, bb_freqs, lo_freqs)
	filtermags = lowpass_cosine( mags, sweep_step, 1./smoothing_scale, 0.1 * (1.0/smoothing_scale))
	#plotting 
	plt.ion()
	plt.figure(1)
	plt.clf()
	plt.plot(chan_freqs,mags,'b',label='#nofilter')
	plt.plot(chan_freqs,filtermags,'g',label='Filtered')
	plt.xlabel('frequency (MHz)')
	plt.ylabel('dB')
	plt.legend()

	plt.figure(2)
	plt.clf()
	plt.plot(chan_freqs,mags-filtermags,'b')
	ilo = np.where( (mags-filtermags) < -1.0*peak_threshold)[0]
	plt.plot(chan_freqs[ilo],mags[ilo]-filtermags[ilo],'r*')
	plt.xlabel('frequency (MHz)')

	iup = np.where( (mags-filtermags) > -1.0*peak_threshold)[0]
	new_mags = mags - filtermags
	new_mags[iup] = 0
	labeled_image, num_objects = scipy.ndimage.label(new_mags)
	indices = scipy.ndimage.measurements.minimum_position(new_mags,labeled_image,np.arange(num_objects)+1)
	kid_idx = np.array(indices, dtype = 'int')
	plt.figure(4)
	plt.clf()
	plt.plot(chan_freqs, mags,'b')
	plt.plot(chan_freqs[kid_idx], mags[kid_idx], 'r*')
	plt.xlabel('frequency (MHz)')
	plt.ylabel('dB')
	# list of kid frequencies
	target_freqs = chan_freqs[kid_idx]
	print len(target_freqs), "pixels found"
	print "Freqs =", chan_freqs[kid_idx]
	prompt = raw_input('Save target freqs in ' + path + ' (y/n) (may overwrite) ? ')
	if prompt == 'y':
		np.save(path + '/target_freqs.npy', chan_freqs[kid_idx])
	return
main(path)	
