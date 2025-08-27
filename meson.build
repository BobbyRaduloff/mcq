project('mcq', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'c_std=c23', 'cpp_std=c++23'])

exe = executable('mcq', 
  ['src/mcq.cpp', 'src/servers/STBlockingHTTPServer.cpp', 'src/servers/MTBlockingHTTPServer.cpp'],
  install : true)

test('basic', exe)
