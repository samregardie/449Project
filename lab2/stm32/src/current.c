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

// Sensor is 185 mV/A, wired straight to the ADC pin (no divider)
#define SENSOR_MV_PER_A 185

// Readings averaged per call, to cut the sensor and PWM noise
#define SAMPLES 16

// Sum of SAMPLES readings at 0 A, per channel (mV)
static int32_t zero_sum_mv[3];

// Read one channel in mV. Returns 0, or a negative error from the ADC driver.
static int read_mv(size_t i, int32_t *mv)
{
    uint32_t buf = 0;

    struct adc_sequence sequence = {
        .buffer = &buf,
        /* buffer size in bytes, not number of samples */
        .buffer_size = sizeof(buf),
    };

    (void)adc_sequence_init_dt(&adc_channels[i], &sequence);

    int err = adc_read_dt(&adc_channels[i], &sequence);

    if (err < 0)
    {
        return err;
    }

    *mv = (int32_t)buf;
    return adc_raw_to_millivolts_dt(&adc_channels[i], mv);
}

// Sum of SAMPLES readings of one channel (mV). Returns 0 or a negative error.
static int read_sum_mv(size_t i, int32_t *sum)
{
    *sum = 0;

    for (int k = 0; k < SAMPLES; k++)
    {
        int32_t mv;
        int err = read_mv(i, &mv);

        if (err < 0)
        {
            return err;
        }
        *sum += mv;
    }

    return 0;
}

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

        // Assumes no current is flowing at power-up
        err = read_sum_mv(i, &zero_sum_mv[i]);
        if (err < 0)
        {
            printk("Could not read channel #%d (%d)\n", i, err);
            return 1;
        }
    }

    printk("ADC successfully setup\n");
    return 0;
}

// Return the current passing through the given sensor, rounded to the
// nearest mA.
// Note that current can be negative based on direction.
// Returns 0 if the read fails; no printk, since this runs every 20 ms.
int get_current(enum current_sensor sensor)
{
    int32_t sum_mv;

    if (read_sum_mv(sensor, &sum_mv) < 0)
    {
        return 0;
    }

    // mV -> mA, then divide out the SAMPLES sum
    return (sum_mv - zero_sum_mv[sensor]) * 1000 /
           (SENSOR_MV_PER_A * SAMPLES);
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