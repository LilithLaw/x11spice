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

#ifndef OPTIONS_H_
#define OPTIONS_H_

/*----------------------------------------------------------------------------
**  constants
**--------------------------------------------------------------------------*/
#define DEFAULT_PASSWORD_LENGTH     8

/*----------------------------------------------------------------------------
**  Structure definitions
**--------------------------------------------------------------------------*/
typedef struct {
    int enabled;
    char *ca_cert_file;
    char *certs_file;
    char *private_key_file;
    char *key_password;
    char *dh_key_file;
    char *ciphersuite;
} ssl_options_t;

typedef enum { AUTO_TRUST, ALWAYS_TRUST, NEVER_TRUST } damage_trust_t;

typedef struct {
    /* Both config and command line arguments */
    long timeout;
    int minimize;
    int allow_control;
    int generate_password;
    int hide;
    char *display;
    char *listen;

    ssl_options_t ssl;

    /* config only */
    char *spice_password;
    char *password_file;
    int disable_ticketing;
    int exit_on_disconnect;
    char *virtio_path;
    char *uinput_path;
    char *on_connect;
    char *on_disconnect;
    int audit;
    int audit_message_type;
    damage_trust_t trust_damage;
    int full_screen_fps;

    /* file names of config files */
    char *user_config_file;
} options_t;


/*----------------------------------------------------------------------------
**  Prototypes
**--------------------------------------------------------------------------*/
void options_init(options_t *options);
void options_free(options_t *options);
int options_load(options_t *options, int argc, char *argv[]);
int options_impossible_config(options_t *options);

#endif
