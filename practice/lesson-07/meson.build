project('gstreamer-lesson-07', 'cpp', default_options: ['cpp_std=c++17', 'warning_level=3'])

gstreamer_dep = dependency('gstreamer-1.0', required: true)
warning_args = ['-Wall', '-Wextra', '-Wpedantic']

examples = {
  '01_basic_states': 'src/01_basic_states.cpp',
  '02_async_transition': 'src/02_async_transition.cpp',
  '03_nonlive_preroll': 'src/03_nonlive_preroll.cpp',
  '04_live_no_preroll': 'src/04_live_no_preroll.cpp',
  '05_restart_pipeline': 'src/05_restart_pipeline.cpp',
}

foreach name, source : examples
  executable(name, source, dependencies: gstreamer_dep, cpp_args: warning_args)
endforeach
