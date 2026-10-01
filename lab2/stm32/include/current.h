
enum current_sensor
{
    MOTOR_LEFT = 0,
    MOTOR_RIGHT = 1,
    SERVO = 2
};

int current_sense_init(void);

int print_all_currents(void);

// Return the current passing through the given sensor, rounded to the
// nearest mA.
// Note that current can be negative based on direction.
int get_current(enum current_sensor sensor);