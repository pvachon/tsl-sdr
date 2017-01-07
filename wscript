import os
import subprocess
import sys

from waflib import Logs
from waflib.Errors import WafError

#For help, type this command:
#
#   ./waf -h

#To build, type this command (parallelism is automatic, -jX is optional):
#
#   ./waf configure clean build

########## Configuration ##########

#waf internal
top = '.'
out = 'build'

versionCommand = 'git describe --abbrev=16 --dirty --always --tags'.split(' ')
cpuArchCommand = '/bin/uname -m'.split(' ')

# FIXME: get rid of this crap
cpuArch = subprocess.check_output(cpuArchCommand).strip()

def _checkSupportedArch(cpuArchId):
	if cpuArchId != 'x86_64' and cpuArchId != 'armv7l':
		raise Exception('Unsupported CPU architecture: {}'.format(cpuArchId))

########## Commands ##########

def _loadTools(context):
	_checkSupportedArch(cpuArch)

	#Force gcc (instead of generic C) since Phil tortures the compiler
	context.load('gcc')

	# Nasm/Yasm
	if cpuArch == 'x86_64':
		context.load('nasm')

	#This does neat stuff, like header dependencies
	context.load('c_preproc')

def options(opt):
	#argparse style options (yay!)
	opt.add_option('-d', '--debug', action='store_true',
		help="Debug mode (turns on debug defines, assertions, etc.) - a superset of -D")
	opt.add_option('-D', '--tsl-debug', action='store_true',
		help="Defines _TSL_DEBUG (even for release builds!)")

	_loadTools(opt)

def configure(conf):
	_loadTools(conf)

	conf.check(lib='rtlsdr', uselib='RTLSDR', define_name='HAVE_RTLSDR')

	#Setup build flags
	conf.env.CFLAGS += [
		'-g3',
		'-gdwarf-4',

		'-Wall',
		'-Wundef',
		'-Wstrict-prototypes',
		'-Wmissing-prototypes',
		'-Wno-trigraphs',
		# '-Wconversion', -- this is a massive undertaking to turn back on, lazy.

		'-fno-strict-aliasing',
		'-fno-common',

		'-Werror-implicit-function-declaration',
		'-Wno-format-security',

		'-fno-delete-null-pointer-checks',

		'-Wuninitialized',
		'-Wmissing-include-dirs',

		'-Wshadow',
		# '-Wcast-qual',

		'-Wframe-larger-than=2047',

		'-std=c11',

		'-g',

		'-rdynamic',
	]

	conf.env.DEFINES += [
		'_ATS_IN_TREE',
		'_GNU_SOURCE',
		'SYS_CACHE_LINE_LENGTH=64',
	]

	conf.env.INCLUDES += [
		'.'
	]

	# FIXME: this is not sane for a lot of environments
	conf.env.LIBPATH += [
		'/usr/lib/x86_64-linux-gnu',
		'/usr/local/lib',
	]

	conf.env.LIB += [
		'pthread',
		'rt',
		'jansson',
		'dl',
		'm',
	]

	conf.env.LDFLAGS += [
		'-rdynamic'
	]

	# Assembler flags (so that the linker will understand the objects)
	if cpuArch == 'x86_64':
		conf.env.ASFLAGS += ['-f', 'elf64']

	conf.msg('Building for:', cpuArch)

	tuning = []
	if cpuArch == 'armv7l':
		# ARM can be a bit quirky. Make sure we specify the right CPU architecture,
		# and ensure we have the right register set defined.
		# Also, for now, we shall asume we have NEON available, all the time.
		conf.env.DEFINES += [ '_USE_ARM_NEON' ]
		tuning = [
			'-mcpu=cortex-a7',
			'-mfpu=crypto-neon-fp-armv8',
			'-mfloat-abi=hard',
		]
	else:
		tuning = [
			'-march=native',
			'-mtune=native',
		]

	#Setup the environment: debug or release
	if conf.options.debug:
		stars = '*' * 20
		conf.msg('Build environment', '%s DEBUG %s' % (stars, stars), color='RED')
		conf.env.ENVIRONMENT = 'debug'
		conf.env.CFLAGS += [
			'-O0',
		] + tuning
	else:

		stars = '$' * 20
		conf.msg('Build environment', '%s RELEASE %s' % (stars, stars), color='BOLD')
		conf.env.ENVIRONMENT = 'release'
		conf.env.CFLAGS += [
			'-O2',
		] + tuning

	if conf.options.debug or conf.options.tsl_debug:
		conf.msg('Defining', '_TSL_DEBUG', color='CYAN')
		conf.env.DEFINES += [
			'_TSL_DEBUG',
		]

	if conf.options.debug:
		conf.env.DEFINES += [
			'_AWESOME_PANIC_MESSAGE',
		]
	else:
		conf.env.DEFINES += [
			'NDEBUG', #Make assert() compile out in production
		]

	# TODO: per-architecture, define the size of a pointer
	conf.env.DEFINES += [
		'TSL_POINTER_SIZE=8',
	]

	# App Specific: Defines where to look for configs by default
	conf.env.DEFINES += [
		'CONFIG_DIRECTORY_DEFAULT=\"/etc/tsl\"',
	]

	# conf.check_cfg(package = 'fftw3f', atleast_version = '3.0.0', args = '--cflags --libs')

