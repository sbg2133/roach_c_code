/*This software is a work in progress. It is a console interface designed 
to operate the BLAST-TNG ROACH2 firmware. 

Copyright (C) May 23, 2016  Gordon, Sam <sbgordo1@asu.edu>
Author: Gordon, Sam <sbgordo1@asu.edu>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netpacket/packet.h>
#include <net/ethernet.h> 
#include <net/if.h>
#include <string.h>
#include <arpa/inet.h>
#include <complex.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <fftw3.h>
//#include "crc.h"
//#include "portable_endian.h"
#include "netc.h"
#include "katcp.h"
#include "katcl.h"

#undef I
#define WRITE_INT_TIMEOUT 500
#define QDR_TIMEOUT 20000
#define M_PI 3.14159265358979323846
#define LUT_BUFFER_LEN 2097152
#define FPGA_SAMP_FREQ 256.0e6
#define DAC_SAMP_FREQ 512.0e6
#define DAC_FREQ_RES 244.140625

static int fft_len = 1024;
static double accum_len = ( 1 << 19 ) - 1;
const char test_fpg[] = "./roach2_8tap_wide_2016_Jun_25_2016.fpg";
double test_freq[] = {50.0125e6};

typedef enum {
    ROACH_STATUS_BOOT,
    ROACH_STATUS_CONNECTED,
    ROACH_STATUS_PROGRAMMED,
    ROACH_STATUS_TONE,
    ROACH_STATUS_DDS,
    ROACH_STATUS_STREAMING,
} e_roach_status;

typedef enum {
    ROACH_UPLOAD_RESULT_WORKING = 0,
    ROACH_UPLOAD_RESULT_TIMEOUT,
    ROACH_UPLOAD_RESULT_ERROR,
    ROACH_UPLOAD_RESULT_SUCCESS
} e_roach_upload_result;

typedef struct {
    size_t len;
    double *I;
    double *Q;
} roach_lut_t;

typedef struct {
	size_t len;
    	uint16_t *I;
    	uint16_t *Q;
} roach_uint16_lut_t;

typedef struct roach_state {
	int status;
	int desired_status;
	int has_error;
	const char *last_err;
	char *address;
	uint16_t port;
	double *freq_residuals;
	double *freq_comb;
	size_t freqlen;
	double *kid_freqs;
	size_t num_kids;
	double *test_freq;
	roach_lut_t DDS;
	roach_lut_t DAC;
	roach_uint16_lut_t LUT;
	char *vna_path;
	char *targ_path;
	char *channels_path;
	struct katcl_line *rpc_conn;
	int udp_sock;
} roach_state_t;

typedef struct {
	const char *firmware_file;
    	uint16_t port;
    	struct timeval timeout;
    	int result;
    	roach_state_t *roach;
} firmware_state_t;

typedef struct data_packet {
	unsigned char *rcv_buffer;
	struct ethhdr *eth;
	struct iphdr *ip; 
	float *I;
	float *Q;
	uint32_t checksum;
	uint32_t pps_count;
	uint32_t clock_count;
	uint32_t packet_count;
} data_packet_t;

/* Roach com functions */

static void roach_buffer_ntohl(uint32_t *m_buffer, size_t m_len)
{
    for (size_t i = 0; i < m_len; i++) {
        printf("%zu\n", i);
	m_buffer[i] = ntohl(m_buffer[i]);
    }
}

int roach_read_data(roach_state_t *m_roach, uint8_t *m_dest, const char *m_register,
                           uint32_t m_offset, uint32_t m_size, int ms_timeout )
{
    int retval = send_rpc_katcl(m_roach->rpc_conn, ms_timeout,
                   KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?read",
                   KATCP_FLAG_STRING, m_register,
                   KATCP_FLAG_ULONG, m_offset,
                   KATCP_FLAG_ULONG | KATCP_FLAG_LAST, m_size,
                   NULL);

    if (retval < 0) {
	return -1;
    }
    if (retval > 0) {
        char *ret = arg_string_katcl(m_roach->rpc_conn, 1);
	return -1;
    }

    if (arg_count_katcl(m_roach->rpc_conn) < 3) {
        printf("Error in read_data");
	return -1;
    	
    }

    uint32_t bytes_copied = arg_buffer_katcl(m_roach->rpc_conn, 2, m_dest, m_size);
    printf("Bytes copied: %u\n%c%c\n", bytes_copied,m_dest[0], m_dest[1]);

    if (bytes_copied != m_size) {
	printf("Bytes copied not equal to size: %d\n", m_size);
	return -1;
    printf("%p/n", &bytes_copied);

    }
    return 0;
}

int roach_read_int(roach_state_t *m_roach, const char *m_register)
{
  	uint32_t m_data;
	roach_read_data(m_roach, (uint8_t*) &m_data, m_register, 
						0, sizeof(m_data), 100);
	m_data = ntohl(m_data);
	return m_data;
}

static void roach_buffer_ntohs(uint16_t *m_buffer, size_t m_len)
{
    for (size_t i = 0; i < m_len; i++) {
        m_buffer[i] = ntohs(m_buffer[i]);
    }
}

