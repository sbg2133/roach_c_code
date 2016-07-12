import sys
import valon_synth

v1 = valon_synth.Synthesizer('/dev/ttyUSB0')
lo_freq = float(sys.argv[1])

v1.set_frequency(0, lo_freq, 0.01)