def _preBuild(bld):
	Logs.info('Pre %s...' % bld.cmd)

	#This gets executed after build() sets up the build, but before it actually happens

def _postBuild(bld):
	Logs.info('Post %s...' % bld.cmd)

	environment = bld.env.ENVIRONMENT
	if environment == 'debug':
		stars = '*' * 20
		Logs.error('Finished building: %s DEBUG [%s] %s' % (stars, bld.env.VERSION, stars))
	else:
		stars = '$' * 20
		Logs.info('Finished building: %s RELEASE [%s] %s' % (stars, bld.env.VERSION, stars))

	#Symlink latest
	os.system('cd "%s" && ln -fns "%s" latest' % (bld.out_dir, environment))

def build(bld):
	#This method gets called when the dependency tree needs to be computed, NOT
	#just on 'build' (e.g. it also is called for 'clean').

	#Check the version immediately before the build happens
	import subprocess
	try:
		version = subprocess.check_output(versionCommand).strip()
		Logs.info('Building version %s' % version)
	except subprocess.CalledProcessError:
		version = 'NotInGit'
		Logs.warn('Building out of version control, so no version string is available')
	bld.env.VERSION = version

	#Pre and post build hooks (these are only called for 'build')
	bld.add_pre_fun(_preBuild)
	bld.add_post_fun(_postBuild)

	bld.read_shlib('rtlsdr', paths=bld.env.LIBPATH)

	#Construct paths for the targets
	basePath = bld.env.ENVIRONMENT
	binPath = os.path.join(basePath, 'bin')
	libPath = os.path.join(basePath, 'lib')
	testPath = os.path.join(basePath, 'test')

	#MultiFM
	bld.program(
		source	= bld.path.ant_glob('multifm/*.c'),
		use		= ['app', 'config', 'tsl', 'filter', 'rtlsdr'],
		target	= os.path.join(binPath, 'multifm'),
		name	= 'multifm',
	)

	# Resampler
	bld.program(
		source	= bld.path.ant_glob('resampler/*.c'),
		use		= ['app', 'config', 'tsl', 'filter'],
		target	= os.path.join(binPath, 'resampler'),
		name	= 'resampler',
	)

	#app
	appExcl = []
	if cpuArch == 'armv7l':
		# We want to ignore the CPU features check, since this relies on x86 CPUID
		appExcl.append('app/cpufeatures.*')
	bld.stlib(
		source   = bld.path.ant_glob('app/*.c', excl=appExcl),
		use      = ['config', 'tsl'],
		target   = os.path.join(libPath, 'app'),
		name     = 'app',
	)
	bld.program(
		source   = bld.path.ant_glob('app/test/*.c'),
		use      = ['app', 'test', 'tsl'],
		target   = os.path.join(testPath, 'test_app'),
		name     = 'test_app',
	)

	#config
	bld.stlib(
		source   = bld.path.ant_glob('config/*.c'),
		use      = ['tsl'],
		target   = os.path.join(libPath, 'config'),
		name     = 'config',
	)
	bld.program(
		source   = bld.path.ant_glob('config/test/*.c'),
		use      = ['config', 'test', 'tsl'],
		target   = os.path.join(testPath, 'test_config'),
		name     = 'test_config',
	)

	# Filter Library
	bld.stlib(
		source   = bld.path.ant_glob('filter/*.c'),
		use      = ['tsl'],
		target   = os.path.join(libPath, 'filter'),
		name     = 'filter',
	)
	bld.program(
		source   = bld.path.ant_glob('filter/test/*.c'),
		use      = ['config', 'test', 'tsl', 'filter'],
		target   = os.path.join(testPath, 'test_filter'),
		name     = 'test_filter',
	)


	# Pager
	bld.stlib(
		source   = bld.path.ant_glob('pager/*.c'),
		use      = ['tsl', 'config'],
		target   = os.path.join(libPath, 'pager'),
		name     = 'pager',
	)
	bld.program(
		source   = bld.path.ant_glob('pager/test/*.c'),
		use      = ['pager', 'config', 'test', 'tsl'],
		target   = os.path.join(testPath, 'test_pager'),
		name     = 'test_pager',
	)

	#test
	bld.stlib(
		source   = bld.path.ant_glob('test/*.c'),
		use      = ['app'],
		target   = os.path.join(libPath, 'test'),
		name     = 'test',
	)

	#TSL
	#Version objects are built specially first since they have a special define
	versionDefine = '_VC_VERSION=\"%s\"' % bld.env.VERSION
	bld.objects(
		defines  = [versionDefine],
		source   = 'tsl/version.c',
		target   = 'tsl_version',
		name     = 'tsl_version',
	)
	#Then the regular build happens

	# Calculate what we might want to exclude for certain architectures
	excl=['tsl/version.c', 'tsl/test/*.*']
	if cpuArch == 'armv7l':
		# We want to ignore coro - there's no ARM implementation yet
		excl.append('tsl/coro/*.*')
		excl.append('tsl/timer.c')

	tslSource = bld.path.ant_glob('tsl/**/*.[cS]', excl=excl)
	bld.stlib(
		cflags   = ['-fPIC'],
		source   = tslSource,
		use      = ['tsl_version'],
		target   = os.path.join(libPath, 'tsl'),
		name     = 'tsl',
	)

	# Exclude the coroutine tests if we're building for ARM32
	excl=[]
	if cpuArch == 'armv7l':
		excl.append('tsl/test/test_coro.c')
		excl.append('tsl/test/test_speed.c')
	bld.program(
		source   = bld.path.ant_glob('tsl/test/*.c', excl=excl),
		use      = ['tsl'],
		target   = os.path.join(testPath, 'test_tsl'),
		name     = 'tsl_test',
	)
	bld.program(
		source   = bld.path.ant_glob('tsl/version_dump/*.c'),
		use      = ['tsl'],
		target   = os.path.join(binPath, 'dump_version'),
		name     = 'dump_version',
	)

from waflib.Build import BuildContext
class TestContext(BuildContext):
        cmd = 'test'
        fun = 'test'

def test(ctx):
	Logs.info('Unit testing... TODO')

class PushContext(TestContext):
        cmd = 'push'
        fun = 'push'

def sysinstall(ctx):
	Logs.info('Installing system packages...')

	packages = [
		'build-essential',
		'pkg-config',
		'libjansson-dev',
		'libfftw3-dev',
		'librtlsdr-dev',
	]

	os.system('apt-get install %s' % ' '.join(packages))
