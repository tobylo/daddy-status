typedef enum {
    GPIO_INTERRUPT,
    TIMER_INTERRUPT
} interrupt_evt_type;

typedef struct {
    interrupt_evt_type type;
    uint32_t gpio;
    int timer_group;
    int timer_idx;
    uint64_t timer_counter_value;
} interrupt_event_t;
