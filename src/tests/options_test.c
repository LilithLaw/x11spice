#undef NDEBUG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <glib.h>

#include "options.h"

static void test(const char *cmd_line, const char *expected)
{
    options_t options;
    int argc = 0;
    int rc;

    char **argv = g_strsplit_set(cmd_line, " ", 0);
    while (argv[argc]) {
        ++argc;
    }

    options_init(&options);
    rc = options_load(&options, argc, argv);
    g_strfreev(argv);
    if (rc != 0) {
        fprintf(stderr, "Unable to load options, rc %d\n", rc);
        options_free(&options);
        exit(1);
    }

    // parse expected fields
    for (;;) {
        char name[102], value[102], buf[102];
        int pos = -1;
        // 2 next token are couple <NAME> <VALUE>
        if (sscanf(expected, " %100s %100s %n", name, value, &pos) < 2)
            break;
        assert(pos > 0);

#define VAL(fmt, fld) \
        if (strcmp(name, #fld) == 0) \
            snprintf(buf, sizeof(buf), fmt, options.fld); \
        else

        // extract expected value
        VAL("%s", display)
        VAL("%d", allow_control)
        VAL("%s", listen)
        VAL("%s", ssl.ca_cert_file)
        {
            fprintf(stderr, "name '%s' not recognized\n", name);
            exit(1);
        }

        // compare expected and parsed
        if (strcmp(buf, value) != 0) {
            fprintf(stderr, "Unexpected results:\n\tcmd:'%s'\n\tfld:%s\n\tout:%s\n\texp:%s\n",
                    cmd_line, name, buf, value);
            exit(1);
        }
        expected += pos;
    }
    options_free(&options);
}

#define TEST(cmd, exp) test("PROGRAM " cmd, exp)

int main(int argc, char **argv)
{
    FILE *f = fopen("test.conf", "w");

    fprintf(f, "[spice]\ndisplay=config_display\nlisten=8765\nallow-control=true");
    fclose(f);

    // some test, input and output
    test("PROGRAM", "display (null) allow_control 0 listen 5900 ssl.ca_cert_file (null)");
    TEST("--hide", "display (null) allow_control 0 listen 5900 ssl.ca_cert_file (null)");
    TEST("1234",
         "display (null) allow_control 0 listen 1234");
    TEST("--config=test.conf",
         "display config_display allow_control 1 listen 8765");
    TEST("--config=test.conf 123",
         "display config_display allow_control 1 listen 123");
    TEST("--no-allow-control --config=test.conf 123",
         "display config_display allow_control 0 listen 123");
    TEST("--display DISPLAY --config=test.conf 123",
         "display DISPLAY allow_control 1 listen 123 ssl.ca_cert_file (null)");
    TEST("--ssl=foo",
         "display (null) allow_control 0 listen 5900 ssl.ca_cert_file foo");

    unlink("test.conf");
    return 0;
}
