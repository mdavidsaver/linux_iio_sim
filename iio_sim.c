// SPDX-License-Identifier: GPL-2.0-only
/* IIO streaming simulator
 */
/* (C) 2021 Michael Davidsaver <mdavidsaver@gmail.com>
 */

//#define DEBUG

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>

#include <linux/timer.h>

MODULE_AUTHOR("Michael Davidsaver <mdavidsaver@gmail.com>");
MODULE_DESCRIPTION("IIO streaming simulator");
MODULE_LICENSE("GPL");

#define sim_dbg(psim, fmt, ...)                                                \
    dev_dbg(&psim->idev->dev, "%s() " fmt, __func__, ##__VA_ARGS__)

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
#define iio_device_alloc(parent, size) iio_device_alloc(size)
#endif

static unsigned period_ms = 100u;
module_param(period_ms, uint, 0644);

static unsigned period_count = 1u;
module_param(period_count, uint, 0644);

struct iio_sim_device {
    struct iio_dev *idev;
    struct iio_buffer *ibuf;

    struct timer_list simtick;

    struct mutex lock;

    // simulation state
    u32 counter;
};

#define SIM_SCAN 0

static void iio_sim_tick(struct timer_list *simtick)
{
    struct iio_sim_device *sdev =
        container_of(simtick, struct iio_sim_device, simtick);
    u32 frame[6]; // 4x u32 + 1x u64 (timestamp)
    int64_t ts = iio_get_time_ns(sdev->idev);
    unsigned per = period_ms;
    unsigned cnt = period_count;

    frame[0] = 0xdeadbeef;
    frame[3] = 0x1badface;

    if (per < 1)
        per = 1u;
    if (cnt < 1)
        cnt = 1u;

    mod_timer(simtick, jiffies + msecs_to_jiffies(per));

    sim_dbg(sdev, "tick active_scan_mask=%p masklength=%u\n",
        sdev->idev->active_scan_mask, sdev->idev->masklength);

    if (!sdev->idev->active_scan_mask ||
        bitmap_empty(sdev->idev->active_scan_mask,
             sdev->idev->masklength)) {
        // scan not enabled

    } else if (sizeof(frame) == sdev->idev->scan_bytes + sizeof(ts)) {
        unsigned i;

        for (i = 0; i < cnt; i++) {
            mutex_lock(&sdev->lock);
            frame[1] = frame[2] = sdev->counter++;
            mutex_unlock(&sdev->lock);

            iio_push_to_buffers(sdev->idev, frame);
        }

    } else {
        dev_warn(&sdev->idev->dev, "scan_bytes=%d sizeof(frame)=%zu\n",
             sdev->idev->scan_bytes, sizeof(frame));
    }
}

static int iio_sim_read_raw(struct iio_dev *idev,
                struct iio_chan_spec const *chan, int max_len,
                int *vals, int *val_len, long mask)
{
    int ret = -EINVAL;
    struct iio_sim_device *sdev = iio_priv(idev);

    sim_dbg(sdev, "idx=%d max_len=%d mask=%ld\n", chan->scan_index, max_len,
        mask);

    mutex_lock(&sdev->lock);
    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        if (max_len >= 4) {
            vals[0] = 12345678;
            vals[1] = vals[2] = sdev->counter;
            vals[3] = 87654321;
            *val_len = 4;
            ret = IIO_VAL_INT;
        }
        break;
    default:
        break;
    }
    mutex_unlock(&sdev->lock);
    sim_dbg(sdev, "-> %d\n", ret);
    return ret;
}

static int iio_sim_update_scan_mode(struct iio_dev *idev,
                    const unsigned long *scan_mask)
{
    struct iio_sim_device *sdev = iio_priv(idev);
    sim_dbg(sdev, "scan_mask=0x%lx\n", *scan_mask);
    return 0;
}

static const struct iio_info iio_sim_ops = {
    .read_raw_multi = iio_sim_read_raw,
    .update_scan_mode = iio_sim_update_scan_mode,
};

static int iio_sim_setup_postenable(struct iio_dev *idev)
{
    struct iio_sim_device *sdev = iio_priv(idev);
    sim_dbg(sdev, "\n");
    mod_timer(&sdev->simtick, jiffies); // now
    return 0;
}

static int iio_sim_setup_predisable(struct iio_dev *idev)
{
    struct iio_sim_device *sdev = iio_priv(idev);
    sim_dbg(sdev, "\n");
    del_timer_sync(&sdev->simtick);
    return 0;
}

static const struct iio_buffer_setup_ops iio_sim_setup = {
    .postenable = iio_sim_setup_postenable,
    .predisable = iio_sim_setup_predisable,
};

static const struct iio_chan_spec iio_sim_spec[] = { {
    .type = IIO_VOLTAGE,
    .indexed = 1,
    .channel = 0,
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    .scan_index = SIM_SCAN,
    .scan_type = {
         // 4x 32 bit unsigned = 16 bytes per scan
         .endianness = IIO_CPU,
         .sign = 'u',
         .realbits = 32u,
         .storagebits = 32u,
         .repeat = 4u,
     },
} };

static struct iio_sim_device *iio_sim_create(void)
{
    long ret = -EINVAL;
    struct iio_dev *idev;
    struct iio_sim_device *sdev;

    idev = iio_device_alloc(NULL, sizeof(*sdev));
    if (!idev) {
        ret = -ENOMEM;
        goto done;
    }

    sdev = iio_priv(idev);
    sdev->idev = idev;
    mutex_init(&sdev->lock);

    sdev->ibuf = iio_kfifo_allocate();
    if (!sdev->ibuf) {
        ret = -ENOMEM;
        goto free_idev;
    }
    iio_device_attach_buffer(idev, sdev->ibuf);
    sdev->idev->setup_ops = &iio_sim_setup;

    idev->name = "sim";
    idev->channels = iio_sim_spec;
    idev->num_channels = ARRAY_SIZE(iio_sim_spec);
    idev->info = &iio_sim_ops;
    idev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_SOFTWARE;

    ret = iio_device_register(idev);
    if (ret < 0)
        goto free_kfifo;

    timer_setup(&sdev->simtick, &iio_sim_tick, 0);

    sim_dbg(sdev, "mask_len=%u\n", idev->masklength);

    return sdev;
    //iio_device_unregister(idev);
free_kfifo:
    iio_kfifo_free(sdev->ibuf);
free_idev:
    iio_device_free(idev);
done:
    return ERR_PTR(ret);
}

static void iio_sim_destroy(struct iio_sim_device *sdev)
{
    sim_dbg(sdev, "done\n");
    del_timer_sync(&sdev->simtick);
    iio_device_unregister(sdev->idev);
    iio_kfifo_free(sdev->ibuf);
    iio_device_free(sdev->idev);
}

static struct iio_sim_device *iio_sim_inst;

static __init int iio_sim_init(void)
{
    iio_sim_inst = iio_sim_create();
    return PTR_ERR_OR_ZERO(iio_sim_inst);
}
module_init(iio_sim_init);

static __exit void iio_sim_fini(void)
{
    if (iio_sim_inst) {
        iio_sim_destroy(iio_sim_inst);
        iio_sim_inst = NULL;
    }
}
module_exit(iio_sim_fini);
