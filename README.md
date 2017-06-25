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
When multiple configuration files are specified, the files will be combined,
one-by-one, into a single large JSON object. If an earlier file in the command
line contains a value whose key conflicts with a later file, the value in the
later file will be taken.

### MultiFM Configuration

This section describes the configuration keys for MultiFM. These parameters are all mandatory.

 * `device` (object): A device configuration stanza. Specifies how samples will get to MultiFM.
   * `type` (string): The device type. This will be either `rtlsdr` or `airspy`.
   * `deviceIndex` (integer): The index of the RTL-SDR device to use. If you have only one device,
   this will be 0.
   * `dbGainLNA` (float): The front-end LNA gain for an RTL-SDR (almost all tuners)
   * `dbGainIF` (float): The front-end LNA gain for an RTL-SDR with the Elonics E4000 tuner
   * `ppmCorrection` (integer): The PPM correction coefficient to be applied to the oscillator in the
	   RTL-SDR.
   * `iqDumpFile` (integer): A file to dump the raw, unprocessed samples to, before processing them.
     Use this for debugging only.
   * `sdrTestMode` (boolean): Specify whether or not the SDR should be put in counter test mode. Not
		 useful unless you know exactly what this does.
 * `sampleRateHz` (integer): The sample rate, in hertz. This is the "broadband" bandwidth of the SDR.
 * `centerFreqHz` (integer): The center frequency to tune to. This controls the tuner front end.
 * `nrSampBufs` (integer): A tunable number of sample buffers. Usually this needs to be no more than 16.
 * `decimationFactor` (integer): The integer factor the sample rate is decimated by. For example, if
   `sampleRateHz` is 1000000 (1MHz), and `decimationFactor` is 40, the output sample rate is 25000 Hz (25kHz).
 * `channels` (array): An array of channel definitions. Each channel is a JSON object. There is one
   channel object per narrowband channel.
   * `outFifo` (string): The path to the output FIFO. This FIFO must already exist (i.e. via `mkfifo(1)`).
   * `chanCenterFreq` (integer): The center frequency for that given channel. The center frequency must fall
     between [`centerFreqHz` - `sampleRateHz`/2, `centerFreqHz` + `sampleRateHz`/2], and the total
     bandwidth of the channel (i.e. `sampleRateHz`/`decimationFactor`) must fall in the sampling
     bandwidth as well.
   * `signalDebugFile` (string): Path to an output file where the non-demodulated FM stream is written to.
     Optional, probably only want this for debugging. The output file is signed 16-bit I/Q samples

In addition to the basic configuration parameters, a low-pass filter must be specified, to be used
during MultiFM's processing. The filter, also JSON, requires the following parameters:

 * `lpfTaps` (array of floats): The taps for the filter. Must be a real-valued (i.e. baseband) array.

Finally, several optional configuration parameters can be specified. These are mostly for debugging
purposes.

 * `ppmCorrection` (integer): A correction factor, in parts per million. Use if your RTL-SDR has a
   deterministic offset.
 * `iqDumpFile` (string): A file that the I/Q samples, straight from the RTL-SDR, will be dumped to. This
   is in the same structure/format as `rtl_sdr` outputs (unsigned 8 bits per sample)
 * `sdrTestMode` (boolean): Force the RTL-SDR to output counting values 0-255 in the stream. This is
   useful only for debugging RTL-SDR issues in the software.
 * `enableDCBlocker` (boolean): Enables a DC block filter on each output stream. You typically don't
   want this. The filter is a simple differentiator + leaky integrator, with noise steering.
 * `dcBlockerPole` (float): The value for the leaky integrator's sole pole.

These can be spread across mutiple JSON files and combined on the command line for MultiFM.

### MultiFM Output Format

All samples generated by MultiFM on the output side are 16-bit signed integers, in native endianess.
These samples are PCM, as you might typically get from a sound card or similar.

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

The final argument is the sample source. This is any UNIX file (so can be a fifo, for example). The input
samples are 16-bit signed integers of the sample rate specified in the command line.

Included in `etc/` is a file called `resampler_filter.json`. If you're messing with
FLEX, this file is appropriate for your purposes, if you're feeding it a 25kHz stream.

Think carefully -- you might be able to pipe, say, the output of MultiFM to the
input of multiple depagers. Just saying.

### Depager Filter File

Depager expects a filter specified using the following parameters:

 * `decimate` (integer): The decimation factor. This is used for reference purposes only.
 * `interpolate` (integer): The interpolation factor. For reference purposes only.
 * `fractionalBw` (float): The fractional bandwidth. For reference purposes only.
 * `lpfCoeffs` (array of floats): The coefficients for the low-pass filter for resampling.

This JSON file is loaded and used to parameterize the built-in rational resampler.

### Limitations

 * Depager probably doesn't support non-roman character sets correctly
 * Depager probably has bugs with some types of long CAPCODES
 * Depager doesn't have the ability (yet) to bypass the polyphase rational resampler
 * Depager doesn't provide a machine-readable way to get error metrics, yet.

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

# License

The TSL, MultiFM and Resampler (as well as libfilter, etc.) are provided under
two licenses - the GPLv2 and the MIT/X license. You can pick whichever license
works best for you.

# Author

Most of this code was written by Phil Vachon (phil@security-embdedded.com).
