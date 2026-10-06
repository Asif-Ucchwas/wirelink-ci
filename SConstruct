# WireLink-CI build.
#   scons -Q          build everything
#   scons -Q test     build and run the unit tests (exit code 0 = all passed)
#   scons -Qc         clean

env = Environment(
    CC='gcc',
    CFLAGS=['-std=c11'],
    CCFLAGS=['-Wall', '-Wextra', '-Werror', '-O2'],
    CPPPATH=['#src'],
)

# Keep object files out of src/ and tests/: everything generated lands in build/.
env.VariantDir('build/src', 'src', duplicate=0)
env.VariantDir('build/tests', 'tests', duplicate=0)

frame_lib = env.StaticLibrary('build/frame', ['build/src/frame.c'])

test_frame = env.Program('build/test_frame', ['build/tests/test_frame.c', frame_lib])

# 'scons -Q test' always runs the tests, even if nothing was rebuilt.
run_tests = env.Command("run_tests", test_frame, "./$SOURCE")
AlwaysBuild(run_tests)
env.Alias('test', run_tests)

Default(frame_lib, test_frame)
