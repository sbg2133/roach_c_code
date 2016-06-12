/*
 *            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
 *                    Version 2, December 2004
 * 
 * Copyright (C) 2016 Gordon, Sam <sbgordo1@asu.edu>
 * Author: Gordon, Sam <sbgordo1@asu.edu>
 * 
 * Everyone is permitted to copy and distribute verbatim or modified
 * copies of this license document, and changing it is allowed as long
 * as the name is changed.
 * 
 *            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
 *   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION
 * 
 *  0. You just DO WHAT THE FUCK YOU WANT TO.
 */

#include <string.h>
#include <python2.7/Python.h>
#include <stdio.h>
#include <complex.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
//#include <stdlib.h>
#include <netinet/in.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include "netc.h"
//#include "roach.h"
#include <fftw3.h>
#include "katcp.h"
#include "katcl.h"
#include <string.h>
#undef I
#define M_PI 3.14159265358979323846

static double dac_samp_freq = 512.0e6;
static double fpga_samp_freq = 256.0e6;
static int fft_len = 1024;

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
    const char *address;
    uint16_t port;
    int ms_cmd_timeout;

    double dac_freq_res;
    double *freq_residuals;
    size_t lut_buffer_len;

    roach_lut_t DDS;
    roach_lut_t DAC;
    roach_uint16_lut_t LUT;

    char *vna_path;
    char *channels_path;

    struct katcl_line *rpc_conn;

} roach_state_t;

typedef struct {
    const char *firmware_file;
    uint16_t port;
    struct timeval timeout;
    int result;
    roach_state_t *roach;

} firmware_state_t;


/* Roach com functions */

int roach_read_data(roach_state_t *m_roach, uint32_t *m_dest, const char *m_register,
                           uint32_t m_offset, uint32_t m_size)
{
    int retval = send_rpc_katcl(m_roach->rpc_conn, m_roach->ms_cmd_timeout,
                   KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?read",
                   KATCP_FLAG_STRING, m_register,
                   KATCP_FLAG_ULONG, m_offset,
                   KATCP_FLAG_ULONG | KATCP_FLAG_LAST, m_size,
                   NULL);

    printf("%d\n", retval);
    if (retval < 0) {
        return -1;
    }
    if (retval > 0) {
        char *ret = arg_string_katcl(m_roach->rpc_conn, 1);
	return -1;
    }

    if (arg_count_katcl(m_roach->rpc_conn) < 2) {
        return -1;
    }

    uint32_t bytes_copied = arg_buffer_katcl(m_roach->rpc_conn, 2, m_dest, m_size);
    printf("Bytes copied: %d\n", bytes_copied);

    if (bytes_copied != m_size) {
	printf("Bytes copied not equal to size: %d\n", m_size);
	return -1;
    
    }
    return 0;
}

static void roach_buffer_ntohl(uint32_t *m_buffer, size_t m_len)
{
    for (size_t i = 0; i < m_len; i++) {
        m_buffer[i] = ntohs(m_buffer[i]);
    }
    printf("%d\n", *m_buffer);
}

int roach_read_int(roach_state_t *m_roach, const char *m_register, uint32_t m_offset)
{
	uint32_t *m_result;
	size_t buffer_len = 4; 
	roach_read_data(m_roach, m_result, "dds_shift", 0, buffer_len * sizeof(uint8_t));
	roach_buffer_ntohl(m_result, buffer_len);
	printf("%d\n", *m_result);
	return 0;
}

int roach_wordwrite(roach_state_t *m_roach, const char *m_register, uint32_t m_val, uint32_t m_offset)
{
	return send_rpc_katcl(m_roach->rpc_conn, m_roach->ms_cmd_timeout,
		KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?wordwrite",
		KATCP_FLAG_STRING, m_register,
		KATCP_FLAG_ULONG, (unsigned long)m_offset,
		KATCP_FLAG_ULONG | KATCP_FLAG_LAST, (unsigned long)m_val);
}

int roach_wordread(roach_state_t *m_roach, const char *m_register, uint32_t m_offset)
{
	return send_rpc_katcl(m_roach->rpc_conn, m_roach->ms_cmd_timeout,
		KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?wordread",
		KATCP_FLAG_STRING, m_register,
		KATCP_FLAG_ULONG | KATCP_FLAG_LAST, (unsigned long)m_offset);
}

