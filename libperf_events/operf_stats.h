/**
 * @file libperf_events/operf_stats.h
 * Management of operf statistics
 *
 * @remark Copyright 2012 OProfile authors
 * @remark Read the file COPYING
 *
 * Created on: June 11, 2012
 * @author Maynard Johnson
 * (C) Copyright IBM Corp. 2012
 */

#include <string>
#include "operf_counter.h"

#ifndef OPERF_STATS_H
#define OPERF_STATS_H

extern unsigned long operf_stats[];

enum {	OPERF_SAMPLES, /**< nr. samples */
	OPERF_KERNEL, /**< nr. kernel samples */
	OPERF_PROCESS, /**< nr. userspace samples */
	OPERF_INVALID_CTX, /**< nr. samples lost due to sample address not in expected range for domain */
	OPERF_LOST_KERNEL,  /**< nr. kernel samples lost */
	OPERF_LOST_SAMPLEFILE, /**< nr samples for which sample file can't be opened */
	OPERF_LOST_NO_MAPPING, /**< nr samples lost due to no mapping */
	OPERF_NO_APP_KERNEL_SAMPLE, /**<nr. user ctx kernel samples dropped due to no app context available */
	OPERF_NO_APP_USER_SAMPLE, /**<nr. user samples dropped due to no app context available */
	OPERF_BT_LOST_NO_MAPPING, /**<nr. backtrace samples dropped due to no mapping */
	OPERF_LOST_INVALID_HYPERV_ADDR, /**<nr. hypervisor samples dropped due to address out-of-range */
	OPERF_RECORD_LOST_SAMPLE, /**<nr. samples lost reported by perf_events kernel */
	OPERF_MAX_STATS /**< end of stats */
};
#define OPERF_INDEX_OF_FIRST_LOST_STAT 3

/* Warn on lost samples if number of lost samples is greater the this fraction
 * of the total samples
*/
#define OPERF_WARN_LOST_SAMPLES_THRESHOLD   0.0001

void operf_print_stats(std::string sampledir, char * starttime, bool throttled);
void warn_if_kern_multiplexing(std::string const & session_samples_dir);
void warn_if_kern_throttling(std::string const & session_samples_dir);

class operf_stats_recorder {

public:
	static std::string create_stats_dir(std::string const & sample_dir);
	/* The checking and writing of the throttled and multiplexing
	 * stats is done by two different processes: 1) 'operf_record_pid'
	 * process, which retrieves sample data from the kernel and
	 * writes it either to a pipe or (with --lazy-conversion) to a file;
	 * and 2) the 'sample data conversion' process, which reads the data
	 * written by operf_record_pid.  The operf_record_pid process checks
	 * for multiplexing and writes the multiplexed event names. The
	 * sample data conversion process processes the data read from the pipe
	 * or file and checks for throttling of the event by the kernel and
	 * prints the throttled data.  The check_for_multiplexing() function
	 * writes its data to a temporary directory.  When the sample data
	 * conversion process has setup the directory "current" to store all
	 * of the data, the multiplexed data is moved from its temporary
	 * location to the "current" directory.
	 */
	static void mv_multiplexed_data_dir(std::string const & session_dir,
					    std::string const & sample_dir);
	static void check_for_multiplexing(std::vector< std::vector<operf_counter> > const & perfCounters,
					   int num_cpus, int system_wide,
					   int evt);

	static void write_throttled_event_files(std::vector< operf_event_t> const & events,
						std::string const & current_sampledir);
};
#endif /* OPERF_STATS_H */
