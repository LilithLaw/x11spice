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

/*----------------------------------------------------------------------------
**  options.c
**      Code to handle options.  This includes command line arguments
**  as well as options that can be set in configuration files.
**--------------------------------------------------------------------------*/

#include <glib.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include <spice/protocol.h>

#include "options.h"
#include "x11spice.h"

#if defined(HAVE_LIBAUDIT_H)
#include <libaudit.h>
#endif

static void str_replace(char **dest, const char *src)
{
    g_free(*dest);
    *dest = src ? g_strdup(src) : NULL;
}

void options_init(options_t *options)
{
    memset(options, 0, sizeof(*options));
}

static void ssl_options_free(ssl_options_t *ssl)
{
    str_replace(&ssl->ca_cert_file, NULL);
    str_replace(&ssl->certs_file, NULL);
    str_replace(&ssl->private_key_file, NULL);
    str_replace(&ssl->key_password, NULL);
    str_replace(&ssl->dh_key_file, NULL);
    str_replace(&ssl->ciphersuite, NULL);
}

void options_free(options_t *options)
{
    str_replace(&options->display, NULL);
    str_replace(&options->listen, NULL);

    ssl_options_free(&options->ssl);
    str_replace(&options->spice_password, NULL);
    str_replace(&options->password_file, NULL);

    str_replace(&options->virtio_path, NULL);
    str_replace(&options->uinput_path, NULL);
    str_replace(&options->on_connect, NULL);
    str_replace(&options->on_disconnect, NULL);
    str_replace(&options->user_config_file, NULL);
}


static void string_option(gchar **dest, GKeyFile *u, GKeyFile *s, const gchar *section, const gchar *key)
{
    gchar *ret = NULL;
    GError *error = NULL;

    if (u)
        ret = g_key_file_get_string(u, section, key, &error);
    if ((!u || error) && s)
        ret = g_key_file_get_string(s, section, key, NULL);
    if (error)
        g_error_free(error);

    g_free(*dest);
    *dest = ret;
}

static gint int_option(GKeyFile *u, GKeyFile *s, const gchar *section, const gchar *key)
{
    gint ret = 0;
    GError *error = NULL;

    if (u)
        ret = g_key_file_get_integer(u, section, key, &error);
    if ((!u || error) && s)
        ret = g_key_file_get_integer(s, section, key, NULL);
    if (error)
        g_error_free(error);

    return ret;
}

static gboolean bool_option(GKeyFile *u, GKeyFile *s, const gchar *section, const gchar *key)
{
    gboolean ret = FALSE;
    GError *error = NULL;

    if (u)
        ret = g_key_file_get_boolean(u, section, key, &error);
    if ((!u || error) && s)
        ret = g_key_file_get_boolean(s, section, key, NULL);
    if (error)
        g_error_free(error);

    return ret;
}

static void usage(char *argv0)
{
    char indent[256];

    snprintf(indent, sizeof(indent), "%*.*s ", (int) strlen(argv0), (int) strlen(argv0), "");
    printf("%s: [OPTIONS] [<listen-specification>]\n", argv0);
    printf("\n");
    printf("Starts a Spice server and connects it to an X11 display.\n");
    printf("\n");
    printf("The <listen-specification> is of the form:\n");
    printf("  [[host]:[port][-end-port]\n");
    printf("where host specifies the address to listen on.  Defaults to localhost\n");
    printf("      port specifies the port to listen to.  Defaults to 5900.\n");
    printf("      end-port, if given, will cause x11spice to scan from port to end-port\n");
    printf("      checking for an open port, and using the first one available.\n");
    printf("\n");
    printf("Options:\n");
    printf("%s [--allow-control]\n", indent);
    printf("%s [--no-allow-control]\n", indent);
    printf("%s [--timeout=<seconds>]\n", indent);
    printf("%s [--display=<DISPLAY>]\n", indent);
    printf("%s [--generate-password[=<len>]\n", indent);
    printf("%s [--password=<password>]\n", indent);
    printf("%s [--password-file={-|<password-file}]\n", indent);
    printf("%s [--config=<config-file>]\n", indent);
    printf("%s [--ssl[=<ssl-spec>]]\n", indent);
    printf("%s [--hide]\n", indent);
    printf("%s [--minimize]\n", indent);
}