int roach_write_data(roach_state_t *m_roach, const char *m_register, uint8_t *m_data,
                            size_t m_len, uint32_t m_offset, int m_timeout)
{
    return send_rpc_katcl(m_roach->rpc_conn, m_timeout,
                   KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?write",
                   KATCP_FLAG_STRING, m_register,
                   KATCP_FLAG_ULONG, m_offset,
                   KATCP_FLAG_BUFFER, m_data, m_len,
		   KATCP_FLAG_ULONG | KATCP_FLAG_LAST, m_len, NULL
                   );
}

int roach_write_int(roach_state_t *m_roach, const char *m_register, uint32_t m_val, uint32_t m_offset)
{
    uint32_t sendval = htonl(m_val);
    return roach_write_data(m_roach, m_register, (uint8_t*)&sendval, sizeof(sendval), m_offset, WRITE_INT_TIMEOUT);
}

int roach_upload_fpg(roach_state_t *m_roach, const char *m_filename)
{
	char *upload_command;
	firmware_state_t state = {
                              .firmware_file = m_filename,
                              .port = (uint16_t) (drand48() * 500.0 + 5000),
			      .timeout.tv_sec = 5,
                              .timeout.tv_usec = 0,
			      .roach = m_roach
    	};
    	printf("Getting permission to upload fpg...\n");
    	int retval = send_rpc_katcl(m_roach->rpc_conn, QDR_TIMEOUT,
                       KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?progremote",
                       KATCP_FLAG_ULONG | KATCP_FLAG_LAST, state.port,
                       NULL);
    	if (retval != KATCP_RESULT_OK) {
        return -1;
	printf("Failed\n");
    	}
    	printf("Uploading fpg through netcat...\n");
    	asprintf(&upload_command, "nc -w 2 %s %u < %s", m_roach->address, state.port, test_fpg);
    	int status = system(upload_command);
    	sleep(2);
	int success_val = send_rpc_katcl(m_roach->rpc_conn, 1000,
                KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?fpgastatus", 
		KATCP_FLAG_LAST | KATCP_FLAG_STRING, "",
		NULL);
    	while (success_val != KATCP_RESULT_OK) {
		usleep(1000);
	}
        char *ret = arg_string_katcl(m_roach->rpc_conn, 1);
	printf("FPGA programmed %s\n", ret);
	return 0;
}

static void roach_init_LUT(roach_state_t *m_roach, size_t m_len)
{
	m_roach->LUT.len = m_len;
	m_roach->LUT.I = calloc(m_len, sizeof(uint16_t));
	m_roach->LUT.Q = calloc(m_len, sizeof(uint16_t));
}

static void roach_init_DACDDS_LUTs(roach_state_t *m_roach, size_t m_len)
{
	m_roach->DAC.len = m_len;
	m_roach->DAC.I = calloc(m_len, sizeof(double));
	m_roach->DAC.Q = calloc(m_len, sizeof(double));
	m_roach->DDS.len = m_len;
	m_roach->DDS.I = calloc(m_len, sizeof(double));
	m_roach->DDS.Q = calloc(m_len, sizeof(double));
}

static inline int roach_fft_bin_index(double *m_freqs, size_t m_index, size_t m_fft_len, double m_samp_freq)
{
	return (int)lround(m_freqs[m_index] / m_samp_freq *  m_fft_len);
}

static int roach_dac_comb(roach_state_t *m_roach, double *m_freqs, size_t m_freqlen,
                            int m_samp_freq, double *m_I, double *m_Q)
{
	size_t comb_fft_len;
    	fftw_plan comb_plan;
    	for (size_t i = 0; i < m_freqlen; i++) {
        	m_freqs[i] = round(m_freqs[i] / DAC_FREQ_RES) * DAC_FREQ_RES;
		//printf("freq remainder: %0.9g\n", m_freqs[i]/DAC_FREQ_RES);
   	}
    	comb_fft_len = LUT_BUFFER_LEN;
    	complex double *spec = calloc(comb_fft_len,  sizeof(complex double));
    	complex double *wave = calloc(comb_fft_len,  sizeof(complex double));
    	double max_val = 0.0;
    	srand48(time(NULL));
    	for (size_t i = 0; i < m_freqlen; i++) {
		int bin = roach_fft_bin_index(m_freqs, i, comb_fft_len, m_samp_freq);
		if (bin < 0) {
			bin += comb_fft_len;
		}
		spec[bin] = cexp(_Complex_I * drand48() * 2.0 * M_PI);
    	}		
    	FILE *f1 = fopen("./dac_spec.txt", "w");
    	for (size_t i = 0; i < comb_fft_len; i++){
    		fprintf(f1,"%f, %f\n", creal(spec[i]), cimag(spec[i])); 
    	} 
     	fclose(f1);
    	FILE *f2 = fopen("./dac_wave.txt", "w");
    	comb_plan = fftw_plan_dft_1d(comb_fft_len, spec, wave, FFTW_BACKWARD, FFTW_ESTIMATE);
    	fftw_execute(comb_plan);
    	fftw_destroy_plan(comb_plan);
    	for (size_t i = 0; i < comb_fft_len; i++) {
    		wave[i] /= comb_fft_len;
    		fprintf(f2,"%f, %f\n", creal(wave[i]), cimag(wave[i])); 
        	if (cabs(wave[i]) > max_val) max_val = cabs(wave[i]);
    	}
    	fclose(f2);
    	for (size_t i = 0; i < comb_fft_len; i++) {
		m_I[i] = (creal(wave[i]) / max_val) * ((1<<15)-1);
        	m_Q[i] = (cimag(wave[i]) / max_val) * ((1<<15)-1);
    	}
    	free(spec);
    	free(wave);
    	return 0;
}

