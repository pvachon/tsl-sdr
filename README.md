# Software Defined Radio Tools

This is the repository for tools that use the TSL that are meant for software
defined radio and signal processing.

## Getting the TSL

Please have a look at [the TSL repository](https://github.com/pvachon/tsl)

# Building

You will need Python installed. Invoke `./waf configure` to configure the build,
and `./waf build` to build the tools.

The applications will end up in `build/release/bin`. Just invoke them in the usual way.

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
