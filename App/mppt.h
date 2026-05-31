#ifndef MPPT_H_
#define MPPT_H_

typedef enum {
    MPPT_STATE_IDLE,
    MPPT_STATE_SEARCH,
    MPPT_STATE_TRACK,
    MPPT_STATE_LIMITED,
    MPPT_STATE_FAULT
} mppt_state_t;

void mppt_init(void);
void mppt_enable(void);
void mppt_disable(void);
void mppt_update(float vin, float iin, float vout, float iout);
mppt_state_t mppt_get_state(void);
float mppt_get_target_duty(void);
const char *mppt_state_to_string(mppt_state_t state);

#endif /* MPPT_H_ */
