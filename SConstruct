#!python
import os

opus_path = ARGUMENTS.get("opus_path", None)
opus_library_path = ARGUMENTS.get("opus_library", None)
use_builtin_opus = not opus_path or not opus_library_path

libsamplerate_path = ARGUMENTS.get("libsamplerate_path", None)
libsamplerate_library_path = ARGUMENTS.get("libsamplerate_library", None)
use_builtin_libsamplerate = not libsamplerate_path or not libsamplerate_library_path

#######################################
#### godot-cpp/SConstruct #############
#######################################
import os
import sys
import subprocess

if sys.version_info < (3,):
    def decode_utf8(x):
        return x
else:
    import codecs
    def decode_utf8(x):
        return codecs.utf_8_decode(x)[0]

# Workaround for MinGW. See:
# http://www.scons.org/wiki/LongCmdLinesOnWin32
if (os.name=="nt"):
    import subprocess

    def mySubProcess(cmdline,env):
        #print "SPAWNED : " + cmdline
        startupinfo = subprocess.STARTUPINFO()
        startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        proc = subprocess.Popen(cmdline, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, startupinfo=startupinfo, shell = False, env = env)
        data, err = proc.communicate()
        rv = proc.wait()
        if rv:
            print("=====")
            print(err.decode("utf-8"))
            print("=====")
        return rv

    def mySpawn(sh, escape, cmd, args, env):

        newargs = ' '.join(args[1:])
        cmdline = cmd + " " + newargs

        rv=0
        if len(cmdline) > 32000 and cmd.endswith("ar") :
            cmdline = cmd + " " + args[1] + " " + args[2] + " "
            for i in range(3,len(args)) :
                rv = mySubProcess( cmdline + args[i], env )
                if rv :
                    break
        else:
            rv = mySubProcess( cmdline, env )

        return rv

def add_sources(sources, dir, extension):
    for f in os.listdir(dir):
        if f.endswith('.' + extension):
            sources.append(dir + '/' + f)


# Try to detect the host platform automatically.
# This is used if no `platform` argument is passed
if sys.platform.startswith('linux'):
    host_platform = 'linux'
elif sys.platform == 'darwin':
    host_platform = 'osx'
elif sys.platform == 'win32' or sys.platform == 'msys':
    host_platform = 'windows'
else:
    raise ValueError(
        'Could not detect platform automatically, please specify with '
        'platform=<platform>'
    )

opts = Variables([], ARGUMENTS)
opts.Add(EnumVariable(
    'platform',
    'Target platform',
    host_platform,
    allowed_values=('linux', 'osx', 'windows', 'android', 'ios'),
    ignorecase=2
))
opts.Add(EnumVariable(
    'bits',
    'Target platform bits',
    'default',
    ('default', '32', '64')
))
opts.Add(BoolVariable(
    'use_llvm',
    'Use the LLVM compiler - only effective when targeting Linux',
    False
))
opts.Add(BoolVariable(
    'use_mingw',
    'Use the MinGW compiler instead of MSVC - only effective on Windows',
    False
))
# Must be the same setting as used for cpp_bindings
opts.Add(EnumVariable(
    'target',
    'Compilation target',
    'debug',
    allowed_values=('debug', 'release'),
    ignorecase=2
))
opts.Add(PathVariable(
    'headers_dir',
    'Path to the directory containing Godot headers',
    'godot-cpp/godot_headers',
    PathVariable.PathIsDir
))
opts.Add(PathVariable(
    'custom_api_file',
    'Path to a custom JSON API file',
    None,
    PathVariable.PathIsFile
))
opts.Add(BoolVariable(
    'generate_bindings',
    'Generate GDNative API bindings',
    False
))
opts.Add(
    'osxcross_sdk',
    'OSXCross SDK version',
    'darwin14'
)
opts.Add(EnumVariable(
    'android_arch',
    'Target Android architecture',
    'armv7',
    ['armv7','arm64v8','x86','x86_64']
))
opts.Add(EnumVariable(
    'ios_arch',
    'Target iOS architecture',
    'arm64',
    ['armv7', 'arm64', 'x86_64']
))
opts.Add(
    'IPHONEPATH',
    'Path to iPhone toolchain',
    '/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain',
)
opts.Add(
    'android_api_level',
    'Target Android API level',
    '18' if ARGUMENTS.get("android_arch", 'armv7') in ['armv7', 'x86'] else '21'
)
opts.Add(
    'ANDROID_NDK_ROOT',
    'Path to your Android NDK installation. By default, uses ANDROID_NDK_ROOT from your defined environment variables.',
    os.environ.get("ANDROID_NDK_ROOT", None)
)