static int options_handle_ssl(options_t *options, const char *spec)
{
    char *save = NULL;
    char *in = g_strdup(spec);
    char *p;
    int i = 0;
    int rc = 0;

    for (p = strtok_r(in, ",", &save); p; p = strtok_r(NULL, ",", &save), i++) {
        if (strlen(p) == 0)
            continue;

        switch(i) {
            case 0:
                str_replace(&options->ssl.ca_cert_file, p);
                break;
            case 1:
                str_replace(&options->ssl.certs_file, p);
                break;
            case 2:
                str_replace(&options->ssl.private_key_file, p);
                break;
            case 3:
                str_replace(&options->ssl.key_password, p);
                break;
            case 4:
                str_replace(&options->ssl.dh_key_file, p);
                break;
            case 5:
                str_replace(&options->ssl.ciphersuite, p);
                break;
            default:
                fprintf(stderr, "Error: invalid ssl specification.");
                rc = X11SPICE_ERR_BADARGS;
                break;
        }
    }

    g_free(in);
    return rc;
}

static void options_handle_ssl_file_options(options_t *options,
                                     GKeyFile *userkey, GKeyFile *systemkey)
{
    options->ssl.enabled = bool_option(userkey, systemkey, "ssl", "enabled");
    string_option(&options->ssl.ca_cert_file, userkey, systemkey, "ssl", "ca-cert-file");
    string_option(&options->ssl.certs_file, userkey, systemkey, "ssl", "certs-file");
    string_option(&options->ssl.private_key_file, userkey, systemkey, "ssl", "private-key-file");
    string_option(&options->ssl.key_password, userkey, systemkey, "ssl", "key-password-file");
    string_option(&options->ssl.dh_key_file, userkey, systemkey, "ssl", "dh-key-file");
    string_option(&options->ssl.ciphersuite, userkey, systemkey, "ssl", "ciphersuite");
}

