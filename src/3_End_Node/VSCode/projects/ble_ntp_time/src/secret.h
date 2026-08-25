#ifndef SECRET_H_
#define SECRET_H_

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Structure to define the exact boundaries of Summer Time (WEST) for a given year.
 */
struct west_boundary {
	uint32_t start_epoch;  /* Exact Epoch when Summer Time begins (GMT+1) */
	uint32_t end_epoch;    /* Exact Epoch when Summer Time ends (GMT+0) */
};

/* ==================================================================== */
/* AUTOMATED 10-YEAR PORTUGAL (EUROPE/LISBON) LOOKUP TABLE               */
/* ==================================================================== */
static const struct west_boundary west_table[] = {
	{ 1774832400, 1792938000 }, /* 2026: 2026-03-29 to 2026-10-25 */
	{ 1806368400, 1824982800 }, /* 2027: 2027-03-28 to 2027-10-31 */
	{ 1837386000, 1855914000 }, /* 2028: 2028-03-26 to 2028-10-29 */
	{ 1869440400, 1888054800 }, /* 2029: 2029-03-25 to 2029-10-28 */
	{ 1900976400, 1919590800 }, /* 2030: 2030-03-31 to 2030-10-27 */
	{ 1932512400, 1951126800 }, /* 2031: 2031-03-30 to 2031-10-26 */
	{ 1963443600, 1982672400 }, /* 2032: 2032-03-28 to 2032-10-31 */
	{ 1995584400, 2014198800 }, /* 2033: 2033-03-27 to 2033-10-30 */
	{ 2026515600, 2045744400 }, /* 2034: 2034-03-26 to 2034-10-29 */
	{ 2058570000, 2077184400 }, /* 2035: 2035-03-25 to 2035-10-28 */
};

/* Safely calculate the array row index bounds automatically */
#define WEST_TABLE_SIZE (sizeof(west_table) / sizeof(west_table[0]))

#endif /* SECRET_H_ */