int roach_write_data(roach_state_t *m_roach, const char *m_register, const uint32_t *m_data, int32_t m_len, uint32_t m_offset) 
{
	return send_rpc_katcl(m_roach->rpc_conn, m_roach->ms_cmd_timeout, 
		KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?write",
		KATCP_FLAG_STRING, m_register,
		KATCP_FLAG_ULONG, (unsigned long)m_offset,
		KATCP_FLAG_BUFFER | KATCP_FLAG_LAST, m_data, m_len);
}


int roach_write_int(roach_state_t *m_roach, const char *m_register, uint32_t m_data, uint32_t m_offset)
{
    	uint32_t sendval = htonl(m_data);
	return roach_write_data(m_roach, m_register, (uint32_t*)&sendval, sizeof(sendval), m_offset); 
}

int roach_write_qdr(roach_state_t *m_roach, const char *m_register, char *m_data, uint32_t m_len , uint32_t m_offset) 
{
	int retval;
	retval = send_rpc_katcl(m_roach->rpc_conn, m_roach->ms_cmd_timeout, 
		KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?write",
		KATCP_FLAG_STRING, m_register,
		KATCP_FLAG_ULONG, (unsigned long)m_offset,
		KATCP_FLAG_BUFFER | KATCP_FLAG_LAST, &m_data[1]);
	printf("%d\n", retval);
	return 0;
}

int roach_upload_fpg(roach_state_t *m_roach, const char *m_filename)
{
    firmware_state_t state = {
                              .firmware_file = m_filename,
                              //.port = (uint16_t) (drand48() * 500.0 + 5000),
                              .port = (uint16_t) 3000,
			      .timeout.tv_sec = 5,
                              .timeout.tv_usec = 0,
			      .roach = m_roach
    };

    printf("Getting permission to upload fpg on port 3000...\t");
    int retval = send_rpc_katcl(m_roach->rpc_conn, m_roach->ms_cmd_timeout,
                       KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?progremote",
                       KATCP_FLAG_ULONG | KATCP_FLAG_LAST, state.port,
                       NULL);
    
    if (retval != KATCP_RESULT_OK) {
        return -1;
	printf("Failed\n");
    
    }
    printf("Done\n");
    printf("Uploading fpg through netcat...\t");
    int status = system("nc -w 2 192.168.40.89 3000 < ./blast_0502.fpg");
    printf("Done\n");
    /*while (state.result == ROACH_UPLOAD_RESULT_WORKING) {
        usleep(1000);
    }

    if (state.result != ROACH_UPLOAD_RESULT_SUCCESS) return -1;

    return 0;*/
}

static int roach_reset_dac(roach_state_t *m_roach)
{
    roach_write_int(m_roach, "dac_reset", 1, 0);
    roach_write_int(m_roach, "dac_reset", 0, 0);
    return 0;
}

