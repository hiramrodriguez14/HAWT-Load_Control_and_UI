#ifndef DUMP_LOAD_H_
#define DUMP_LOAD_H_

typedef enum {
    DUMP_LOAD_DISABLED,
    DUMP_LOAD_ENABLED
} dump_load_state_t;

void dump_load_init(void);
void dump_load_enable(void);
void dump_load_disable(void);
dump_load_state_t dump_load_get_state(void);
const char *dump_load_state_to_string(dump_load_state_t state);

#endif /* DUMP_LOAD_H_ */