env = Environment(ENV = os.environ)
opts.Update(env)
Help(opts.GenerateHelpText(env))

env["install_name_tool"] = "install_name_tool"

is64 = sys.maxsize > 2**32
if (
    env['TARGET_ARCH'] == 'amd64' or
    env['TARGET_ARCH'] == 'emt64' or
    env['TARGET_ARCH'] == 'x86_64' or
    env['TARGET_ARCH'] == 'arm64-v8a'
):
    is64 = True

if env['bits'] == 'default':
    env['bits'] = '64' if is64 else '32'

# This makes sure to keep the session environment variables on Windows.
# This way, you can run SCons in a Visual Studio 2017 prompt and it will find
# all the required tools
if host_platform == 'windows' and env['platform'] != 'android':
    if env['bits'] == '64':
        env = Environment(TARGET_ARCH='amd64')
    elif env['bits'] == '32':
        env = Environment(TARGET_ARCH='x86')

    opts.Update(env)

if env['platform'] == 'linux':
    if env['use_llvm']:
        env['CC'] = 'clang'
        env['CXX'] = 'clang++'

    env.Append(CCFLAGS=['-fPIC', '-g', '-Wwrite-strings'])
    env.Append(CFLAGS=['-std=c11'])
    env.Append(CXXFLAGS=['-std=c++14'])
    env.Append(LINKFLAGS=["-Wl,-R,'$$ORIGIN'"])

    if env['target'] == 'debug':
        env.Append(CCFLAGS=['-Og'])
    elif env['target'] == 'release':
        env.Append(CCFLAGS=['-O3'])

    if env['bits'] == '64':
        env.Append(CCFLAGS=['-m64'])
        env.Append(LINKFLAGS=['-m64'])
    elif env['bits'] == '32':
        env.Append(CCFLAGS=['-m32'])
        env.Append(LINKFLAGS=['-m32'])

elif env['platform'] == 'osx':
    # Save this in environment for use by other modules
    if not "OSXCROSS_ROOT" in os.environ:  # regular native build
        if env["arch"] == "arm64":
            print("Building for macOS 10.15+, platform arm64.")
            env.Append(CCFLAGS=["-arch", "arm64", "-mmacosx-version-min=10.15", "-target", "arm64-apple-macos10.15"])
            env.Append(LINKFLAGS=["-arch", "arm64", "-mmacosx-version-min=10.15", "-target", "arm64-apple-macos10.15"])
        else:
            print("Building for macOS 10.9+, platform x86-64.")
            env.Append(CCFLAGS=["-arch", "x86_64", "-mmacosx-version-min=10.9"])
            env.Append(LINKFLAGS=["-arch", "x86_64", "-mmacosx-version-min=10.9"])

        # Use Clang on macOS by default
        env["CXX"] = "clang"
        env["CXX"] = "clang++"

    else:  # osxcross build
        root = os.environ.get("OSXCROSS_ROOT", 0)
        basecmd = root + "/target/bin/x86_64-apple-" + env["osxcross_sdk"] + "-"

        env["install_name_tool"] = basecmd + "install_name_tool"
        env["CC"] = basecmd + "cc"
        env["CXX"] = basecmd + "c++"
        env["AR"] = basecmd + "ar"
        env["RANLIB"] = basecmd + "ranlib"
        env["AS"] = basecmd + "as"
        env.Append(CPPDEFINES=["__MACPORTS__"])  # hack to fix libvpx MM256_BROADCASTSI128_SI256 define

    if env['bits'] == '32':
        raise ValueError(
            'Only 64-bit builds are supported for the macOS target.'
        )

    env.Append(CCFLAGS=['-g'])
    env.Append(CFLAGS=['-std=c11'])
    env.Append(CXXFLAGS=['-std=c++14'])
    env.Append(LINKFLAGS=[
        '-framework',
        'Cocoa',
        '-Wl,-undefined,dynamic_lookup',
    ])

    if env['target'] == 'debug':
        env.Append(CCFLAGS=['-Og'])
    elif env['target'] == 'release':
        env.Append(CCFLAGS=['-O3'])