static int roach_dds_comb(roach_state_t *m_roach, double m_freqs, size_t m_freqlen,
                            int m_samp_freq, int m_bin, double *m_I, double *m_Q)
{
	size_t comb_fft_len;
    	fftw_plan comb_plan;
	comb_fft_len = LUT_BUFFER_LEN / fft_len;
    	complex double *spec = calloc(comb_fft_len,  sizeof(complex double));
    	complex double *wave = calloc(comb_fft_len,  sizeof(complex double));
    	double max_val = 0.0;
	spec[m_bin] = cexp(_Complex_I * 0.);	
	FILE *f3 = fopen("./dds_spec.txt", "w");
    	for (size_t i = 0; i < comb_fft_len; i++){
    		fprintf(f3,"%f, %f\n", creal(spec[i]), cimag(spec[i])); 
     	 }	
	fclose(f3); 
    	FILE *f4 = fopen("./dds_wave.txt", "w");
    	comb_plan = fftw_plan_dft_1d(comb_fft_len, spec, wave, FFTW_BACKWARD, FFTW_ESTIMATE);
    	fftw_execute(comb_plan);
    	fftw_destroy_plan(comb_plan);
	for (size_t i = 0; i < comb_fft_len; i++) {
    		wave[i] /= comb_fft_len;
    		fprintf(f4,"%f, %f\n", creal(wave[i]), cimag(wave[i])); 
        	if (cabs(wave[i]) > max_val) max_val = cabs(wave[i]);
    	}
	fclose(f4);
    	for (size_t i = 0; i < comb_fft_len; i++) {
		m_I[i] = creal(wave[i]) / max_val * ((1<<15)-1);
        	m_Q[i] = cimag(wave[i]) / max_val * ((1<<15)-1);
    	}
    	free(spec);
    	free(wave);
    	return 0;
}

static void roach_define_DAC_LUT(roach_state_t *m_roach, double *m_freqs, size_t m_freqlen)
{
	if (m_roach->DAC.len > 0 && m_roach->DAC.len != LUT_BUFFER_LEN) {
		free(m_roach->DAC.I);
        	free(m_roach->DAC.Q);
        	m_roach->DAC.len = 0;
    	}
    	if (m_roach->DAC.len == 0) {
        	m_roach->DAC.I = calloc(LUT_BUFFER_LEN, sizeof(double));
        	m_roach->DAC.Q = calloc(LUT_BUFFER_LEN, sizeof(double));
        	m_roach->DAC.len = LUT_BUFFER_LEN;
    	}
    	roach_dac_comb(m_roach, m_freqs, m_freqlen,
                    DAC_SAMP_FREQ, m_roach->DAC.I, m_roach->DAC.Q);
}

static void roach_select_bins(roach_state_t *m_roach, double *m_freqs, size_t m_freqlen)
{
    	int bins[fft_len];
    	double bin_freqs[fft_len];

	m_roach->freq_residuals = malloc(m_freqlen * sizeof(size_t));
    	for (size_t i = 0; i < m_freqlen; i++) {
		bins[i] = roach_fft_bin_index(m_freqs, i, fft_len, DAC_SAMP_FREQ);
		bin_freqs[i] = bins[i] * DAC_SAMP_FREQ / fft_len;
		if (bins[i] < 0) {
			bins[i] += 1024;
		}
		m_roach->freq_residuals[i] = round((m_freqs[i] - bin_freqs[i]) / DAC_FREQ_RES) * DAC_FREQ_RES;
		printf("bin, fbin, freq, offset: %d, %g, %0.9g, %0.9g\n", bins[i], bin_freqs[i]/1.0e6, m_freqs[i]/1.0e6, m_roach->freq_residuals[i]);
    	}

    	for (int ch = 0; ch < m_freqlen; ch++) {
        	roach_write_int(m_roach, "bins", bins[ch], 0);
        	roach_write_int(m_roach, "load_bins", 2*ch + 1, 0);
        	roach_write_int(m_roach, "load_bins", 0, 0);
    	}

}

void roach_define_DDS_LUT(roach_state_t *m_roach, double *m_freqs, size_t m_freqlen)
{
    	roach_select_bins(m_roach, m_freqs, m_freqlen);
    	if (m_roach->DDS.len > 0 && m_roach->DDS.len != LUT_BUFFER_LEN) {
        	free(m_roach->DDS.I);
        	free(m_roach->DDS.Q);
        	m_roach->DDS.len = 0;
    	}
    	if (m_roach->DDS.len == 0) {
        	m_roach->DDS.I = calloc(LUT_BUFFER_LEN, sizeof(double));
        	m_roach->DDS.Q = calloc(LUT_BUFFER_LEN, sizeof(double));
        	m_roach->DDS.len = LUT_BUFFER_LEN;
    	}
    	for (size_t i = 0; i < m_freqlen; i++){
		double I[2 * fft_len];
    		double Q[2 * fft_len];
		int bin = roach_fft_bin_index(m_roach->freq_residuals, i, LUT_BUFFER_LEN / fft_len, FPGA_SAMP_FREQ / (fft_len / 2));
		if (bin < 0) {
			bin += 2048;
		}
		roach_dds_comb(m_roach, m_roach->freq_residuals[i], m_freqlen,
                        FPGA_SAMP_FREQ / (fft_len / 2), bin, I, Q);
		for (int j = i, k = 0; k < 2*fft_len; j += fft_len, k++){
			m_roach->DDS.I[j] = I[k];
        		m_roach->DDS.Q[j] = Q[k]; 
    		}	
    	}
}

