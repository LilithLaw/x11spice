/*
    Copyright (C) 2016  Jeremy White <jwhite@codeweavers.com>
    All rights reserved.

    This file is part of x11spice

    x11spice is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    x11spice is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with x11spice.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <stdio.h>
#include <locale.h>

#include "xdummy.h"
#include "tests.h"

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    g_test_init(&argc, &argv, NULL);

    g_test_add("/x11spice/basic", xdummy_t, "basic", start_server, test_basic, stop_server);

    g_test_add("/x11spice/resize", xdummy_t, "resize", start_server, test_resize, stop_server);

    g_test_add("/x11spice/tallscreen", xdummy_t, "tallscreen", start_server, test_tallscreen,
               stop_server);

    g_test_add("/x11spice/x11perf1", xdummy_t, "x11perf1", start_server, test_script, stop_server);

#if defined(SPICEDUMMY)
    g_test_add("/x11spice/piglit1", xdummy_t, "piglit1", start_server, test_script, stop_server);
#endif

    g_log_set_always_fatal(G_LOG_LEVEL_ERROR);

    return g_test_run();
}
