load('//:subdir_glob.bzl', 'subdir_glob')

prebuilt_cxx_library(
  name = 'ftl',
  header_namespace = '',
  header_only = True,
  exported_headers = subdir_glob([
    ('include', '**/*.h')
  ]),
  visibility = ['PUBLIC']
)

cxx_binary(
  name = 'example',
  deps = [':ftl'],
  headers = subdir_glob([
    ('examples', '**/*.h')
  ]),
  srcs = glob(['examples/**/*.cpp'])
)
