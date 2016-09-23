import casperfpga, time
from myQdr import Qdr as myQdr

def qdrCal():    
	time.sleep(2)
	fpga = casperfpga.katcp_fpga.KatcpFpga("192.168.40.89",timeout=120.)
	bFailHard = False
	calVerbosity = 1
	qdrMemName = 'qdr0_memory'
	qdrNames = ['qdr0_memory','qdr1_memory']
	print 'Fpga Clock Rate =',fpga.estimate_fpga_clock()
	fpga.get_system_information()
	results = {}
	for qdr in fpga.qdrs:
    		print qdr
    		mqdr = myQdr.from_qdr(qdr)
    		results[qdr.name] = mqdr.qdr_cal2(fail_hard=bFailHard,verbosity=calVerbosity)
	print 'qdr cal results:',results
	for qdrName in ['qdr0','qdr1']:
    		if not results[qdr.name]:
			print 'Calibration Failed'
			break
	time.sleep(1)
	return

qdrCal()