static int options_parse_arguments(int argc, char *argv[], options_t *options)
{
    int rc;
    int longindex = 0;

    enum option_types {  OPTION_ALLOW_CONTROL, OPTION_DISALLOW_CONTROL,
                         OPTION_TIMEOUT, OPTION_AUTO, OPTION_HIDE,
                         OPTION_PASSWORD, OPTION_PASSWORD_FILE, OPTION_CONFIG, OPTION_SSL,
                         OPTION_GENERATE_PASSWORD, OPTION_DISPLAY, OPTION_MINIMIZE,
                         OPTION_HELP
    };

    static struct option long_options[] =
    {
        {"allow-control",            0, 0,       OPTION_ALLOW_CONTROL },
        {"no-allow-control",         0, 0,       OPTION_DISALLOW_CONTROL },
        {"timeout",                  1, 0,       OPTION_TIMEOUT  },
        {"auto",                     1, 0,       OPTION_AUTO },
        {"hide",                     0, 0,       OPTION_HIDE },
        {"password",                 1, 0,       OPTION_PASSWORD },
        {"password-file",            1, 0,       OPTION_PASSWORD_FILE },
        {"config",                   1, 0,       OPTION_CONFIG },
        {"ssl",                      2, 0,       OPTION_SSL},
        {"generate-password",        2, 0,       OPTION_GENERATE_PASSWORD },
        {"display",                  1, 0,       OPTION_DISPLAY },
        {"minimize",                 0, 0,       OPTION_MINIMIZE },
        {"help",                     0, 0,       OPTION_HELP},
        {0, 0, 0, 0}
    };

    optind = 1; /* Allow reuse of this function */
    while (1) {
        rc = getopt_long_only(argc, argv, "", long_options, &longindex);
        if (rc == -1) {
            rc = 0;
            break;
        }

        switch (rc) {
            case OPTION_ALLOW_CONTROL:
                options->allow_control = 1;
                break;

            case OPTION_DISALLOW_CONTROL:
                options->allow_control = 0;
                break;

            case OPTION_TIMEOUT:
                options->timeout = atol(optarg);
                break;

            case OPTION_HIDE:
                options->hide = 1;
                break;

            case OPTION_PASSWORD:
                str_replace(&options->spice_password, optarg);
                break;

            case OPTION_PASSWORD_FILE:
                str_replace(&options->password_file, optarg);
                break;

            case OPTION_CONFIG:
                str_replace(&options->user_config_file, optarg);
                break;

            case OPTION_SSL:
                options->ssl.enabled = 1;
                if (optarg) {
                    rc = options_handle_ssl(options, optarg);
                    if (rc)
                        return rc;
                }
                break;

            case OPTION_GENERATE_PASSWORD:
                options->generate_password = DEFAULT_PASSWORD_LENGTH;
                if (optarg)
                    options->generate_password = atol(optarg);
                break;

            case OPTION_DISPLAY:
                str_replace(&options->display, optarg);
                break;

            case OPTION_MINIMIZE:
                options->minimize = 1;
                break;

            default:
                usage(argv[0]);
                return X11SPICE_ERR_BADARGS;
        }
    }

    /* Make sure conflicting password options are not given */
    if (rc == 0) {
        int count = 0;
        count += options->password_file ? 1 : 0;
        count += options->spice_password ? 1 : 0;
        count += options->generate_password ? 1 : 0;
        if (count > 1) {
            fprintf(stderr, "Error: you can specify only one of password, password-file, "
                            "and generate-password\n");
            rc = X11SPICE_ERR_BADARGS;
        }
    }

    /* Grab the listen spec, if given */
    if (rc == 0) {
        if (optind >= argc) {
            /* Default */
            if (options->listen == NULL) {
                str_replace(&options->listen, "5900");
            }
        } else if (optind < (argc - 1)) {
            fprintf(stderr, "Error: too many arguments\n");
            rc = X11SPICE_ERR_BADARGS;
        } else {
            str_replace(&options->listen, argv[optind]);
        }
    }

    return rc;
}

