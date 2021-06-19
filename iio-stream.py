#!/usr/bin/env python3

import math
import time
import os
import re
import numpy

# <endian> ':' <sign> <realbits> '/' <storagebits> 'X' <repeat> '>>' <shift>
# 'le:u32/32X4>>0'
_scan_type = re.compile(r'(?P<endian>[^:]+):(?P<sign>[a-z]+)(?P<realbits>\d+)/(?P<storagebits>\d+)(?:X(?P<repeat>\d+))?(?:>>(?P<shift>\d+))?')

def getargs():
    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument('device')
    P.add_argument('chan')
    P.add_argument('-D', '--depth', type=int, default=32)
    return P

def sysget(*parts):
    with open(os.path.join('/sys/bus/iio/devices', *parts), 'r') as F:
        return F.read()

def syswrite(*parts, value=''):
    with open(os.path.join('/sys/bus/iio/devices', *parts), 'w') as F:
        F.write(value)

class IIOBuffer(object):
    def __init__(self, args):
        self.args = args

    def __enter__(self):
        fmt = sysget(self.args.device, 'scan_elements', 'in_%s_type'%self.args.chan)
        M = _scan_type.match(fmt.strip())
        print('sample format', M.groupdict())
        self.sample_size = int(math.ceil( int(M.group('storagebits'))/8 * int(M.group('repeat') or '1') ))
        print('sample_size', self.sample_size, 'bytes')

        # ensure disabled
        syswrite(self.args.device, 'buffer', 'enable', value='0')
        self._dev = open(os.path.join('/dev', self.args.device), 'rb', 0)
        syswrite(self.args.device, 'buffer', 'length', value=str(self.args.depth))
        syswrite(self.args.device, 'scan_elements', 'in_%s_en'%self.args.chan, value='1')
        syswrite(self.args.device, 'buffer', 'enable', value='1')
        return self

    def __exit__(self,A,B,C):
        syswrite(self.args.device, 'buffer', 'enable', value='0')
        self._dev.close()
        del self._dev

    def read(self):
        return self._dev.read(self.sample_size*self.args.depth)

if __name__=='__main__':
    maxsamplepersys = 0
    nsys = 0
    nsamples = 0
    nbytes = 0
    lastcnt = -1
    T0 = time.monotonic()
    with IIOBuffer(getargs().parse_args()) as B:
        while True:
            samples = B.read()
            now = time.monotonic()
            if not samples:
                print('Done')
                break

            nsys += 1
            nbytes += len(samples)
            samples = numpy.frombuffer(samples, 'u4')

            samples.shape = (samples.size//4, 4)
            assert (samples[:,0]==0xdeadbeef).all(), samples
            assert (samples[:,1]==samples[:,2]).all(), samples
            assert (samples[:,3]==0x1badface).all(), samples
            nsamples += samples.shape[0]

            if samples.shape[1] > maxsamplepersys:
                maxsamplepersys = samples.shape[1]
                print('maxsamplepersys', maxsamplepersys)

            if samples[0,1]!=lastcnt+1:
                print('Jump start', lastcnt, samples[0,1])

            if (numpy.diff(samples[:,1])!=1).any():
                print('Jump mid', samples)

            lastcnt = samples[-1,1]

            #print(samples[:,1])

            dT = now-T0
            if dT > 1.0:
                print('  %.2f samples/syscall, %.2f syscalls/s, %.2f samples/s, %.3f MB/s'%(nsamples/nsys, nsys/dT, nsamples/dT, nbytes/dT/1048576))
                T0=now
                nsys=0
                nsamples=0
                nbytes=0
