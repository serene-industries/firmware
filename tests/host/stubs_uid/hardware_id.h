#pragma once
#include <stdint.h>
typedef struct hardware_id_t { uint32_t data[3]; } hardware_id_t;
hardware_id_t get_hardware_id(void);
