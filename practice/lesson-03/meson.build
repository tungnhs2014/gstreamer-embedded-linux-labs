project(
  'gstreamer-lesson-03',
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
  '01_buffer_memory': 'src/01_buffer_memory.cpp',
  '02_mapping_writability': 'src/02_mapping_writability.cpp',
  '03_buffer_copy': 'src/03_buffer_copy.cpp',
  '04_buffer_metadata': 'src/04_buffer_metadata.cpp',
  '05_buffer_pool': 'src/05_buffer_pool.cpp',
  '06_pad_push_ownership': 'src/06_pad_push_ownership.cpp',
  '07_pad_probe_raii': 'src/07_pad_probe_raii.cpp',
}

foreach name, source : examples
  executable(
    name,
    source,
    dependencies: gstreamer_dep,
    cpp_args: warning_args,
  )
endforeach