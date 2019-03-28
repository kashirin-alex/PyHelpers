# -*- coding: utf-8 -*-
# AUTHOR Kashirin Alex (kashirin.alex@gmail.com) #

from distutils.core import setup, Extension
from distutils import sysconfig

library_dirs = ['/'.join(['']+sysconfig.get_python_lib().split('/')[0:1]+['lib'])]
include_dirs = [sysconfig.get_python_inc(plat_specific=True), '/usr/local/include', '/usr/include']


extenstions = [
    Extension('pyhelpers.udp_handler_dest',
              sources=['pyhelpers/udp_handler_dest.cc'],
              include_dirs=include_dirs,
              libraries=['tcmalloc_minimal'],
              library_dirs=library_dirs,
              extra_compile_args=['-D_LARGEFILE_SOURCE', '-D_FILE_OFFSET_BITS=64', '-m64', '-D_REENTRANT', '-DNDEBUG',
                                  '-s', '-static-libgcc', '-static-libstdc++', '-fPIC', '-std=c++17',
                                  '-O3', '-flto', '-fuse-linker-plugin', '-ffat-lto-objects', '-floop-interchange',
                                  '-fno-builtin-malloc', '-fno-builtin-calloc', '-fno-builtin-realloc', '-fno-builtin-free'],
              # language='c++17',
              ),
    # from pyhelpers.udp_handler_dest import UdpHandlerDest
    Extension('pyhelpers.tcmalloc',
              sources=['pyhelpers/tcmalloc.cc'],
              include_dirs=include_dirs,
              libraries=['tcmalloc_minimal'],
              library_dirs=library_dirs,
              extra_compile_args=['-D_LARGEFILE_SOURCE', '-D_FILE_OFFSET_BITS=64', '-m64', '-D_REENTRANT', '-DNDEBUG',
                                  '-s', '-static-libgcc', '-static-libstdc++', '-fPIC', '-std=c++17',
                                  '-O3', '-flto', '-fuse-linker-plugin', '-ffat-lto-objects', '-floop-interchange',
                                  '-DTCMALLOC_MINIMAL',  #-ltcmalloc', '-lunwind',
                                  '-fno-builtin-malloc', '-fno-builtin-calloc', '-fno-builtin-realloc', '-fno-builtin-free'],
              # language='c++17',
              ),
    # from pyhelpers.tcmalloc import TCMalloc
]

setup(
    name='PyHelpers',
    version='0.0.1',
    description='Python Helper Extensions',
    long_description='',

    url='https://github.com/kashirin-alex/PyHelpers',
    license='apache-2',

    package_dir={
      'pyhelpers': 'pyhelpers',
    },
    packages=[
      'pyhelpers'
    ],

    maintainer='Kashirin Alex',
    maintainer_email='kashirin.alex@gmail.com',
    ext_modules=extenstions
)
