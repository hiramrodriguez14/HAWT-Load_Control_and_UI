#include "load_supervisor.h"

#include "dump_load.h"
#include "mppt.h"

#define LOAD_SUPERVISOR_POWER_MARGIN_W      0.50f
#define LOAD_SUPERVISOR_RESTART_MARGIN_W    0.75f

static float last_power_margin_w;
static unsigned char mppt_allowed;

void load_supervisor_init(void)
{
    last_power_margin_w = 0.0f;
    mppt_allowed = 0U;
    mppt_disable();
    dump_load_enable();
}

void load_supervisor_update(float battery_power_w, float rectifier_power_w)
{
    float battery_demand_w = (battery_power_w < 0.0f) ? -battery_power_w : battery_power_w;
    float rectifier_available_w = (rectifier_power_w < 0.0f) ? 0.0f : rectifier_power_w;

    last_power_margin_w = rectifier_available_w - battery_demand_w;

    if (mppt_allowed) {
        if (last_power_margin_w <= LOAD_SUPERVISOR_POWER_MARGIN_W) {
            mppt_allowed = 0U;
            mppt_disable();
            dump_load_enable();
        }
    } else {
        if (last_power_margin_w >= LOAD_SUPERVISOR_RESTART_MARGIN_W) {
            mppt_allowed = 1U;
            dump_load_disable();
            mppt_enable();
        }
    }
}

void load_supervisor_force_mppt_disabled(void)
{
    mppt_allowed = 0U;
    mppt_disable();
    dump_load_enable();
}

float load_supervisor_get_power_margin_w(void)
{
    return last_power_margin_w;
}

unsigned char load_supervisor_is_mppt_allowed(void)
{
    return mppt_allowed;
}