void roach_pack_LUTs(roach_state_t *m_roach, double *m_freqs, size_t m_freqlen)
{
   /* if (m_roach->LUT.len > 0 && m_roach->LUT.len != LUT_BUFFER_LEN)  {
    	printf("Attempting to free luts\t");
	free(m_roach->LUT.I);
        free(m_roach->LUT.Q);
    }*/
   	roach_init_LUT(m_roach, 2 * LUT_BUFFER_LEN);
	for (size_t i = 0; i < LUT_BUFFER_LEN; i += 2) {
        	m_roach->LUT.I[2 * i + 0] = htons(m_roach->DAC.I[i + 1]);
        	m_roach->LUT.I[2 * i + 1] = htons(m_roach->DAC.I[i]);
        	m_roach->LUT.I[2 * i + 2] = htons(m_roach->DDS.I[i + 1]);
		m_roach->LUT.I[2 * i + 3] = htons(m_roach->DDS.I[i]);
		m_roach->LUT.Q[2 * i + 0] = htons(m_roach->DAC.Q[i + 1]);
		m_roach->LUT.Q[2 * i + 1] = htons(m_roach->DAC.Q[i]);
		m_roach->LUT.Q[2 * i + 2] = htons(m_roach->DDS.Q[i + 1]);
		m_roach->LUT.Q[2 * i + 3] = htons(m_roach->DDS.Q[i]);
	}
}

void save_luts(roach_state_t *m_roach)
{
	FILE *f1 = fopen("./DACI.txt", "w");
    	FILE *f2 = fopen("./DACQ.txt", "w");
   	FILE *f3 = fopen("./DDSI.txt", "w");
   	FILE *f4 = fopen("./DDSQ.txt", "w");
	for (size_t i = 0; i < LUT_BUFFER_LEN; i++){
    		fprintf(f1,"%g\n", m_roach->DAC.I[i]); 
    		fprintf(f2,"%g\n", m_roach->DAC.Q[i]); 
    		fprintf(f3,"%g\n", m_roach->DDS.I[i]); 
    		fprintf(f4,"%g\n", m_roach->DDS.Q[i]); 
    	}   
    	fclose(f1);
    	fclose(f2);
    	fclose(f3);
    	fclose(f4);
	printf("LUTs written to disk\n");
}

void save_packed_luts(roach_state_t *m_roach)
{
	FILE *f1 = fopen("./QDRI.txt", "w");
    	FILE *f2 = fopen("./QDRQ.txt", "w");
    	
	for (size_t i = 0; i < 2*LUT_BUFFER_LEN; i++){
    		fprintf(f1,"%d\n", ntohs(m_roach->LUT.I[i])); 
    		fprintf(f2,"%d\n", ntohs(m_roach->LUT.Q[i])); 
    	}   
    	fclose(f1);
    	fclose(f2);
}

void roach_write_QDR(roach_state_t *m_roach, double *m_freqs, size_t m_freqlen)
{
	roach_pack_LUTs(m_roach, m_freqs, m_freqlen);
	roach_write_int(m_roach, "dac_reset", 1, 0);
	roach_write_int(m_roach, "dac_reset", 0, 0);
	roach_write_int(m_roach, "start_dac", 0, 0);
	if (roach_write_data(m_roach, "qdr0_memory", (uint8_t*)m_roach->LUT.I, m_roach->LUT.len * sizeof(uint16_t), 0, 20000) < 0) { printf("Could not write to qdr0!");}
	roach_write_data(m_roach, "qdr1_memory", (uint8_t*)m_roach->LUT.Q, m_roach->LUT.len * sizeof(uint16_t), 0, 20000);
	roach_write_int(m_roach, "start_dac", 1, 0);
	roach_write_int(m_roach, "sync_accum_reset", 0, 0);
	roach_write_int(m_roach, "sync_accum_reset", 1, 0);
}

void roach_write_tones(roach_state_t *m_roach, double *m_freqs, size_t m_freqlen)
{	
	printf("Allocating memory for LUTs...\n");
	roach_init_DACDDS_LUTs(m_roach, LUT_BUFFER_LEN);
	printf("Defining DAC LUT...\t");
	roach_define_DAC_LUT(m_roach, m_freqs, m_freqlen);
	printf("Done\n"); 
	printf("Defining DDS LUT...\t"); 
	roach_define_DDS_LUT(m_roach, m_freqs, m_freqlen);
	printf("Done\n");
	printf("Uploading LUTs to QDR RAM...\t");
	roach_write_QDR(m_roach, m_freqs, m_freqlen);
}

