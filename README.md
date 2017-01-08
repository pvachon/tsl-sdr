#The Standard Library

This is the repository for the Software Defined Radio/DSP-focused version of
The Standard Library, or TSL.

#Building

You will need Python installed. Invoke `./waf configure` to configure the build,
and `./waf build` to build the tools.

The applications will end up in `build/release/bin`

#Tools

There are a number of applications in this repo.

##MultiFM

MultiFM is the heart of the TSL distribution for SDR. This allows a user to take
a broadband chunk of the spectrum and subdivide it into multiple arbitrarily
spaced narrowband channels. MultiFM is optimized for environments with relatively
limited compute resources. Almost every step of the signal processing chain is
is fixed point. Some functions make use of NEON intrinsics when built for ARM.

Sample configuration files can be found in `etc`. This includes a sample filter
for FLEX pager channels, and sample configuration for multifm. The configurations
should be largely self-explanatory.

Invoke MultiFM with `multifm [configuration file 1] ... [configuration file n]`.

##Resampler

Resampler is a simple rational resampler. Given an appropriate filter and set of
command line filters, Resampler will allow a user to perform a rational resampling
of samples from an input fifo, and write the resampled samples to the output fifo.

See `resampler -h` for information on the specific parameters to use.

A script, in `scripts/design_interpolation_filter.py` will generate a filter that
can be used with Resampler as an input. This script requires GNURadio be installed.

#License

The TSL, MultiFM and Resampler (as well as libfilter, etc.) are provided under
two licenses - the GPLv2 and the MIT/X license. You can pick whichever license
works best for you.

#Author

Most of this code was written by Phil Vachon (phil@security-embdedded.com).