elif env['platform'] == 'ios':
    if env['ios_arch'] == 'x86_64':
        sdk_name = 'iphonesimulator'
        env.Append(CCFLAGS=['-mios-simulator-version-min=10.0'])
    else:
        sdk_name = 'iphoneos'
        env.Append(CCFLAGS=['-miphoneos-version-min=10.0'])

    try:
        sdk_path = decode_utf8(subprocess.check_output(['xcrun', '--sdk', sdk_name, '--show-sdk-path']).strip())
    except (subprocess.CalledProcessError, OSError):
        raise ValueError("Failed to find SDK path while running xcrun --sdk {} --show-sdk-path.".format(sdk_name))

    compiler_path = env['IPHONEPATH'] + '/usr/bin/'
    env['ENV']['PATH'] = env['IPHONEPATH'] + "/Developer/usr/bin/:" + env['ENV']['PATH']

    env['CC'] = compiler_path + 'clang'
    env['CXX'] = compiler_path + 'clang++'
    env['AR'] = compiler_path + 'ar'
    env['RANLIB'] = compiler_path + 'ranlib'

    env.Append(CCFLAGS=['-g', '-arch', env['ios_arch'], '-isysroot', sdk_path])
    env.Append(CFLAGS=['-std=c11'])
    env.Append(CXXFLAGS=['-std=c++14'])
    env.Append(LINKFLAGS=[
        '-arch',
        env['ios_arch'],
        '-framework',
        'Cocoa',
        '-Wl,-undefined,dynamic_lookup',
        '-isysroot', sdk_path,
        '-F' + sdk_path
    ])

    if env['target'] == 'debug':
        env.Append(CCFLAGS=['-Og'])
    elif env['target'] == 'release':
        env.Append(CCFLAGS=['-O3'])

elif env['platform'] == 'windows':
    if host_platform == 'windows' and not env['use_mingw']:
        # MSVC
        ############################
        ### A change is made here:
        ### NOTE: gdnative plugins use /Zi, not /Z7
        ### Also, LINKFLAGS must contain /DEBUG:FULL
        ############################
        env.Append(LINKFLAGS=['/WX', '/DEBUG:FULL'])
        if env['target'] == 'debug':
            env.Append(CCFLAGS=['/Zi', '/Od', '/EHsc', '/D_DEBUG', '/MDd', '/FS'])
        elif env['target'] == 'release':
            env.Append(CCFLAGS=['/Zi', '/O2', '/EHsc', '/DNDEBUG', '/MD', '/FS'])

    else:  #if host_platform == 'linux' or host_platform == 'osx':
        if host_platform == 'windows' and env['use_mingw']:
            env = env.Clone(tools=['mingw'])
            env["SPAWN"] = mySpawn
        # Cross-compilation using MinGW
        if env['bits'] == '64':
            mingw_prefix = 'x86_64-w64-mingw32-'
        elif env['bits'] == '32':
            mingw_prefix = 'i686-w64-mingw32-'
        if env["use_llvm"]:
            env["CC"] = mingw_prefix + "clang"
            env["AS"] = mingw_prefix + "as"
            env["CXX"] = mingw_prefix + "clang++"
            env["AR"] = mingw_prefix + "ar"
            env["RANLIB"] = mingw_prefix + "ranlib"
            env["LINK"] = mingw_prefix + "clang++"
            env.Append(LINKFLAGS=["-Wl,-pdb="])
            env.Append(CCFLAGS=["-gcodeview"])
        else:
            env["CC"] = mingw_prefix + "gcc"
            env["AS"] = mingw_prefix + "as"
            env["CXX"] = mingw_prefix + "g++"
            env["AR"] = mingw_prefix + "gcc-ar"
            env["RANLIB"] = mingw_prefix + "gcc-ranlib"
            env["LINK"] = mingw_prefix + "g++"
        env["SHCCFLAGS"] = '$CCFLAGS'

    # Native or cross-compilation using MinGW
    if host_platform == 'linux' or host_platform == 'osx' or env['use_mingw']:
        env.Append(CCFLAGS=['-g', '-O3', '-Wwrite-strings'])
        env.Append(CFLAGS=['-std=c11'])
        env.Append(CXXFLAGS=['-std=c++14'])
        if not env['use_llvm']:
            env.Append(LINKFLAGS=[
                '-Wl,--no-undefined',
            ])
        env.Append(LINKFLAGS=[
            '--static',
            '-static-libgcc',
            '-static-libstdc++',
        ])
