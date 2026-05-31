#ifndef BATTERY_CHARGER_H_
#define BATTERY_CHARGER_H_

typedef enum {
    BATTERY_CHARGER_BULK,
    BATTERY_CHARGER_ABSORPTION,
    BATTERY_CHARGER_FLOAT,
    BATTERY_CHARGER_FAULT
} battery_charger_state_t;

void battery_charger_init(void);
void battery_charger_update(float vbat, float ibat);
battery_charger_state_t battery_charger_get_state(void);
const char *battery_charger_state_to_string(battery_charger_state_t state);

#endif /* BATTERY_CHARGER_H_ */
