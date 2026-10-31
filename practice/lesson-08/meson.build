project('gstreamer-lesson-08', 'cpp', default_options: ['cpp_std=c++17', 'warning_level=3'])

gstreamer_dep = dependency('gstreamer-1.0', required: true)
glib_dep = dependency('glib-2.0', required: true)
warning_args = ['-Wall', '-Wextra', '-Wpedantic']

examples = {
  '01_bus_polling': 'src/01_bus_polling.cpp',
  '02_eos_handling': 'src/02_eos_handling.cpp',
  '03_error_warning': 'src/03_error_warning.cpp',
  '04_state_messages': 'src/04_state_messages.cpp',
  '05_async_bus_watch': 'src/05_async_bus_watch.cpp',
}

foreach name, source : examples
  executable(name, source, dependencies: [gstreamer_dep, glib_dep], cpp_args: warning_args)
endforeach
