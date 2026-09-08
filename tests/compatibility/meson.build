# Optional side-by-side comparison. Supply an unmodified legacy checkout.
project('ppack-legacy-comparison', 'c', default_options: ['c_std=c11'])

legacy_root = get_option('legacy_root')
assert(legacy_root != '', 'Set -Dlegacy_root to an absolute legacy source path')
current_root = '../..'
current_headers = include_directories(current_root / 'include')
legacy_headers = include_directories(legacy_root / 'include')

foreach simulate : [false, true]
  suffix = simulate ? '-sim' : '-native'
  flags = simulate ? ['-DPPACK_SIMULATE_16BIT_MAU'] : []
  legacy = static_library(
    'legacy' + suffix,
    legacy_root / 'src' / 'ppack.c',
    include_directories: legacy_headers,
    c_args: flags + ['-Dppack_pack=legacy_pack', '-Dppack_unpack=legacy_unpack'],
  )
  current = static_library(
    'current' + suffix,
    current_root / 'src' / 'ppack.c',
    include_directories: current_headers,
    c_args: flags,
  )
  exe = executable(
    'compare' + suffix,
    'test_legacy_compat.c',
    include_directories: [current_headers, include_directories('..')],
    c_args: flags,
    link_with: [legacy, current],
  )
  test('legacy comparison' + suffix, exe, timeout: 120)
endforeach
