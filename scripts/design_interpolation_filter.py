#!/usr/bin/env python
from gnuradio import filter

import json
import sys

def design_filter(interpolation, decimation, fractional_bw):
    """
    Given the interpolation rate, decimation rate and a fractional bandwidth,
    design a set of taps.
    Args:
        interpolation: interpolation factor (integer > 0)
        decimation: decimation factor (integer > 0)
        fractional_bw: fractional bandwidth in (0, 0.5)  0.4 works well. (float)
    Returns:
        : sequence of numbers
    """

    if fractional_bw >= 0.5 or fractional_bw <= 0:
        raise ValueError('Invalid fractional bandwidth, must be in (0, 0.5)')

    if decimation < 1 or interpolation < 1:
        raise ValueError('Invalid interpolation or decimation rate. Must be a non-zero positive integer.')

    beta = 7.0
    halfband = 0.5
    rate = float(interpolation)/float(decimation)
    if(rate >= 1.0):
        trans_width = halfband - fractional_bw
        mid_transition_band = halfband - trans_width/2.0
    else:
        trans_width = rate*(halfband - fractional_bw)
        mid_transition_band = rate*halfband - trans_width/2.0

    taps = filter.firdes.low_pass(interpolation,                     # gain
                                  interpolation,                     # Fs
                                  mid_transition_band,               # trans mid point
                                  trans_width,                       # transition width
                                  filter.firdes.WIN_KAISER,
                                  beta)                              # beta

    return taps

def main(argv):
    if len(argv) < 3:
        print('Usage: {} [interpolation] [decimation] [fractional bandwidth]'.format(argv[0]))
        print('  Design a filter for use with a rational resampler')
        sys.exit(-1)

    interpolation = int(argv[1])
    decimation = int(argv[2])
    fractional_bw = float(argv[3])

    print(json.dumps({'rationalResampler': {'interpolate': interpolation, 'decimate': decimation, 'fractionalBw': fractional_bw, 'lpfCoeffs': design_filter(interpolation, decimation, fractional_bw)}}))

if __name__ == '__main__':
    main(sys.argv)