elif env['platform'] == 'android':
    if host_platform == 'windows':
        env = env.Clone(tools=['mingw'])
        env["SPAWN"] = mySpawn

    # Verify NDK root
    if not 'ANDROID_NDK_ROOT' in env:
        raise ValueError("To build for Android, ANDROID_NDK_ROOT must be defined. Please set ANDROID_NDK_ROOT to the root folder of your Android NDK installation.")

    # Validate API level
    api_level = int(env['android_api_level'])
    if env['android_arch'] in ['x86_64', 'arm64v8'] and api_level < 21:
        print("WARN: 64-bit Android architectures require an API level of at least 21; setting android_api_level=21")
        env['android_api_level'] = '21'
        api_level = 21

    # Setup toolchain
    toolchain = env['ANDROID_NDK_ROOT'] + "/toolchains/llvm/prebuilt/"
    if host_platform == "windows":
        toolchain += "windows"
        import platform as pltfm
        if pltfm.machine().endswith("64"):
            toolchain += "-x86_64"
    elif host_platform == "linux":
        toolchain += "linux-x86_64"
    elif host_platform == "osx":
        toolchain += "darwin-x86_64"
    env.PrependENVPath('PATH', toolchain + "/bin") # This does nothing half of the time, but we'll put it here anyways

    # Get architecture info
    arch_info_table = {
        "armv7" : {
            "march":"armv7-a", "target":"armv7a-linux-androideabi", "tool_path":"arm-linux-androideabi", "compiler_path":"armv7a-linux-androideabi",
            "ccflags" : ['-mfpu=neon']
            },
        "arm64v8" : {
            "march":"armv8-a", "target":"aarch64-linux-android", "tool_path":"aarch64-linux-android", "compiler_path":"aarch64-linux-android",
            "ccflags" : []
            },
        "x86" : {
            "march":"i686", "target":"i686-linux-android", "tool_path":"i686-linux-android", "compiler_path":"i686-linux-android",
            "ccflags" : ['-mstackrealign']
            },
        "x86_64" : {"march":"x86-64", "target":"x86_64-linux-android", "tool_path":"x86_64-linux-android", "compiler_path":"x86_64-linux-android",
            "ccflags" : []
        }
    }
    arch_info = arch_info_table[env['android_arch']]

    # Setup tools
    env['CC'] = toolchain + "/bin/clang"
    env['CXX'] = toolchain + "/bin/clang++"
    env['AR'] = toolchain + "/bin/" + arch_info['tool_path'] + "-ar"

    env.Append(CCFLAGS=['--target=' + arch_info['target'] + env['android_api_level'], '-march=' + arch_info['march'], '-fPIC'])#, '-fPIE', '-fno-addrsig', '-Oz'])
    env.Append(CCFLAGS=arch_info['ccflags'])


#######################################
#### End godot-cpp/SConstruct #########
#######################################


Export("env")

godot_headers_path = ARGUMENTS.get("headers", os.getenv("GODOT_HEADERS", "godot-cpp/godot_headers"))
godot_bindings_path = ARGUMENTS.get("cpp_bindings", os.getenv("CPP_BINDINGS", "godot-cpp"))
external_path = 'src/external'


# default to debug build, must be same setting as used for cpp_bindings
target = ARGUMENTS.get("target", "debug")

if env['bits'] == 'default':
    env['bits'] = '64' if is64 else '32'

platform_suffix = '.' + env['platform'] + '.' + ("release" if target == "release_debug" else target) + '.' + env["bits"]

# put stuff that is the same for all first, saves duplication 
#if env["platform"] == "osx":
#    platform_suffix = ''

def add_sources(sources, dir):
    for f in os.listdir(dir):
        if f.endswith(".cpp"):
            sources.append(dir + "/" + f)

def add_suffix(libs):
    return [lib + platform_suffix for lib in libs]

env.Append(CPPPATH=[godot_headers_path,
	godot_bindings_path + '/include/',
	godot_bindings_path + '/include/core/',
	godot_bindings_path + '/include/gen/'] + 
        ([opus_path] if not use_builtin_opus else []) +
        ([libsamplerate_path] if not use_builtin_libsamplerate else []))

env.Append(LIBS=[add_suffix(['libgodot-cpp'])] +
        ([opus_library_path] if not use_builtin_opus else []) +
        ([libsamplerate_library_path] if not use_builtin_libsamplerate else []))

env.Append(LIBPATH=[godot_bindings_path + '/bin/'])

env["builtin_opus"] = use_builtin_opus
env["builtin_libsamplerate"] = use_builtin_libsamplerate
env['STATIC_AND_SHARED_OBJECTS_ARE_THE_SAME'] = 1

# fix needed on OSX
def rpath_fix(target, source, env):
    subprocess.check_call("%s -id @rpath/libgodot_speech.dylib %s" % (env["install_name_tool"], str(target[0])), shell=True)

sources = []
env.modules_sources = sources

SConscript("SCsub")

add_sources(sources, "./src")

dll_extension = ""
if env['platform'] == "windows":
    # Override scons default.
    dll_extension = ".dll"

library = env.SharedLibrary(target='bin/' + target + '/libGodotSpeech' + dll_extension, source=sources)
if env['platform'] == "osx":
    env.AddPostAction(library, rpath_fix)

Default(library)
