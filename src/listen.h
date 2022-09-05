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

#ifndef LISTEN_H_
#define LISTEN_H_


/*----------------------------------------------------------------------------
**  Prototypes
**--------------------------------------------------------------------------*/
int listen_parse(const char *listen_spec, char **addr, int *port_start, int *port_end, int *flags);
int listen_find_open_port(const char *addr, int start, int end, int *port);

#endif
