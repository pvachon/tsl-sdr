# The Standard Library

This is the repository for the Software Defined Radio/DSP-focused version of
The Standard Library, or TSL.

# Building

You will need Python installed. Invoke `./waf configure` to configure the build,
and `./waf build` to build the tools.

The applications will end up in `build/release/bin`. Just invoke them in the usual way.

## Dependencies

The TSL currently depends on:
 * Linux
 * ConcurrencyKit (https://github.com/concurrencykit/ck)
 * libjansson (most distributions have a package)
 * librtlsdr (most distributions have a package) - for RTL-SDR support - optional
 * libdespairspy (https://github.com/pvachon/despairspy) - for Airspy support - optional
 * libuhd (https://files.ettus.com/manual/index.html) - for USRP support - optional

Of course, you need at least one data source, so if you don't have at least one of the above driver libraries
you'll be limited in how you can use MultiFM.

As well, some scripts will use GNUradio (since reimplementing Parks-McClelland
would be just a waste of time!).

If you're deploying MultiFM or Depager to an embedded device (such as a Raspberry Pi), you do not need to have GNUradio or similar present.

### On Debian/Raspbian/Ubuntu
To perform the minimal build:

1. Install Build Dependencies from `apt`:
   ```
   sudo apt-get install build-essential git librtlsdr-dev libjansson-dev
   ```

2. Build ConcurrencyKit:
   ```
   git clone https://github.com/concurrencykit/ck.git
   cd ck
   ./configure && make && sudo make install
   ```

3. Check out tsl-sdr:
   ```
   git clone https://github.com/pvachon/tsl-sdr.git
   cd tsl-sdr
   ./waf configure && ./waf build
   ```

This will leave you with `multifm` and `depager` in `build/release/bin`.

# Tools

There are a number of applications in this repo. They all serve different
purposes.

## MultiFM

MultiFM is the heart of the TSL distribution for SDR. This allows a user to take
a broadband chunk of the spectrum and subdivide it into multiple arbitrarily
spaced narrowband channels. MultiFM is optimized for environments with relatively
limited compute resources. Almost every step of the signal processing chain is
is fixed point. Some functions make use of NEON intrinsics when built for ARM.

## Depager

Depager is designed to take a PCM stream of demodulated but undecoded FLEX or POCSAG
data, and decode the pages contained therein. Depager will yield the pages as JSON 
objects, which then can be further postprocessed as you see fit.

Depager supports FLEX (1600, 3200, 6400 baud) and POCSAG (512, 1200, 2400 baud)
reception.

## Resampler

Resampler is a simple rational resampler. Given an appropriate filter and set of
command line filters, Resampler will allow a user to perform a rational resampling
of samples from an input fifo, and write the resampled samples to the output fifo.

See `resampler -h` for information on the specific parameters to use.

A script, in `scripts/design_interpolation_filter.py` will generate a filter that
can be used with Resampler as an input. This script requires GNURadio be installed.

Resampler requires real-valued 16-bit samples on the input side. The output will
be real-valued samples.

This was really built as a test case, but it might be useful for some cases.

# Getting Help

Be sure to check the [project wiki](https://github.com/pvachon/tsl-sdr/wiki) for
use cases, documentation and other details.

If you think you've found a bug (hey, it happens), open a [Github issue](https://github.com/pvachon/tsl-sdr/issues) for the project.

# License

The TSL, MultiFM and Resampler (as well as libfilter, etc.) are provided under
two licenses - the GPLv2 and the MIT/X license. You can pick whichever license
works best for you.

# Author

Most of this code was written by Phil Vachon (phil@security-embedded.com).