static int roach_read_QDR(roach_state_t *m_roach, uint16_t *m_qdr_I, uint16_t *m_qdr_Q)
{
	size_t buffer_len = (1<<22);
	int16_t *temp_data_I;
	int16_t *temp_data_Q;

	temp_data_I = calloc(sizeof(int16_t), buffer_len);
	if (roach_read_data(m_roach, (uint8_t*)temp_data_I, "qdr0_memory", 0, buffer_len * sizeof(uint16_t), 20000)) {
	free(temp_data_I);
	return -1;
	}
	temp_data_Q = calloc(sizeof(int16_t), buffer_len);
	if (roach_read_data(m_roach, (uint8_t*)temp_data_Q, "qdr1_memory", 0, buffer_len * sizeof(uint16_t), 20000)) {
	free(temp_data_Q);
	return -1;
	}
	roach_buffer_ntohs((uint16_t*)temp_data_I, buffer_len);
	roach_buffer_ntohs((uint16_t*)temp_data_Q, buffer_len);
	for (size_t i = 0; i < buffer_len; i++) {
	m_qdr_I[i] = temp_data_I[i];
	m_qdr_Q[i] = temp_data_Q[i];
	}
	free(temp_data_I);
	free(temp_data_Q);
	return 0;
}

static int roach_save_1d(const char *m_filename, void *m_data, size_t m_element_size, size_t m_len)
{
	//uint32_t channel_crc;
	FILE *fp;
	//channel_crc = crc32(BLAST_MAGIC32, m_data, m_element_size * m_len);
	fp = fopen(m_filename, "w");
	fwrite(&m_len, sizeof(size_t), 1, fp);
	fwrite(m_data, m_element_size, m_len, fp);
	//fwrite(&channel_crc, sizeof(channel_crc), 1, fp);
	fclose(fp);
	return 0;
}

static ssize_t roach_load_1d(const char *m_filename, void *m_data, size_t m_element_size)
{
	size_t len;
	FILE *fp;
	struct stat fp_stat;
	size_t res;
	//uint32_t channel_crc;
	if (stat(m_filename, &fp_stat)) {
	printf("Could not get file data for %s", m_filename);
	return -1;
	}
	if (!(fp = fopen(m_filename, "r"))) {
	printf("Could not open %s for reading", m_filename);
	return -1;
	}
	if (fread(&len, sizeof(len), 1, fp) != 1) {
	printf("Could not read data length from %s", m_filename);
	fclose(fp);
	return -1;
	}
	res = fread(m_data, m_element_size, len, fp);
	//for (size_t i = 0; i < len; i++) {
	//	printf("%g\n", (double)&m_data[i]);
	//}
	fclose(fp);
	return len;
}

int init_socket(roach_state_t *m_roach)
{
	int sock;
	struct sockaddr saddr;
	int saddr_len = sizeof(saddr);
	if ((sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP))) < 0) {
		perror("cannot create socket\n");
		return -1;
	}
	struct sockaddr_ll sll;
	struct ifreq ifr;
	bzero(&sll, sizeof(sll));
	bzero(&ifr, sizeof(ifr));
	/* First Get the Interface Index */
	strncpy((char *)ifr.ifr_name, "eth0", IFNAMSIZ);
	if((ioctl(sock, SIOCGIFINDEX, &ifr)) == -1){
		perror("Error getting Interface index !\n");
		exit(-1);
	}
	/* Bind our raw socket to this interface */
	sll.sll_family = AF_PACKET;
	sll.sll_ifindex = ifr.ifr_ifindex;
	sll.sll_protocol = htons(ETH_P_ALL);
	if((bind(sock, (struct sockaddr *)&sll, sizeof(sll)))== -1){
		perror("Error binding raw socket to interface\n");
		exit(-1);
	}	
	return sock;
}

int fill_packet_buffer(roach_state_t *m_roach, data_packet_t *m_packet)	
{
	int buflen;
	//m_packet->rcv_buffer = (unsigned char *) malloc(8234); 
	m_packet->rcv_buffer = calloc(8234, sizeof(uint8_t));
	memset(m_packet->rcv_buffer,0,8234);
	buflen = recvfrom(m_roach->udp_sock, m_packet->rcv_buffer, 8234, 0, NULL, NULL);
	if(buflen<0){
		printf("error in reading recvfrom function\n");
		return -1;
	}
	//printf("Received %d bytes\n", buflen);
	return 0;
}

void parse_packet(data_packet_t *m_packet)
{
	m_packet->I = calloc(1024, sizeof(float));
        m_packet->Q = calloc(1024, sizeof(float));
	uint8_t *payload = (uint8_t *)(m_packet->rcv_buffer);
	uint8_t *data = (payload + 42);
	m_packet->checksum = (data[8176] << 24) | (data[8177] << 16) | (data[8178] << 8) | data[8179];
	m_packet->pps_count = (data[8180] << 24) | (data[8181] << 16) | (data[8182] << 8) | data[8183];
	m_packet->clock_count = (data[8184] << 24) | (data[8185] << 16) | (data[8186] << 8) | data[8187];
	m_packet->packet_count = (data[8188] << 24) | (data[8189] << 16) | (data[8190] << 8) | data[8191];
	// I, Q
	for (int i = 0;	i < 1024; i += 1) {
		int j;
		int k;
		if ((i % 2) == 0) {
			j = ( (i*4) /2 );
			k = 512*4 + ( (i*4) /2 );
		} else {
			j = 1024*4 + (((i*4) - 1) / 2) - 1;
			k = 1536*4 + (((i*4) - 1) / 2) - 1;
		}
		m_packet->I[i] = (float)(int32_t)(ntohl((data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3])));
		m_packet->Q[i] = (float)(int32_t)(ntohl((data[k] << 24) | (data[k + 1] << 16) | (data[k + 2] << 8) | (data[k + 3])));
		//printf("%d\t %d\t %d\t %d\t %d\t\n", i, j, j + 1, j + 2, j + 3);
		//printf("%d\t %d\t %d\t %d\t %d\t\n", i, k, k + 1, k + 2, k + 3);
	}
}

