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
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <glib.h>

int redirect(char *fname)
{
    int fd;
    fd = open(fname, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        perror(fname);
        return -1;
    }

    dup2(fd, fileno(stdout));
    dup2(fd, fileno(stderr));

    return 0;
}


int still_alive(int pid)
{
    return !waitpid(pid, NULL, WNOHANG);
}

int spawn_command(char *cmd, char *output_file, int *pid)
{
    *pid = fork();
    if (*pid == 0) {
        if (redirect(output_file))
            return -1;

        execl("/bin/sh", "sh", "-c", cmd, NULL);
        g_error("exec of [%s] failed", cmd);
    }

    return 0;
}