/* Computer side functions */

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
        m_freqs[i] = round(m_freqs[i] / m_roach->dac_freq_res) * m_roach->dac_freq_res;
	//printf("freq: %g\n", m_freqs[i]);
    }

    comb_fft_len = m_roach->lut_buffer_len;
    //printf("comb fft len: %zd\n", comb_fft_len);
    complex double *spec = calloc(comb_fft_len,  sizeof(complex double));
    complex double *wave = calloc(comb_fft_len,  sizeof(complex double));
    double max_val = 0.0;
   
    srand48(time(NULL));
    for (size_t i = 0; i < m_freqlen; i++) {
    	////printf("bin: %d\n", roach_fft_bin_index(m_freqs, i, comb_fft_len, m_samp_freq));
	spec[roach_fft_bin_index(m_freqs, i, comb_fft_len, m_samp_freq)] = 
					cexp(_Complex_I * drand48() * 2.0 * M_PI);
	//printf("%f\n", drand48() * 2.0 * M_PI);
	//printf("spec bin: %f, %f\n", creal(spec[roach_fft_bin_index(m_freqs, i, comb_fft_len, m_samp_freq)]), cimag(spec[roach_fft_bin_index(m_freqs, i, comb_fft_len, m_samp_freq)]));
    	}		
    	/*FILE *f1 = fopen("./dac_spec.txt", "w");
    	for (size_t i = 0; i < comb_fft_len; i++){
    		fprintf(f1,"%f, %f\n", creal(spec[i]), cimag(spec[i])); 
    	} 
     	fclose(f1);*/
       
    FILE *f2 = fopen("./dac_wave.txt", "w");
    comb_plan = fftw_plan_dft_1d(comb_fft_len, spec, wave, FFTW_BACKWARD, FFTW_ESTIMATE);
    fftw_execute(comb_plan);
    fftw_destroy_plan(comb_plan);
    for (size_t i = 0; i < comb_fft_len; i++) {
    	fprintf(f2,"%f, %f\n", creal(wave[i]), cimag(wave[i])); 
        if (cabs(wave[i]) > max_val) max_val = cabs(wave[i]);
    }
    fclose(f2);
    //printf("max wave val: %g\n", max_val);
    
    for (size_t i = 0; i < comb_fft_len; i++) {
	m_I[i] = creal(wave[i]) / max_val * ((1<<15)-1);
        m_Q[i] = cimag(wave[i]) / max_val * ((1<<15)-1);
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

	//printf("freq: %g\n", m_freqs);
    	
	comb_fft_len = m_roach->lut_buffer_len / fft_len;
    	//printf("comb fft len: %zd\n", comb_fft_len);
    	complex double *spec = calloc(comb_fft_len,  sizeof(complex double));
    	complex double *wave = calloc(comb_fft_len,  sizeof(complex double));
    	double max_val = 0.0;
    	
	spec[m_bin] = cexp(_Complex_I * 0.);	
	//printf("spec bin: %f, %f\n", creal(spec[m_bin]), cimag(spec[m_bin]));
    	
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
    		fprintf(f4,"%f, %f\n", creal(wave[i]), cimag(wave[i])); 
        	if (cabs(wave[i]) > max_val) max_val = cabs(wave[i]);
    	}
    	fclose(f4);
    	//printf("max wave val: %g\n", max_val);
    
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
    if (m_roach->DAC.len > 0 && m_roach->DAC.len != m_roach->lut_buffer_len) {
	free(m_roach->DAC.I);
        free(m_roach->DAC.Q);
        m_roach->DAC.len = 0;
    }
    if (m_roach->DAC.len == 0) {
        m_roach->DAC.I = calloc(m_roach->lut_buffer_len, sizeof(double));
        m_roach->DAC.Q = calloc(m_roach->lut_buffer_len, sizeof(double));
        m_roach->DAC.len = m_roach->lut_buffer_len;
    }
    roach_dac_comb(m_roach, m_freqs, m_freqlen,
                    dac_samp_freq, m_roach->DAC.I, m_roach->DAC.Q);
}

static void roach_select_bins(roach_state_t *m_roach, double *m_freqs, size_t m_freqlen)
{
    int bins[fft_len];
    double bin_freqs[fft_len];
    int last_bin = -1;
    int ch = 0;

    m_roach->freq_residuals = malloc(m_freqlen * sizeof(size_t));
    for (size_t i = 0; i < m_freqlen; i++) {
	bins[i] = roach_fft_bin_index(m_freqs, i, fft_len, dac_samp_freq);
	//printf("select bins, bin: %d\n", bins[i]); fflush(stdout);
	bin_freqs[i] = bins[i] * dac_samp_freq / fft_len;
	//printf("%g\n", m_roach->freq_residuals[i]);
	//printf("bin freq: %d\n", bins[i]);
	
	m_roach->freq_residuals[i] = round((m_freqs[i] - bin_freqs[i]) / m_roach->dac_freq_res) * m_roach->dac_freq_res;
	//printf("res freq: %g\n", m_roach->freq_residuals[i]);
    	
    }

    for (int i = 0; i < m_freqlen; i++) {
        if (bins[i] == last_bin) continue;
        roach_write_int(m_roach, "bins", bins[i], 0);
        roach_write_int(m_roach, "load_bins", 2 * ch + 1, 0);
        roach_write_int(m_roach, "load_bins", 0, 0);
        ch++;
    }
    /**
     * Fill any remaining of the 1024 channelizer addresses with '0'
     */
    
    for (int i = ch; i < fft_len; i++) {
        roach_write_int(m_roach, "bins", 0, 0);
        roach_write_int(m_roach, "load_bins", 2 * ch + 1, 0);
        roach_write_int(m_roach, "load_bins", 0, 0);
        ch++;
    }

}
void roach_define_DDS_LUT(roach_state_t *m_roach, double *m_freqs, size_t m_freqlen)
{
    roach_select_bins(m_roach, m_freqs, m_freqlen);
    if (m_roach->DDS.len > 0 && m_roach->DDS.len != m_roach->lut_buffer_len) {
        free(m_roach->DDS.I);
        free(m_roach->DDS.Q);
        m_roach->DDS.len = 0;
    }
    
    if (m_roach->DDS.len == 0) {
        m_roach->DDS.I = calloc(m_roach->lut_buffer_len, sizeof(double));
        m_roach->DDS.Q = calloc(m_roach->lut_buffer_len, sizeof(double));
        m_roach->DDS.len = m_roach->lut_buffer_len;
    }

    for (size_t i = 0; i < m_freqlen; i++){
	double I[2 * fft_len];
    	double Q[2 * fft_len];
    	//printf("%zd\n", i);
	int bin = roach_fft_bin_index(m_roach->freq_residuals, i, m_roach->lut_buffer_len / fft_len, fpga_samp_freq / (fft_len / 2));
	//printf("inside def dds, bin: %d\n", bin);
	roach_dds_comb(m_roach, m_roach->freq_residuals[i], m_freqlen,
                        fpga_samp_freq / (fft_len / 2), bin, I, Q);

	/*FILE *f1 = fopen("./I.txt", "w");
    	FILE *f2 = fopen("./Q.txt", "w");
	
	for (int j = 0; j < 2 * fft_len; j++){
    		fprintf(f1,"%g\n", I[j]); 
    		fprintf(f2,"%g\n", Q[j]); 
	}
	fclose(f1);
	fclose(f2);*/

	for (int j = i, k = 1; k < 2*fft_len; j += fft_len, k++){
		m_roach->DDS.I[j] = I[k];
        	m_roach->DDS.Q[j] = Q[k]; 
    	}	
    }
}