int stream_packets(roach_state_t *m_roach, size_t m_num_packets, int m_chan)
{
	for (size_t i = 0; i < m_num_packets; i++){	
		data_packet_t m_packet;
		fill_packet_buffer(m_roach, &m_packet);
		parse_packet(&m_packet);
		fflush(stdout);
		printf("%u\t", m_packet.packet_count); 	
		printf("%d\t", m_chan);
		printf("%f\t%f\t", m_packet.I[m_chan], m_packet.Q[m_chan]);
		float chan_phase = atan2( m_packet.Q[m_chan], m_packet.I[m_chan]); 
		printf("%g\t\n", chan_phase);	
		free(m_packet.I);
		free(m_packet.Q);
		//printf("%u\t", m_packet.checksum); 	
		//printf("%u\t", m_packet.pps_count); 	
		//printf("%u\t", m_packet.clock_count); 	
	}		
	return 0;
}

void write_packet(roach_state_t *m_roach, data_packet_t *m_packet, FILE *m_fd)
{
	int rc = fill_packet_buffer(m_roach, m_packet);
	m_packet->eth  = (struct ethhdr *)(m_packet->rcv_buffer);
	fprintf(m_fd,"Source MAC : %.2X-%.2X-%.2X-%.2X-%.2X-%.2X\n",m_packet->eth->h_source[0],m_packet->eth->h_source[1],m_packet->eth->h_source[2],m_packet->eth->h_source[3],m_packet->eth->h_source[4],m_packet->eth->h_source[5]);	
	unsigned short iphdrlen;
	struct sockaddr_in source, dest;
	m_packet->ip = (struct iphdr *)(m_packet->rcv_buffer + sizeof(struct ethhdr));
	memset(&source, 0, sizeof(source));
	source.sin_addr.s_addr = m_packet->ip->saddr;
	memset(&dest, 0, sizeof(dest));
	dest.sin_addr.s_addr = m_packet->ip->daddr;
	fprintf(m_fd,"Version : %d\n",(unsigned int)m_packet->ip->version);
	fprintf(m_fd,"Internet Header Length : %d DWORDS or %d Bytes\n",(unsigned int)m_packet->ip->ihl,((unsigned int)(m_packet->ip->ihl))*4);
	fprintf(m_fd,"TOS : %d\n",(unsigned int)m_packet->ip->tos);
	fprintf(m_fd,"Total Length : %d Bytes\n",ntohs(m_packet->ip->tot_len));
	fprintf(m_fd,"ID : %d\n",ntohs(m_packet->ip->id));
	fprintf(m_fd,"TTL : %d\n",(unsigned int)m_packet->ip->ttl);
	fprintf(m_fd,"Protocol : %d\n",(unsigned int)m_packet->ip->protocol);
	fprintf(m_fd,"Header Checksum : %d\n",ntohs(m_packet->ip->check));
	fprintf(m_fd,"Source IP : %s\n", inet_ntoa(source.sin_addr));
	fprintf(m_fd,"Dest IP : %s\n\n",inet_ntoa(dest.sin_addr));		
	parse_packet(m_packet);
	fprintf(m_fd,"Firmware Checksum : %u\n", m_packet->checksum);	
	fprintf(m_fd, "PPS Count : %u\n", m_packet->pps_count);	
	fprintf(m_fd, "Clock Count : %u\n", m_packet->clock_count);	
	fprintf(m_fd, "Packet Num : %u\n\n", m_packet->packet_count);	
	for (int i = 0; i < 1024; i++){
		fprintf(m_fd, "%d, %f\t, %f\n", i, m_packet->I[i], m_packet->Q[i]);	
	}
	free(m_packet->I);
	free(m_packet->Q);
}

void save_packets(roach_state_t *m_roach, size_t m_num_packets, double m_filetag, const char *m_savepath, const char *m_pathtag)
{	
	char fname[FILENAME_MAX];
	for (size_t i = 0; i < m_num_packets; i++){
		data_packet_t packet;
		snprintf(fname, sizeof(fname), "%s/%s/p%3g.dat", m_savepath, m_pathtag, m_filetag);
		FILE *fd = fopen(fname, "w");
		write_packet(m_roach, &packet, fd);
		fclose(fd);
	}
}

void roach_freq_comb(roach_state_t *m_roach)
{
	size_t m_freqlen = 100;
	double p_max_freq = 245.001234e6;
	double p_min_freq = 10.02342e6;
	double n_max_freq = -10.02342e6;
	double n_min_freq = -245.001234e6+5.1123e4;
	m_roach->freq_comb = calloc(m_freqlen, sizeof(double));
	m_roach->freqlen = m_freqlen;
	/* positive freqs */
	printf("p freqs: %0.9g, %0.9g\n", p_max_freq/1.0e6, p_min_freq/1.0e6);
	double p_delta_f = (n_max_freq - n_min_freq) / ((m_freqlen/2) - 1);
	printf("p delta f: %0.9g\n", p_delta_f);
	for (size_t i = m_freqlen/2; i-- > 0;) {
		m_roach->freq_comb[m_freqlen/2 - (i + 1)] = p_max_freq - i*p_delta_f;
		printf("%g\n",m_roach->freq_comb[m_freqlen/2 - (i + 1)]);
	}
	/* negative freqs */
	printf("n freqs: %0.9g, %0.9g\n", n_max_freq/1.0e6, n_min_freq/1.0e6);
	double n_delta_f = (n_max_freq - n_min_freq) / ((m_freqlen/2) - 1);
	printf("n delta f: %0.9g\n", n_delta_f);
	for (size_t i = 0; i < m_freqlen/2; i++) {
		m_roach->freq_comb[i + m_freqlen/2] = n_min_freq + i*n_delta_f;
		printf("%g\n",m_roach->freq_comb[i + m_freqlen/2]);
	}
}

