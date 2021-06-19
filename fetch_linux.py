#!/usr/bin/env python3

import sys
import re
import gzip
import logging
from html.parser import HTMLParser
from urllib.request import urlopen

_log = logging.getLogger(sys.argv[0])

base_url = 'https://cdn.kernel.org/pub/linux/kernel/'

def getargs():
    from argparse import ArgumentParser, FileType
    P = ArgumentParser()
    P.add_argument('version',
                   help='Linux kernel version (eg. 5.9.9) or "latest"')
    P.add_argument('output', type=FileType('w'),
                   help='Output .tar.xz file, or - for stdout')
    P.add_argument('-n', '--dry-run', action='store_true')
    return P

class DirParser(HTMLParser):
    def __init__(self):
        super(DirParser, self).__init__()
        self._tag, self._attrs = None, {}
        self.series = {}
        self.kernels = {}

    def handle_starttag(self, tag, attrs):
        self._tag, self._attrs = tag, dict(attrs)

    def handle_endtag(self, tag):
        self._tag, self._attrs = None, {}

    def handle_data(self, data):
        if self._tag!='a' or 'href' not in self._attrs:
            return

        link = self._attrs['href']
        M=re.match(r'v(\d+)\.x/', link)
        if M:
            self.series[int(M.group(1))] = link
            return

        M=re.match(r'linux-(\d+(?:\.\d+)+)\.tar\.xz', link)
        if M:
            self.kernels[tuple(int(p) for p in M.group(1).split('.'))] = link
            return

    @classmethod
    def fetch(klass, url):
        with urlopen(url) as F:
            blob = F.read()
            if F.info().get('Content-Encoding') == 'gzip':
                blob = gzip.decompress(blob)

        P = klass()
        P.feed(blob.decode())
        return P

def main(args):
    logging.basicConfig(level=logging.INFO)

    if args.version!='latest':
        series = args.version.split('.',1)[0]
        url = '%sv%s.x/linux-%s.tar.xz'%(base_url, series, args.version)

    else:
        _log.info('Fetch series list')
        P = DirParser.fetch(base_url)

        series = list(P.series.items())
        series.sort()
        series.reverse()

        for ser, link in series:
            _log.info('Fetch series %s list', ser)

            P = DirParser.fetch(base_url + link)

            kernels = list(P.kernels.items())
            kernels.sort()
            kernels.reverse()
            if len(kernels)==0:
                continue

            url = base_url + link + kernels[0][1]
            break

        else:
            _log.info("No series has a kernel.  Probably HTML parsing error")
            sys.exit(1)

    _log.info('GET: %s', url)

    if args.dry_run:
        _log.info('Skip download')
        return

    nbytes = 0
    with urlopen(url) as F:
        while True:
            blob = F.read(2**20)
            if not blob:
                break
            nbytes += len(blob)
            args.output.buffer.write(blob)
    _log.info('Wrote %u bytes', nbytes)
    args.output.buffer.flush()

if __name__=='__main__':
    main(getargs().parse_args())