void roach_pack_LUTs(roach_state_t *m_roach, double *m_freqs, size_t m_freqlen)
{
    //roach_define_DDS_LUT(m_roach, m_freqs, m_freqlen);
    //roach_define_DAC_LUT(m_roach, m_freqs, m_freqlen);

   /* if (m_roach->LUT.len > 0 && m_roach->LUT.len != m_roach->lut_buffer_len)  {
    	printf("Attempting to free luts\t");
	free(m_roach->LUT.I);
        free(m_roach->LUT.Q);
    }*/

   	roach_init_LUT(m_roach, 2 * m_roach->lut_buffer_len);
    	
	for (size_t i = 0; i < m_roach->lut_buffer_len; i += 2) {
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

void save_luts(roach_state_t *m_roach, size_t m_len)
{
	FILE *f1 = fopen("./DACI.txt", "w");
    	FILE *f2 = fopen("./DACQ.txt", "w");
   	FILE *f3 = fopen("./DDSI.txt", "w");
   	FILE *f4 = fopen("./DDSQ.txt", "w");
    	
	for (size_t i = 0; i < m_len; i++){
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

void save_packed_luts(roach_state_t *m_roach, size_t m_len)
{
	FILE *f1 = fopen("./QDRI.txt", "w");
    	FILE *f2 = fopen("./QDRQ.txt", "w");
    	
	for (size_t i = 0; i < m_len; i++){
    		fprintf(f1,"%d\n", ntohs(m_roach->LUT.I[i])); 
    		fprintf(f2,"%d\n", ntohs(m_roach->LUT.Q[i])); 
    	}   
    	fclose(f1);
    	fclose(f2);
}

void roach_write_QDR(roach_state_t *m_roach, double *m_freqs, size_t m_freqlen)
{
    roach_pack_LUTs(m_roach, m_freqs, m_freqlen);
    roach_reset_dac(m_roach);
    roach_write_int(m_roach, "start_dac", 0, 0);
    
    char *Istr = calloc(m_roach->LUT.len, sizeof(char));    
    int index = 0;
    for (size_t i = 0; i < m_roach->LUT.len; i++){
    	index += sprintf(&Istr[index], "%u", m_roach->LUT.I[i]);
    }
    roach_write_qdr(m_roach, "qdr0_memory", Istr, m_roach->LUT.len * sizeof(uint16_t), 0);
    roach_write_qdr(m_roach, "qdr1_memory", Istr, m_roach->LUT.len * sizeof(uint16_t), 0);
    
    free(m_roach->LUT.I);
    //free(m_roach->LUT.Q);
    roach_write_int(m_roach, "start_dac", 1, 0);
    roach_write_int(m_roach, "sync_accum_reset", 0, 0);
    roach_write_int(m_roach, "sync_accum_reset", 1, 0);
    free(Istr);
}

int main(void)
{	
	char server[] = "192.168.40.89";
	int fd;
	fd = net_connect(server, 0, NETC_VERBOSE_ERRORS | NETC_VERBOSE_STATS);

	roach_state_t roachacho;  
	roachacho.rpc_conn = create_katcl(fd);

	roachacho.address = "192.168.40.89";
	roachacho.port = 7147;
	roachacho.ms_cmd_timeout = 10;
	roachacho.lut_buffer_len = 1 << 21;
	const char fpg[] = "/home/muchacho/roach_c_code/blast_0502.fpg";
	
	//roach_wordwrite(&roachacho, "dds_shift", 304, 0);
	//roach_wordwrite(&roachacho, "fft_shift", 255, 0);
	//roach_wordwrite(&roachacho, "sync_accum_len", (1 << 20) - 1, 0);
	
	//roach_read_int(&roachacho, "dds_shift", 0);
	
	roach_write_int(&roachacho, "dds_shift", 304, 0);
	roach_write_int(&roachacho, "fft_shift", 255, 0);
	roach_write_int(&roachacho, "sync_accum_reset", 1048575 ,0);

	double freqs[1] = {50.0125e6};  
	size_t freqlen = 1;
	roachacho.dac_freq_res = fpga_samp_freq / roachacho.lut_buffer_len ;
	//Variable used for reading the user input
    	char option;
    	//Variable used for controlling the while loop
    	bool isRunning = true;

    	while(isRunning==true)
    	{
        	//system("clear");        //For UNIX-based OSes

        	fflush(stdin);
        	fflush(stdout);
		//Outputs the options to console
        	puts(
		"\n\n"
		"\n\n[0]\tUpload fpg to ROACH"
		"\n\n[1]\tCalibrate QDR w/ Python"
		"\n\n[2]\tCreate DAC and DDS LUTs"
		"\n\n[3]\tSave LUTs to disk"
		"\n\n[4]\tPack LUTs"
             	"\n\n[5]\tSave packed LUTs to disk"
             	"\n\n[6]\tUpload LUTs to QDR RAM"
             	"\n\n[x]\tExit"
		"\n\n");
        	//Reads the user's option
        	option = getchar();
        	//Selects the course of action specified by the option
        	switch(option)
        {
        	case '0':
			roach_upload_fpg(&roachacho, fpg);
			break;
			
		case '1':
			roach_write_int(&roachacho, "dac_reset", 1, 0);
			printf("Calling Python script to calibrate QDR...\n");
			/*Py_Initialize();
			FILE* qdr_cal = fopen("/home/muchacho/roach_c_code/python_embed/cal_roach_qdr.py", "r");
			printf("Opened file\n");	
			PyRun_SimpleFile(qdr_cal, "/home/muchacho/roach_c_code/python_embed/cal_roach_qdr.py");
			printf("Ran file\n");	
			Py_Finalize();*/
			system ("python /home/muchacho/roach_c_code/python_embed/cal_roach_qdr.py");
			printf("Done\n");	
			printf("Setting Valon...\n");	
			system ("python /home/muchacho/roach_c_code/python_embed/set_valon.py");
			printf("Done\n");	
			getchar();
			break;
		case '2':
			printf("Allocating memory for LUTs...\n"); fflush(stdout);
			roach_init_DACDDS_LUTs(&roachacho, roachacho.lut_buffer_len);
			printf("Defining DAC LUT...\t");fflush(stdout);
			roach_define_DAC_LUT(&roachacho, freqs, freqlen);
			printf("Done\n"); fflush(stdout);
			printf("Defining DDS LUT...\t"); fflush(stdout);
			roach_define_DDS_LUT(&roachacho, freqs, freqlen);
			printf("Done\n");
			getchar();
			break;
		case '3':
			save_luts(&roachacho, roachacho.lut_buffer_len);
			printf("LUTs written to disk\n");
			getchar();	
			break;
            	case '4':
			printf("String packing LUTs...\t");
			roach_pack_LUTs(&roachacho, freqs, freqlen);	
                	printf("Done\n");
			getchar();
			break;
            	case '5':
			save_packed_luts(&roachacho, 2 * roachacho.lut_buffer_len);
			roach_reset_dac(&roachacho);
			printf("QDR LUTs written to disk\n");
                     	getchar();
			break;
            	case '6':
			printf("Uploading LUTs to QDR RAM...\t");
			roach_write_QDR(&roachacho, freqs, freqlen);
			printf("Done...\n");
			getchar();
                     	break;
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