void sweep_lo(roach_state_t *m_roach, double m_centerfreq, double m_span, double delta_f, bool save, const char *m_savepath, const char *m_filetag)
{
	double min_freq = m_centerfreq - (m_span/2);
	double max_freq = m_centerfreq + (m_span/2);
	double *sweep_freqs;
	double megahertz = 1.0e6;
	size_t num_freqs = (max_freq - min_freq) / delta_f;
	char command[FILENAME_MAX];
	sweep_freqs = calloc(num_freqs, sizeof(double));
	sweep_freqs[0] = min_freq;
	for (size_t i = 1; i < num_freqs; i++) {
		sweep_freqs[i] = sweep_freqs[i - 1] + delta_f;
	}
	for (size_t i = 0; i < num_freqs; i++) {
		snprintf(command,sizeof(command),"python ./python_embed/set_lo.py %g", sweep_freqs[i]/megahertz);
		//printf("%s\n", command);
		fflush(stdout);
		system(command);
		printf("LO @ %g\n", sweep_freqs[i]);
		if (save) {
			save_packets(m_roach, 1, sweep_freqs[i], m_savepath, m_filetag);
		}
		snprintf(command,sizeof(command),"python ./python_embed/set_lo.py %g", m_centerfreq/megahertz);
		system(command);
	}
	free(sweep_freqs);
}

void roach_sweep(roach_state_t *m_roach, double m_centerfreq, const char *m_savepath, const char *m_packetdir, 
									bool vna, bool write, bool plot)
{
	if (vna) {
		char fullpath[FILENAME_MAX];
		double *rf_freqs;
		int *channels;
		rf_freqs = calloc(m_roach->freqlen, sizeof(double));
		channels = calloc(m_roach->freqlen, sizeof(int));
		roach_freq_comb(m_roach);
		for (size_t i = 0; i < m_roach->freqlen; i++) {
			channels[i] = i;
			rf_freqs[i] = m_roach->freq_comb[i] + m_centerfreq;
		}
		snprintf(fullpath, sizeof(fullpath), "%s/last_bb_freqs.dat", m_savepath);
		printf("Wrote %s\n", fullpath);
		roach_save_1d(fullpath, m_roach->freq_comb, sizeof(*m_roach->freq_comb), m_roach->freqlen);
		snprintf(fullpath, sizeof(fullpath), "%s/last_rf_freqs.dat", m_savepath);
		printf("Wrote %s\n", fullpath);
		roach_save_1d(fullpath, rf_freqs, sizeof(*rf_freqs), m_roach->freqlen);
		snprintf(fullpath, sizeof(fullpath), "%s/last_channels.dat", m_savepath);
		printf("Wrote %s\n", fullpath);
		roach_save_1d(fullpath, channels, sizeof(*channels), m_roach->freqlen);
		if (write) {
			roach_write_tones(m_roach, m_roach->freq_comb, m_roach->freqlen);
		}	
		sweep_lo(m_roach, m_centerfreq, 5.0e5, 5.0e3, 1, m_savepath, m_packetdir);
		
	} else {
		FILE *m_fd;
		m_fd = fopen("/home/lazarus/sam_git/BLAST_TNG_devel/kid_freqs.dat", "r");
		double m_kid_freqs[m_roach->num_kids];	
		size_t i = 0;
		while(!feof(m_fd)) {
			fscanf(m_fd,"%lg\n", &m_kid_freqs[i]); 
			printf("%g\n", m_kid_freqs[i]);
			//m_roach->kid_freqs[i] = m_kid_freqs[i];
			i++;
		}
		fclose(m_fd);
		// reorder frequencies
		if (write) {
			roach_write_tones(m_roach, m_kid_freqs, m_roach->num_kids);
		}
		sweep_lo(m_roach, m_centerfreq, 1.0e5, 2.5e3, 1, m_savepath, m_packetdir);
	}
	if (plot) {
		char command[FILENAME_MAX];
		snprintf(command,sizeof(command),"python -i ./python_embed/plot_sweep.py %s/%s", m_savepath, m_packetdir);
		printf("%s\n", command);
		fflush(stdout);
		system(command);
	}
}

void get_kid_freqs(roach_state_t *m_roach, const char *m_packetpath)
{
	//char fullpath[FILENAME_MAX];
	char command[FILENAME_MAX];
	snprintf(command,sizeof(command),"python -i ./python_embed/find_kids_blast.py %s", m_packetpath);
	printf("%s\n", command);
	fflush(stdout);
	system(command);
	//size_t num_freqs = sizeof(kid_freqs)/sizeof(double);
	//snprintf(fullpath, sizeof(fullpath), "%s/last_kid_freqs.dat", m_savepath);
	//printf("Wrote %s\n", fullpath);
	//roach_save_1d(fullpath, kid_freqs, sizeof(*kid_freqs), num_freqs);
}

