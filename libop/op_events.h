/**
 * @file op_events.h
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 * 
 * @author John Levon <moz@compsoc.man.ac.uk>
 * @author Philippe Elie <phil_el@wanadoo.fr>
 */

#ifndef OP_EVENTS_H
#define OP_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "op_interface.h"
 
/** Describe an event. */
struct op_event {
	u32 counter_mask;	/**< bitmask of allowed counter  */
	u16 cpu_mask;		/**< bitmask of allowed cpu_type */
	u8 val;			/**< event number */
	/* FIXME, is u8 really sufficient ? */
	u8 unit;		/**< which unit mask if any allowed */
	char const * name;	/**< the event name */
	int min_count;		/**< minimum counter value allowed */
};

/** Describe an unit mask type. Events can optionnaly use a filter called
 * the unit mask. the mask type can be a bitmask or a discrete value */
enum unit_mask_type {
	utm_mandatory,		/**< useless but required by the hardware */
	utm_exclusive,		/**< only one of the values is allowed */
	utm_bitmask		/**< bitmask */
};

/** Describe an unit mask. */
struct op_unit_mask {
	u32 num;		/**< number of possible unit masks */
	enum unit_mask_type unit_type_mask;
	u8 default_mask;	/**< only the gui use it */
	/* FIXME, is u8 really sufficient ? */
	u8 um[7];		/**< up to seven allowed unit masks */
};

/* op_events.c */
/**
 * @param ctr_type event value
 * @param cpu_type cpu type
 *
 * The function returns > 0 if the event is found 0 otherwise
 */
int op_min_count(u8 ctr_type, op_cpu cpu_type);

/**
 * sanity check event values
 * @param ctr counter number
 * @param ctr_type event value for counter 0
 * @param ctr_um unit mask for counter 0
 * @param cpu_type processor type
 *
 * Check that the counter event and unit mask values are allowed.
 *
 * Don't fail if ctr_type == 0. (FIXME why, this seems insane)
 *
 * The function returns bitmask of failure cause 0 otherwise
 *
 * \sa op_cpu, OP_EVENTS_OK
 */
int op_check_events(int ctr, u8 ctr_type, u8 ctr_um, op_cpu cpu_type);

/**
 * get the number of counter available for this cpu
 * @param cpu_type the cpu type identifier
 *
 * The function return the number of counter available for this
 * cpu type. return (u32)-1 if the cpu type is nopt recognized
 */
u32 op_get_cpu_nr_counters(op_cpu cpu_type);

/**
 * sanity check unit mask value
 * @param allow allowed unit mask array
 * @param um unit mask value to check
 *
 * Verify that a unit mask value is within the allowed array.
 *
 * The function returns:
 * -1  if the value is not allowed,
 * 0   if the value is allowed and represent multiple units,
 * > 0 otherwise.
 *
 * if the return value is > 0 caller can access to the description of
 * the unit_mask through op_unit_descs
 * \sa op_unit_descs
 */
int op_check_unit_mask(struct op_unit_mask * allow, u8 um);

/** a special constant meaning this event is available for all counters */
#define CTR_ALL		(~0u)

#ifdef __cplusplus
}
#endif

#endif /* OP_EVENTS_H */
