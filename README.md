# Linux Industrial I/O (IIO) stress testing simulator

Tool for stress testing and benchmarking [Linux IIO](https://www.kernel.org/doc/html/latest/driver-api/iio/index.html)
subsystem kernel and userspace.

Simulates a buffered IIO device which sends a sample of 4 unsigned 32-bit intergers (16 bytes)
in bursts of `period_count` at an interval of `period_ms` (subject to OS tick timer resolution).

The simulated sample is `(0xdeadbeef, counter, counter, 0x1badface)`, where the counter
increments by one with each sample.

The provided `iio-stream.py` script will consume the sample stream,
checking for consistency and dropped samples.
It also prints aggregate statistics periodically.

Required Linux kernel configuration parameters (`y` or `m`)

```
CONFIG_IIO=m
CONFIG_IIO_BUFFER=y
```

```sh
# Build for host kernel
make

# Load
modprobe industrialio
modprobe kfifo_buf
insmod ./iio_sim.ko period_ms=100 period_count=10

# Test
python3 ./iio-stream.py iio:device0 voltage0 --depth 128
```

A test on QEMU/KVM/amd64 and Linux 4.19 guest with `period_count==200000` and `period_ms` set for 60Hz effective,
and `--depth 65536` reached ~150 MB/s before saturating CPU, and triggering soft lock warnings in the kernel log.

```
  239.64 samples/syscall, 41722.95 syscalls/s, 9998285.18 samples/s, 152.562 MB/s
  239.38 samples/syscall, 41684.63 syscalls/s, 9978574.06 samples/s, 152.261 MB/s
  237.51 samples/syscall, 41845.80 syscalls/s, 9938730.68 samples/s, 151.653 MB/s
Jump start 711792505 711801545
  243.25 samples/syscall, 37978.93 syscalls/s, 9238244.57 samples/s, 140.964 MB/s
```

A test on a zynqmq (zcu102) and Linux 4.9 (xilinx-2017.1) with `period_count==100000` and `period_ms` set for 60Hz effective,
and `--depth 65536` reached ~67 MB/s before saturating CPU, and triggering soft lock warnings in the kernel log.

```
  2229.08 samples/syscall, 1970.34 syscalls/s, 4392046.40 samples/s, 67.017 MB/s
  2229.29 samples/syscall, 1970.60 syscalls/s, 4393037.84 samples/s, 67.032 MB/s
  2228.59 samples/syscall, 1970.52 syscalls/s, 4391487.90 samples/s, 67.009 MB/s
```
