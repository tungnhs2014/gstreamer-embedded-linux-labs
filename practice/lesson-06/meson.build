project('gstreamer-lesson-06', 'cpp', default_options: ['cpp_std=c++17', 'warning_level=3'])

gstreamer_dep = dependency('gstreamer-1.0', required: true)
warning_args = ['-Wall', '-Wextra', '-Wpedantic']

examples = {
  '01_device_discovery': 'src/01_device_discovery.cpp',
  '02_v4l2_capabilities': 'src/02_v4l2_capabilities.cpp',
  '03_raw_yuy2_capture': 'src/03_raw_yuy2_capture.cpp',
  '04_mjpeg_capture': 'src/04_mjpeg_capture.cpp',
  '05_camera_error': 'src/05_camera_error.cpp',
}

foreach name, source : examples
  executable(name, source, dependencies: gstreamer_dep, cpp_args: warning_args)
endforeach
