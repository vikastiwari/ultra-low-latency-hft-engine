#ifndef RTE_BYTEORDER_H
#define RTE_BYTEORDER_H

#include <endian.h>

#define rte_be_to_cpu_32(x) be32toh(x)
#define rte_be_to_cpu_64(x) be64toh(x)

#endif
