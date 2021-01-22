# Copyright 2020 Vijay Kumar Banerjee (vijay@rtems.org)
#
# This file's license is 2-clause BSD as in this distribution's LICENSE.2 file.
#

import rtems_waf.rtems as rtems
import rtems_waf.rtems_bsd as rtems_bsd
import os

def build(bld)
    rtems.build(bld)
    if rtems.check_lib(bld, ['bsd', 'lvgl'])
        arch_inc_path = rtems.arch_bsp_include_path(str(bld.env.RTEMS_VERSION),
                                                    str(bld.env.RTEMS_ARCH_BSP))
        include_paths = ['',
                         'lvgl',
                         'lvglsrc',
                         'lv_driversdisplay',
                         'lv_driversindev',]

        for i in range(0,len(include_paths))
            include_paths[i] = os.path.join(bld.env.PREFIX, arch_inc_path, include_paths[i])

        bld(features = 'c cprogram',
            target = 'lvgl_gui.exe',
            source = ['test.c'],
            includes = include_paths,
            lib = ['m', 'lvgl', 'bsd'])s