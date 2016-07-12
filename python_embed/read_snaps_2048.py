import sys
import numpy as np
import matplotlib, time, struct
import matplotlib.pyplot as plt
import matplotlib.lines
matplotlib.use("TkAgg")
import casperfpga

class FirmwareSnaps(object):
    
    	def __init__(self):
		self.fpga = casperfpga.katcp_fpga.KatcpFpga("192.168.40.52",timeout=120.)
		self.dds_shift = 338
		self.accum_len = 2**19 
	
    	def menu(self,prompt,options):
        	print '\t' + prompt + '\n'
        	for i in range(len(options)):
            		print '\t' +  '\033[32m' + str(i) + ' ..... ' '\033[0m' +  options[i] + '\n'
        	opt = input()
        	return opt
    
	def plotADC(self):
		# Plots the ADC timestream
		# Peak to peak should be 900 mV (from DAC)
		fig = plt.figure(figsize=(20,12))
		ax = fig.add_subplot(211)    
		ax.tick_params(labelcolor='w', top='off', bottom='off', left='off', right='off')
		ax.spines['top'].set_color('none')
		ax.spines['bottom'].set_color('none')
		ax.spines['left'].set_color('none')
		ax.spines['right'].set_color('none')
		#ax.suptitle('ADC timestream', size = 20)
		ax.set_xlabel('ADC Clock Period (1/512 MHz =  2 ns)', size = 20)
		ax.set_ylabel('Amplitude (Volts)', size = 20)
		plot1 = fig.add_subplot(211)
		line1, = plot1.plot(np.arange(0,2048), np.zeros(2048), 'r-', linewidth = 2)
		plot1.set_title('I', size = 20)
		plt.xlim(0,1024)
		plt.ylim(-2.2,2.2)
		#plt.ylim(-2**12 - 1,2**12 -1)
		plt.grid()
		plot2 = fig.add_subplot(212)
		line2, = plot2.plot(np.arange(0,2048), np.zeros(2048), 'b-', linewidth = 2)
		plot2.set_title('Q', size = 20)
		plt.xlim(0,1024)
		plt.ylim(-2.2,2.2)
		#plt.ylim(-2**12-1,2**12-1)
		plt.grid()
		plt.tight_layout()
		plt.show(block = False)
		count = 0
		stop = 1.0e6
		while count < stop:    
			time.sleep(0.1)
			self.fpga.write_int('adc_snap_ctrl',0)
			self.fpga.write_int('adc_snap_ctrl',1)
			self.fpga.write_int('adc_snap_trig',0)    
			self.fpga.write_int('adc_snap_trig',1)    
			self.fpga.write_int('adc_snap_trig',0)
			adc = (np.fromstring(self.fpga.read('adc_snap_bram',(2**10)*8),dtype='>h')).astype('float')
			adc /= (2**12 -1)
			# ADC full scale is 2.2 V
			#adc *= 0.909091
			I = np.hstack(zip(adc[0::4],adc[1::4]))
			Q = np.hstack(zip(adc[2::4],adc[3::4]))
			#return I
			#raw_input()
			line1.set_ydata(I)
			line2.set_ydata(Q)
			plt.draw()
			count += 1
		return

	def read_mixer_snaps(self, shift, chan, mixer_out = True):
		# returns snap data for the dds mixer inputs and outputs
		self.fpga.write_int('dds_shift', shift)
		self.fpga.write_int('chan_select', chan)
		self.fpga.write_int('rawfftbin_ctrl', 0)
		self.fpga.write_int('mixerout_ctrl', 0)
		self.fpga.write_int('rawfftbin_ctrl', 1)
		self.fpga.write_int('mixerout_ctrl', 1)
		mixer_in = np.fromstring(self.fpga.read('rawfftbin_bram', 8*2**10),dtype='>h').astype('float')
		if mixer_out:
			mixer_out = np.fromstring(self.fpga.read('mixerout_bram', 4*2**10),dtype='>h').astype('float')
			return mixer_in, mixer_out
		else:
			return mixer_in
		
	def read_accum_snap(self):
		# Reads the avgIQ buffer. Returns I and Q as 32-b signed integers     
		self.fpga.write_int('accum_snap_ctrl', 0)
		self.fpga.write_int('accum_snap_ctrl', 1)
		accum_data = np.fromstring(self.fpga.read('accum_snap_bram', 8*2**10), dtype = '>i').astype('float')
		#accum_data /= (2.0**15 - 1)
		#accum_data /= ((self.accum_len)/1024.)
		#accum_data *= 1000. # put into mV
		I = accum_data[0::2]    
		Q = accum_data[1::2]    
		return I, Q    
		
	def read_accum_reg(self):
		count = 0
		while count < 1.0e6:
			i0 = np.fromstring(self.fpga.read('i0_accum', 4), dtype = '<i').astype('float') 
			i1 = np.fromstring(self.fpga.read('i1_accum', 4), dtype = '<i').astype('float') 
			q0 = np.fromstring(self.fpga.read('q0_accum', 4), dtype = '<i').astype('float') 
			q1 = np.fromstring(self.fpga.read('q1_accum3', 4), dtype = '<i').astype('float') 
			print  i0, i1, q0, q1
			if (i0[0] > (2**32 -1)) or (i1[0] > (2**32 -1)) or (q0[0] > (2**32 -1)) or (q1[0] > (2**32 -1)):
				print  i0, i1, q0, q1
				break
		return

	def plotAccum(self):
		# Generates a plot stream from read_avgIQ_snap(). To view, run plotAvgIQ.py in a separate terminal
		fig = plt.figure(num= None, figsize=(20,12))
		#plt.suptitle('Averaged FFT, Accum. Frequency = ' + str(self.accum_freq), fontsize=20)
		plot1 = fig.add_subplot(111)
		line1, = plot1.plot(np.arange(0,1024),np.zeros(1024), '#FF4500')
		line1.set_linestyle('None')
		line1.set_marker('.')
		plt.xlabel('Channel #',fontsize = 20)
		plt.ylabel('Amplitude [mV]',fontsize = 20)
		plt.xticks(np.arange(0,1024,5))
		plt.xlim(0,1024)
		plt.grid()
		plt.tight_layout()
		plt.show(block = False)
		count = 0 
		stop = 10000
		while(count < stop):
			I, Q = self.read_accum_snap()
			mags =(np.sqrt(I**2 + Q**2))
			plt.ylim((0,np.max(mags) + 0.001))
			line1.set_ydata(mags)
			plt.draw()
			count += 1
			#    plt.savefig('/home/user1/blastfirmware/images/' + 'accum' + str(int(count)) + '.png', dpi=fig.dpi)
		return

	def plotFFT(self):
		# Generates plot of the FFT output. 
		fig = plt.figure(num= None, figsize=(20,12), dpi=80, facecolor='w', edgecolor='w')
		plot1 = fig.add_subplot(111)
		line1, = plot1.plot( np.arange(0,2048), np.zeros(2048), '#FF4500')
		#line1.set_linestyle('None')
		#line1.set_marker('.')
		plt.xlabel('bin index',fontsize = 20)
		plt.ylabel('Amplitude [mV]',fontsize = 20)
		plt.title('Pre-DDS FFT',fontsize = 20)
		plt.xticks(np.arange(0,2048,10))
		plt.xlim((0,2048))
		plt.ylim((0,40))
		plt.tight_layout()
		plt.grid()
		plt.show(block = False)
		count = 0 
		stop = 1.0e6
		while(count < stop):
			overflow = np.fromstring(self.fpga.read('overflow', 4), dtype = '<B')
			print overflow
			self.fpga.write_int('fft_snap_ctrl',0)
			self.fpga.write_int('fft_snap_ctrl',1)
			fft_snap = (np.fromstring(self.fpga.read('fft_snap_bram',(2**10)*8),dtype='>i2')).astype('float')
			I0 = fft_snap[0::4]
			Q0 = fft_snap[1::4]
			I1 = fft_snap[2::4]
			Q1 = fft_snap[3::4]
			mag0 = np.sqrt(I0**2 + Q0**2)
			mag1 = np.sqrt(I1**2 + Q1**2)
			fft_mags = np.hstack(zip(mag0,mag1))
			fft_mags /= (2.0**18 - 1)
			fft_mags *= 1000. # put into mV
			#plt.ylim((0,np.max(fft_mags)))
			line1.set_ydata((fft_mags))
			plt.draw()
			#plt.savefig('/home/user1/blastfirmware/images/' + 'fft' + str(int(count)) + '.png', dpi=fig.dpi)
			count += 1
		return
    
	def mixer_comp(self,chan, find_shift = True):
		# Plots the dds mixer data at the shift found by return_shift     
		if find_shift:
			shift = self.return_shift(chan)
		else: 
			shift = self.dds_shift
		mixer_in, mixer_out = self.read_mixer_snaps(shift, chan)    
		I_in = mixer_in[0::4]
		Q_in = mixer_in[1::4]
		I_dds_in = mixer_in[2::4]
		Q_dds_in = mixer_in[3::4]
		I_out = mixer_out[0::2]
		Q_out = mixer_out[1::2]
		
		# Mixer in 
		I_out_guess = ((I_in * I_dds_in) + (Q_in * Q_dds_in))
		Q_out_guess = (-1.*(I_in * Q_dds_in) + (Q_in * I_dds_in))
		return I_in, Q_in, I_dds_in, Q_dds_in, I_out, Q_out
	    
	def plotMixer(self, chan):
		fig = plt.figure(figsize=(20,12))
		# I and Q
		plot1 = fig.add_subplot(311)
		plt.title('Chan ' + str(chan) + ' in', size = 20)
		line1, = plot1.plot(range(1024), np.zeros(1024), label = 'I in', color = 'green', linewidth = 2)
		line2, = plot1.plot(range(1024), np.zeros(1024), label = 'Q in', color = 'black', linewidth = 2)
		plt.xlim((0,1024))
		plt.ylim((-.05,.05))
		plt.grid()
		# DDS I and Q
		plot2 = fig.add_subplot(312)
		plt.title('DDS', size = 20)
		line3, = plot2.plot(range(1024), np.zeros(1024), label = 'I dds', color = 'red', linewidth = 2)
		line4, = plot2.plot(range(1024), np.zeros(1024), label = 'Q dds', color = 'black', linewidth = 2)
		plt.xlim((0,1024))
		plt.ylim((-1.0,1.0))
		plt.grid()
		# Mixer output
		plot3 = fig.add_subplot(313)
		plt.title('Chan out', size = 20)
		line5, = plot3.plot(range(1024), np.zeros(1024), label = 'I out', color = 'green', linewidth = 2)
		line6, = plot3.plot(range(1024), np.zeros(1024), label = 'Q out', color = 'black', linewidth = 2)
		plt.xlim((0,1024))
		plt.ylim(-1, 1)
		plt.tight_layout()
		plt.grid()
		plt.show(block = False)
		count = 0
		stop = 10000
		while (count < stop):
			I_in, Q_in, I_dds_in, Q_dds_in, I_out, Q_out = self.mixer_comp(chan, find_shift = False)
			I_out /= (2**16 -1)
			Q_out /= (2**16 -1)
			line1.set_ydata((I_in/(2**18 - 1)))
			line2.set_ydata((Q_in/(2**18 - 1)))
			plt.draw()
			line3.set_ydata((I_dds_in/(2**16 - 1)))
			line4.set_ydata((Q_dds_in/(2**16 - 1)))
			#plot2.set_ylim(( np.min(I_dds_in) - 1.0e-3 ,np.max(I_dds_in) + 1.0e-3))
			plt.draw()
			#mag = np.sqrt(((I_out**2 + Q_out**2)/(2**19 -1)))
			line5.set_ydata(I_out)
			line6.set_ydata(Q_out)
			plot3.set_ylim(( np.min(I_out) - 1.0e-2 ,np.max(I_out) + 1.0e-2))
			#line6.set_ydata(Q_out)
			
			plt.draw()
			#plt.savefig('/home/user1/blastfirmware/images/' + 'mixer0' + str(int(count)) + '.png', dpi=fig.dpi)
			count += 1
		return

	def plotPhase(self, chan):
		fig = plt.figure(figsize=(20,10))
		plt.suptitle('Channel ' + str(chan),size = 20) 
		plot1 = fig.add_subplot(211)
		plt.title('Phase (rad)', size = 20)
		line1, = plot1.plot(range(100), np.zeros(100), color = 'b', linewidth = 2)
		plt.grid()
		plot2 = fig.add_subplot(212)
		plt.title('Mag [mV]', size = 20)
		line2, = plot2.plot(range(100), np.zeros(100), color = 'g', linewidth = 2)
		plt.grid()
		chan_I = np.zeros(100)
		chan_Q = np.zeros(100)
		chan_phase = np.zeros(100)
		chan_mag = np.zeros(100)
		plt.show(block = False)
		while 1: 
			for count in range(100):
				time.sleep(0.1)
				I, Q = self.read_accum_snap()
				chan_phase[count] = np.arctan2(Q[chan],I[chan])
				#chan_I[count] = I[chan]
				#chan_Q[count] = Q[chan]
				chan_mag[count] = np.sqrt(I[chan]**2 + Q[chan]**2)
				#line1.set_ydata(chan_I)
				#line2.set_ydata(chan_Q)
				line1.set_ydata(chan_phase)
				plot1.set_ylim((np.min(chan_phase) - 0.001,np.max(chan_phase) + 0.001))
				line2.set_ydata(chan_mag)
				plot2.set_ylim((np.min(chan_mag) - 0.001,np.max(chan_mag) + 0.001))
				plt.draw()	
				#print 'Phase =', np.round(phase,10), I[chan], Q[chan]
				count += 1
		return 
	

	def plotPSD(self, chan, time_interval):
		Npackets = np.int(time_interval * self.accum_freq)
		plot_range = (Npackets / 2) + 1
		figure = plt.figure(num= None, figsize=(12,12), dpi=80, facecolor='w', edgecolor='w')
		# I 
		plt.suptitle('Channel ' + str(chan) + ' , Freq = ' + str((self.freqs[chan] + self.LO_freq)/1.0e6) + ' MHz') 
		plot1 = figure.add_subplot(311)
		plot1.set_xscale('log')
		plot1.set_autoscale_on(True)
		plt.ylim((-160,-80))
		plt.title('I')
		line1, = plot1.plot(np.linspace(0, self.accum_freq/2., (Npackets/2) + 1), np.zeros(plot_range), label = 'I', color = 'green', linewidth = 1)
		plt.grid()
		# Q
		plot2 = figure.add_subplot(312)
		plot2.set_xscale('log')
		plot2.set_autoscale_on(True)
		plt.ylim((-160,-80))
		plt.title('Q')
		line2, = plot2.plot(np.linspace(0, self.accum_freq/2., (Npackets/2) + 1), np.zeros(plot_range), label = 'Q', color = 'red', linewidth = 1)
		plt.grid()
		# Phase
		plot3 = figure.add_subplot(313)
		plot3.set_xscale('log')
		plot3.set_autoscale_on(True)
		plt.ylim((-120,-70))
		#plt.xlim((0.0001, self.accum_freq/2.))
		plt.title('Phase')
		plt.ylabel('dBc rad^2/Hz')
		plt.xlabel('log Hz')
		line3, = plot3.plot(np.linspace(0, self.accum_freq/2., (Npackets/2) + 1), np.zeros(plot_range), label = 'Phase', color = 'black', linewidth = 1)
		plt.grid()
		plt.show(block = False)
		count = 0
		stop = 1.0e10
		while count < stop:
			Is, Qs, phases = self.get_stream(chan, time_interval)
			I_mags = np.fft.rfft(Is, Npackets)
			Q_mags = np.fft.rfft(Is, Npackets)
			phase_mags = np.fft.rfft(phases, Npackets)
			I_vals = (np.abs(I_mags)**2 * ((1./self.accum_freq)**2 / (1.0*time_interval)))
			Q_vals = (np.abs(Q_mags)**2 * ((1./self.accum_freq)**2 / (1.0*time_interval)))
			phase_vals = (np.abs(phase_mags)**2 * ((1./self.accum_freq)**2 / (1.0*time_interval)))
			phase_vals = 10*np.log10(phase_vals)
			phase_vals -= phase_vals[0]
			#line1.set_ydata(Is)
			#line2.set_ydata(Qs)
			#line3.set_ydata(phases)
			line1.set_ydata(10*np.log10(I_vals))
			line2.set_ydata(10*np.log10(Q_vals))
			line3.set_ydata(phase_vals)
			plot1.relim()
			plot1.autoscale_view(True,True,False)
			plot2.relim()
			plot2.autoscale_view(True,True,False)
			#plot3.relim()
			plot3.autoscale_view(True,True,False)
			plt.draw()
			count +=1
		return
	

