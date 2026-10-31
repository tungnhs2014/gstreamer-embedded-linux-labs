project('gstreamer-lesson-09', 'cpp', default_options: ['cpp_std=c++17', 'warning_level=3'])

gstreamer_dep = dependency('gstreamer-1.0', required: true)
gstreamer_app_dep = dependency('gstreamer-app-1.0', required: true)

examples = {
  '01_clock_timestamps': 'src/01_clock_timestamps.cpp',
  '02_tee_queues': 'src/02_tee_queues.cpp',
  '03_appsrc_appsink': 'src/03_appsrc_appsink.cpp',
  '04_queue_drop': 'src/04_queue_drop.cpp',
}

foreach name, source : examples
  executable(name, source,
    dependencies: [gstreamer_dep, gstreamer_app_dep],
    cpp_args: ['-Wall', '-Wextra', '-Wpedantic'])
endforeach