void init_roach(roach_state_t *m_roach)
{
	printf("Setting Valon... ");	
	system ("python ./python_embed/set_valon.py");
	printf("Done\n");	
	uint32_t dest_ip = 192*pow(2,24) + 168*pow(2,16) + 41*pow(2,8) + 2;
	uint16_t udp_port = 60001;
    	//srand48(time(NULL));
        //m_roach->port = (uint16_t) (drand48() * 500.0 + 50000),
	//printf("%u\n", m_roach->port);
	/* Set software registers */
	printf("Setting software registers to default values...\n");
	roach_write_int(m_roach, "tx_destport", udp_port, 0);		/* UDP port */
	uint16_t port = roach_read_int(m_roach, "tx_destport");
	printf("udp port = %u\n", port); 
	roach_write_int(m_roach, "dds_shift", 305, 0);	/* DDS LUT shift, in clock cycles */
	int dds_shift = roach_read_int(m_roach, "dds_shift");
	printf("dds shift = %u\n", dds_shift); 
	roach_write_int(m_roach, "fft_shift", 255, 0);	/* FFT shift schedule */
	int fft_shift = roach_read_int(m_roach, "fft_shift");
	printf("fft shift = %u\n", fft_shift); 
	roach_write_int(m_roach, "sync_accum_len", 524287, 0); /* Number of accumulations */
	int sync_accum_len = roach_read_int(m_roach, "sync_accum_len");
	printf("accumulation length = %u\n", sync_accum_len); 
	roach_write_int(m_roach, "tx_destip", dest_ip, 0);		/* UDP destination IP */
	int udp_ip = roach_read_int(m_roach, "tx_destip");
	struct in_addr ip_addr;
    	ip_addr.s_addr = ntohl(udp_ip);
	printf("udp ip = %s\n", inet_ntoa(ip_addr)); 
	roach_write_int(m_roach, "pps_start", 1, 0);
	roach_write_int(m_roach, "dac_reset", 1, 0);
	printf("Calling Python script to calibrate QDR...\n");
	system ("python ./python_embed/cal_roach_qdr.py");
	printf("Done\n");	
	roach_write_int(m_roach, "tx_rst", 0, 0);	/* perform only once per fpg upload */
	roach_write_int(m_roach, "tx_rst", 1, 0);
	roach_write_int(m_roach, "tx_rst", 0, 0);
	//printf("Initializing UDP socket... ");	
	//m_roach->udp_sock = init_socket(m_roach);
	//printf("Done\n");	
}

int main(void)
{	
	roach_state_t roach2;  
	printf("Initializing UDP socket... ");	
	roach2.udp_sock = init_socket(&roach2);
	printf("Done\n");	
	roach2.address = "192.168.40.89";
	int fd;
	fd = net_connect(roach2.address, 0, NETC_VERBOSE_ERRORS | NETC_VERBOSE_STATS);
	roach2.rpc_conn = create_katcl(fd);
	char option;
    	bool isRunning = true;
    	while(isRunning==true)
    	{
        	puts(
		"\n\n"
		"\n\n[0]\tUpload fpg to ROACH"
		"\n\n[1]\tInitialize"
		"\n\n[2]\tWrite test comb"
		"\n\n[3]\tVNA Sweep"
             	"\n\n[4]\tFind KID freqs"
             	"\n\n[5]\tTarget Sweep"
             	"\n\n[6]\tStream Packets"
             	"\n\n[x]\tExit"
		"\n\n");
		option = getchar();
        	switch(option)
        {
        	case '0':
			roach_upload_fpg(&roach2, test_fpg);
			//isRunning = false;
			break;
		case '1':
			init_roach(&roach2);
			break;
		case '2':
			roach_freq_comb(&roach2);
			roach_write_tones(&roach2, roach2.freq_comb, roach2.freqlen);	
			//roach_write_tones(&roach2, test_freq, 1);
			//roach_read_QDR(&roach2, m_qdr_I, m_qdr_Q);
			//printf("%u\t%u\n", m_qdr_I[0], m_qdr_Q[0]);
			//save_packed_luts(&roach2);
			break;
		case '3':
			roach2.vna_path = "./iqstream/r2/vna";
			printf("Starting VNA sweep\n");
			roach_sweep(&roach2, 750e6, roach2.vna_path, "test", 1, 0, 1);
			break;
		case '4':
			roach2.num_kids = 273;
			get_kid_freqs(&roach2, "/home/muchacho/olimpo_readout/sweeps/vna/0804_2");
			break;
		case '5': 
			roach2.targ_path = "./iqstream/r2/targ";
			printf("Starting target sweep\n");
			roach_sweep(&roach2, 750e6, roach2.targ_path, "test", 0, 1, 1);
			break;
            	case '6':
		{	
			int which_chan;
			printf("\nChannel Number = ? ");
			scanf("%d", &which_chan);
			stream_packets(&roach2, 100000, which_chan);
			break;
		}
		case 'x':
                     	isRunning = false;
                     	return 0;
            	default :
                     	//User enters wrong input
                     	//TO DO CODE
                     	break;
        }
    	}
    	return 0;
}
