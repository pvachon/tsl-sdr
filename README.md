# Software Defined Radio Tools

This is the repository for tools that use the TSL that are meant for software
defined radio and signal processing.

## Getting the TSL

Please have a look at [the TSL repository](https://github.com/pvachon/tsl). The easiest
way to do this is to likely build the debian package. You do not need this package for
running executables, only for building (everything is statically linked).

# Building

Please install CMake. Most repos have a package. As well, you'll likely want
at least one of the following RF interface libraries:
 * `librtlsdr`for RTL-SDR (known as `librtlsdr0` in Debian variants)
 * `libuhd` for USRP (known as `libuhd003` in Debian variants)
 * `libdespairspy` for Airspy (find it [here](https://github.com/pvachon/despairspy)).

Simply create a directory inside the project, e.g. `build`, change to that directory
and instruct CMake to do its thing. Simply:
```
git clone https://github.com/pvachon/tsl-sdr
mkdir tsl-sdr/build
cd tsl-sdr/build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
sudo make install
```

You an optionally skip the `make` steps and invoke `cpack`. This will generate a Debian
package for your convenience.

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
