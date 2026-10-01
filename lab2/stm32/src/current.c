#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/printk.h>

#include "current.h"

// Macro definitions copied from https://github.com/zephyrproject-rtos/zephyr/blob/main/samples/drivers/adc/adc_dt/src/main.c
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
    !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA_FOR_INPUTS(node_id, prop, idx)           \
    COND_CODE_1(DT_PHA_HAS_CELL_AT_IDX(node_id, prop, idx, input), \
                (ADC_DT_SPEC_GET_BY_IDX(node_id, idx), ), ())

/* Data of ADC io-channels specified in devicetree. */
static const struct adc_dt_spec adc_channels[] = {
    DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
                         DT_SPEC_AND_COMMA_FOR_INPUTS)};

// Storing the initial reading of the adc with 0 current
static int initial_readings[3];

int current_sense_init(void)
{
    int err;

    /* Configure channels individually prior to sampling. */
    for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++)
    {
        if (!adc_is_ready_dt(&adc_channels[i]))
        {
            printk("ADC controller device %s not ready\n", adc_channels[i].dev->name);
            return 1;
        }

        err = adc_channel_setup_dt(&adc_channels[i]);
        if (err < 0)
        {
            printk("Could not setup channel #%d (%d)\n", i, err);
            return 1;
        }

        uint32_t buf = 0;

        struct adc_sequence sequence = {
            .buffer = &buf,
            /* buffer size in bytes, not number of samples */
            .buffer_size = sizeof(buf),
        };
        int err;

        (void)adc_sequence_init_dt(&adc_channels[i], &sequence);

        err = adc_read_dt(&adc_channels[i], &sequence);

        int32_t val_mv = (int32_t)buf;

        err = adc_raw_to_millivolts_dt(&adc_channels[i],
                                       &val_mv);

        initial_readings[i] = val_mv;
    }

    printk("ADC successfully setup\n");
    return 0;
}

// Return the current passing through the given sensor, rounded to the
// nearest mA.
// Note that current can be negative based on direction.
int get_current(enum current_sensor sensor)
{
    uint32_t buf = 0;

    struct adc_sequence sequence = {
        .buffer = &buf,
        /* buffer size in bytes, not number of samples */
        .buffer_size = sizeof(buf),
    };
    int err;

    (void)adc_sequence_init_dt(&adc_channels[sensor], &sequence);

    err = adc_read_dt(&adc_channels[sensor], &sequence);

    int32_t val_mv = (int32_t)buf;

    err = adc_raw_to_millivolts_dt(&adc_channels[sensor],
                                   &val_mv);

    if (err < 0)
    {
        printk("Could not read (%d)\n", err);
        // TODO: enter error state
    }

    val_mv -= initial_readings[sensor];

    // Sensor has 185 mV/A sensitivity
    // Multiply mV by 0.005405 ~= 173 >> 5
    return (val_mv * 173) >> 5;
}

int print_all_currents(void)
{

    printk("Printing ADC Values: \n");

    uint32_t buf = 0;
    int err;

    struct adc_sequence sequence = {
        .buffer = &buf,
        /* buffer size in bytes, not number of samples */
        .buffer_size = sizeof(buf),
    };

    for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++)
    {

        int32_t val_mv;

        /*
         * Clear buffer before reading.  This ensures the upper 16-bits will be zero
         * when the adc uses a 16-bit buffer size.
         */
        buf = 0;

        printk("- %s, channel %d: ",
               adc_channels[i].dev->name,
               adc_channels[i].channel_id);

        (void)adc_sequence_init_dt(&adc_channels[i], &sequence);

        err = adc_read_dt(&adc_channels[i], &sequence);
        if (err < 0)
        {
            printk("Could not read (%d)\n", err);
            continue;
        }

        /*
         * If using differential mode, the 16 bit value
         * in the ADC sample buffer should be a signed 2's
         * complement value.
         */
        if (adc_channels[i].channel_cfg.differential)
        {
            val_mv = (int32_t)((int16_t)buf);
        }
        else
        {
            val_mv = (int32_t)buf;
        }

        err = adc_raw_to_millivolts_dt(&adc_channels[i],
                                       &val_mv);

        printk("%" PRId32, val_mv);
    }

    return 0;
}