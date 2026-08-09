project(
  'gstreamer-lesson-04',
  'cpp',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
  ],
)

gstreamer_dep = dependency('gstreamer-1.0', required: true)
gstreamer_video_dep = dependency('gstreamer-video-1.0', required: true)

warning_args = [
  '-Wall',
  '-Wextra',
  '-Wpedantic',
]

executable(
  '01_video_format_layout',
  'src/01_video_format_layout.cpp',
  dependencies: [gstreamer_dep, gstreamer_video_dep],
  cpp_args: warning_args,
)

examples = {
  '02_yuy2_byte_layout': 'src/02_yuy2_byte_layout.cpp',
  '03_raw_yuy2_pipeline': 'src/03_raw_yuy2_pipeline.cpp',
  '04_raw_format_conversion': 'src/04_raw_format_conversion.cpp',
  '05_mjpeg_payload': 'src/05_mjpeg_payload.cpp',
  '06_mjpeg_decode': 'src/06_mjpeg_decode.cpp',
}

foreach name, source : examples
  executable(
    name,
    source,
    dependencies: gstreamer_dep,
    cpp_args: warning_args,
  )
endforeach