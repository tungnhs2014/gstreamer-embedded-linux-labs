project(
  'gstreamer-lesson-02',
  'cpp',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
  ],
)

gstreamer_dep = dependency('gstreamer-1.0', required: true)

warning_args = [
  '-Wall',
  '-Wextra',
  '-Wpedantic',
]

examples = {
  '01_registry_factory': 'src/01_registry_factory.cpp',
  '02_pad_template': 'src/02_pad_template.cpp',
  '03_bin_pipeline': 'src/03_bin_pipeline.cpp',
  '04_static_linking': 'src/04_static_linking.cpp',
  '05_request_pad': 'src/05_request_pad.cpp',
  '06_ghost_pad': 'src/06_ghost_pad.cpp',
  '07_dynamic_pad': 'src/07_dynamic_pad.cpp',
}

foreach name, source : examples
  executable(
    name,
    source,
    dependencies: gstreamer_dep,
    cpp_args: warning_args,
  )
endforeach