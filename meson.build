project(
  'ppack',
  'c',
  version: '2.5.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
  license: 'MIT',
)

# === Version header ===========================================================

ver = meson.project_version().split('.')

major = ver[0].to_int()
minor = ver[1].to_int()
patch = ver[2].to_int()

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set('MAJOR', major)
conf.set('MINOR', minor)
conf.set('PATCH', patch)

configure_file(
  input: 'config' / 'ppack_version.h.in',
  output: 'ppack_version.h',
  configuration: conf,
  install: true,
  install_dir: get_option('includedir') / 'ppack',
)

# === Include directories ======================================================

public_headers = include_directories('include')

# === Application core library =================================================
# Hardware-agnostic application logic.  Linked by both the application
# executable and the unit-test suite (which supplies its own platform mocks).

library_sources = ['src/ppack.c']

ppack_lib = static_library(
  'ppack',
  library_sources,
  include_directories: public_headers,
  install: true,
  install_dir: get_option('libdir'),
)

# Install public header
install_headers(['include/ppack.h', 'include/ppack_platform.h'], subdir: 'ppack')

ppack_dep = declare_dependency(
  include_directories: public_headers,
  link_with: ppack_lib,
)

# === Host unit tests ==========================================================

if get_option('build_tests')
  # Check header configuration separately from the runtime size checks.
  cc = meson.get_compiler('c')
  foreach bits : ['8', '512', '65528']
    assert(cc.compiles(
      '#include "ppack.h"',
      include_directories: public_headers,
      args: ['-UPPACK_PAYLOAD_BITS', '-DPPACK_PAYLOAD_BITS=' + bits],
      name: 'valid default payload size ' + bits,
    ), 'Valid PPACK_PAYLOAD_BITS rejected: ' + bits)
  endforeach
  foreach bits : ['0', '7', '65529', '65536']
    assert(not cc.compiles(
      '#include "ppack.h"',
      include_directories: public_headers,
      args: ['-UPPACK_PAYLOAD_BITS', '-DPPACK_PAYLOAD_BITS=' + bits],
      name: 'invalid default payload size ' + bits,
    ), 'Invalid PPACK_PAYLOAD_BITS accepted: ' + bits)
  endforeach

  # Second build of the library with the 16-bit MAU simulation flag, so the
  # test binary linking against it actually exercises the word-addressable
  # code path end-to-end (including ppack_u8_t sizing inside the library).
  ppack_sim_lib = static_library(
    'ppack_sim',
    library_sources,
    include_directories: public_headers,
    c_args: ['-DPPACK_SIMULATE_16BIT_MAU'],
    install: false,
  )

  ppack_sim_dep = declare_dependency(
    include_directories: public_headers,
    link_with: ppack_sim_lib,
    compile_args: ['-DPPACK_SIMULATE_16BIT_MAU'],
  )

  subdir('tests')
endif
