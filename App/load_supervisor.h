#ifndef LOAD_SUPERVISOR_H_
#define LOAD_SUPERVISOR_H_

void load_supervisor_init(void);
void load_supervisor_update(float battery_power_w, float rectifier_power_w);
void load_supervisor_force_mppt_disabled(void);
float load_supervisor_get_power_margin_w(void);
unsigned char load_supervisor_is_mppt_allowed(void);

#endif /* LOAD_SUPERVISOR_H_ */
