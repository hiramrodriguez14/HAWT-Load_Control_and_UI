#ifndef LOAD_RELAY_H_
#define LOAD_RELAY_H_

typedef enum {
    LOAD_RELAY_DISABLED,
    LOAD_RELAY_ENABLED
} load_relay_state_t;

void load_relay_init(void);
void load_relay_enable(void);
void load_relay_disable(void);
void load_relay_update(float vbat);
load_relay_state_t load_relay_get_state(void);
const char *load_relay_state_to_string(load_relay_state_t state);

#endif /* LOAD_RELAY_H_ */
