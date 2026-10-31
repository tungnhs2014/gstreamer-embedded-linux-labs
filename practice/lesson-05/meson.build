project('gstreamer-lesson-05', 'cpp', default_options: ['cpp_std=c++17', 'warning_level=3'])

gstreamer_dep = dependency('gstreamer-1.0', required: true)
warning_args = ['-Wall', '-Wextra', '-Wpedantic']

examples = {
  '01_caps_fundamentals': 'src/01_caps_fundamentals.cpp',
  '02_caps_intersection': 'src/02_caps_intersection.cpp',
  '03_pad_caps_views': 'src/03_pad_caps_views.cpp',
  '04_runtime_negotiation': 'src/04_runtime_negotiation.cpp',
  '05_negotiation_failure': 'src/05_negotiation_failure.cpp',
}

foreach name, source : examples
  executable(name, source, dependencies: gstreamer_dep, cpp_args: warning_args)
endforeach
