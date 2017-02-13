# The Standard Library

This is the repository for the Software Defined Radio/DSP-focused version of
The Standard Library, or TSL.

# Building

You will need Python installed. Invoke `./waf configure` to configure the build,
and `./waf build` to build the tools.

The applications will end up in `build/release/bin`

## Dependencies

The TSL currently depends on:
 * ConcurrencyKit (https://github.com/concurrencykit/ck)
 * libjansson
 * librtlsdr

As well, some scripts will use GNUradio (since reimplementing Parks-McClelland
would be just a waste of time!).

# Tools

There are a number of applications in this repo. They all serve different
purposes.

## MultiFM

MultiFM is the heart of the TSL distribution for SDR. This allows a user to take
a broadband chunk of the spectrum and subdivide it into multiple arbitrarily
spaced narrowband channels. MultiFM is optimized for environments with relatively
limited compute resources. Almost every step of the signal processing chain is
is fixed point. Some functions make use of NEON intrinsics when built for ARM.

Sample configuration files can be found in `etc`. This includes a sample filter
for FLEX pager channels, and sample configuration for multifm. The configurations
should be largely self-explanatory.

Invoke MultiFM with `multifm [configuration file 1] ... [configuration file n]`.

## Depager

Depager is designed to take a PCM stream of FLEX data, and decode the pages
contained therein. Depager will yield the pages as JSON objects, which then can
be further postprocessed as you see fit.

Depager has a built-in rational resampler. As long as you can efficiently get your
input data to 16kHz samples through a rational resampling, you're good to go.

You can think of the rational resampler as allowing a user to resample input
samples of frequency `F_in` per the following equation:

```
F_out = F_in * Interpolating factor
        ---------------------------
             Decimation Factor
```

Depager takes several arguments:
 * `-I [int]` The interpolating (or multiplicative) factor for the resampler
 * `-D [int]` The decimating (or dividing) factor for the resampler.
 * `-F [JSON]` The filter kernel - this should be designed to ensure no aliasing.
 * `-d [binary File]` Output debug file. All samples after initial processing are written here. Optional.
 * `-S [rate]` Input sample rate, from the source file, in hertz
 * `-f [frequency]` The frequency, in Hertz, of the pager channel. Required, but can be bogus.
 * `-b` Enable the DC blocker filter. You probably don't want to do this.
 * `-o [file]` Specify the output file. If this is not provided, it's assumed you want to write to stdout.
 * `-c` If specified, create the output file. Optional, ignored if `-o` isn't specified.

The final argument is the sample source. This is any UNIX file (so can be a fifo, for example).

Included in `etc/` is a file called `resampler_filter.json`. If you're messing with
FLEX, this file is appropriate for your purposes.

Think carefully -- you might be able to pipe, say, the output of MultiFM to the
input of multiple depagers. Just saying.

## Resampler

Resampler is a simple rational resampler. Given an appropriate filter and set of
command line filters, Resampler will allow a user to perform a rational resampling
of samples from an input fifo, and write the resampled samples to the output fifo.

See `resampler -h` for information on the specific parameters to use.

A script, in `scripts/design_interpolation_filter.py` will generate a filter that
can be used with Resampler as an input. This script requires GNURadio be installed.

# License

The TSL, MultiFM and Resampler (as well as libfilter, etc.) are provided under
two licenses - the GPLv2 and the MIT/X license. You can pick whichever license
works best for you.

# Author

Most of this code was written by Phil Vachon (phil@security-embdedded.com).