static void options_from_config(options_t *options)
{
    GKeyFile *userkey = g_key_file_new();
    GKeyFile *systemkey = NULL;
    int config_file_given = options->user_config_file ? TRUE : FALSE;

    if (!config_file_given) {
        options->user_config_file = g_build_filename(g_get_user_config_dir(), "x11spice/x11spice.conf", NULL);

        systemkey = g_key_file_new();
        if (!g_key_file_load_from_dirs(systemkey, "x11spice/x11spice.conf",
                                       (const char **) g_get_system_config_dirs(),
                                       NULL, G_KEY_FILE_NONE, NULL)) {
            g_key_file_free(systemkey);
            systemkey = NULL;
        }
    }

    if (!g_key_file_load_from_file(userkey, options->user_config_file, G_KEY_FILE_NONE, NULL)) {
        g_key_file_free(userkey);
        userkey = NULL;
    }

    options->timeout = int_option(userkey, systemkey, "spice", "timeout");
    options->minimize = bool_option(userkey, systemkey, "spice", "minimize");
    options->allow_control = bool_option(userkey, systemkey, "spice", "allow-control");
    options->generate_password = int_option(userkey, systemkey, "spice", "generate-password");
    options->hide = bool_option(userkey, systemkey, "spice", "hide");
    string_option(&options->display, userkey, systemkey, "spice", "display");

    string_option(&options->listen, userkey, systemkey, "spice", "listen");
    string_option(&options->spice_password, userkey, systemkey, "spice", "password");
    string_option(&options->password_file, userkey, systemkey, "spice", "password-file");
    options->disable_ticketing = bool_option(userkey, systemkey, "spice", "disable-ticketing");
    options->exit_on_disconnect = bool_option(userkey, systemkey, "spice", "exit-on-disconnect");
    string_option(&options->virtio_path, userkey, systemkey, "spice", "virtio-path");
    string_option(&options->uinput_path, userkey, systemkey, "spice", "uinput-path");
    string_option(&options->on_connect, userkey, systemkey, "spice", "on-connect");
    string_option(&options->on_disconnect, userkey, systemkey, "spice", "on-disconnect");
    options->audit = bool_option(userkey, systemkey, "spice", "audit");
    options->audit_message_type = int_option(userkey, systemkey, "spice", "audit-message-type");
    options->always_trust_damage = bool_option(userkey, systemkey, "spice", "always-trust-damage");

#if defined(HAVE_LIBAUDIT_H)
    /* Pick an arbitrary default in the user range.  CodeWeavers was founed in 1996, so 1196 it is... */
    if (options->audit_message_type == 0)
        options->audit_message_type = AUDIT_LAST_USER_MSG - 3;
#endif

    options_handle_ssl_file_options(options, userkey, systemkey);

    if (systemkey)
        g_key_file_free(systemkey);
    if (userkey)
        g_key_file_free(userkey);

    g_debug("options listen '%s', disable_ticketing %d", options->listen,
            options->disable_ticketing);
}

static int process_password_file(options_t *options)
{
    int rc = 0;
    FILE *fp;
    char *p;
    char buf[SPICE_MAX_PASSWORD_LENGTH + 1];

    if (strcmp(options->password_file, "-") == 0) {
        printf("Enter password: ");
        fflush(stdout);
        fp = stdin;
    }
    else {
        fp = fopen(options->password_file, "r");
        if (!fp)
            return X11SPICE_ERR_OPEN;
    }
    if (!fgets(buf, sizeof(buf), fp))
        rc = X11SPICE_ERR_PARSE;

    if (strcmp(options->password_file, "-") != 0)
        fclose(fp);

    /* Strip a trailing \n */
    p = buf + strlen(buf);
    if (p > buf && *(p - 1) == '\n')
        *(p - 1) = '\0';

    str_replace(&options->spice_password, buf);

    return rc;
}

static int generate_password(options_t *options)
{
    int fd;
    int rc;
    char *p;

    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return X11SPICE_ERR_OPEN;

    p = options->spice_password = g_malloc(options->generate_password + 1);

    while (p - options->spice_password < options->generate_password) {
        rc = read(fd, p, sizeof(*p));
        if (rc == 0 || (rc == -1 && errno != EINTR))
            return -1;

        if (isalnum(*p))
            p++;
    }
    *p = '\0';

    close(fd);

    return 0;
}

static int options_process_io(options_t *options)
{
    int rc;
    if (options->password_file) {
        rc = process_password_file(options);
        if (rc)
            return rc;
    }

    if (options->generate_password) {
        rc = generate_password(options);
        if (rc)
            return rc;
        printf("PASSWORD=%s\n", options->spice_password);
        fflush(stdout);
    }

    return 0;
}

int options_load(options_t *options, int argc, char *argv[])
{
    int rc;

    rc = options_parse_arguments(argc, argv, options);
    if (rc == 0) {
        options_from_config(options);
        /* We parse command line arguments a second time to ensure
        **  that command line options take precedence over config files */
        rc = options_parse_arguments(argc, argv, options);
        if (rc == 0)
            rc = options_process_io(options);
    }
    return rc;
}


int options_impossible_config(options_t *options)
{
    if (options->spice_password)
        return 0;

    if (options->generate_password || options->password_file)
        return 0;

    if (options->disable_ticketing)
        return 0;

    return 1;
}
