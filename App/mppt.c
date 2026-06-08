#include "mppt.h"

static mppt_state_t state;

void mppt_init(void)
{
    state = MPPT_STATE_IDLE;
}

void mppt_enable(void)
{
    state = MPPT_STATE_IDLE;
}

void mppt_disable(void)
{
    state = MPPT_STATE_IDLE;
}

void mppt_update(float vin, float iin, float vout, float iout)
{
    (void)vin;
    (void)iin;
    (void)vout;
    (void)iout;

    state = MPPT_STATE_IDLE;
}

mppt_state_t mppt_get_state(void)
{
    return state;
}

float mppt_get_target_duty(void)
{
    return 0.0f;
}

const char *mppt_state_to_string(mppt_state_t mppt_state)
{
    switch (mppt_state) {
        case MPPT_STATE_IDLE:
            return "IDLE";

        case MPPT_STATE_SEARCH:
            return "SEARCH";

        case MPPT_STATE_TRACK:
            return "TRACK";

        case MPPT_STATE_LIMITED:
            return "LIMITED";

        case MPPT_STATE_FAULT:
            return "FAULT";

        default:
            return "UNKNOWN";
    }
}
