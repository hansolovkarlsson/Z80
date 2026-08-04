// A thin GTK4 launcher for the real bin/z80 CLI emulator - not a
// terminal emulator of its own. It spawns the existing, completely
// unmodified bin/z80 binary as a child process attached to a pty, and
// hands that pty to a VteTerminal widget, which does the actual
// VT100/ANSI interpretation (the same job a real terminal app like
// Terminal.app/iTerm already does for bin/z80 today - this just means
// the program doesn't need a separate terminal window/app to run in).
// z80.c/cpm.c/main.c are untouched by this: console_emit()/
// console_read_char() already just talk to stdin/stdout, and a pty's
// slave side *is* stdin/stdout from the child's point of view.
//
// Kept as its own binary/build target (`make gtk`, opt-in - see the
// Makefile) rather than a flag on bin/z80, so the default `make`/`make
// test` build stays free of the GTK4+VTE dependency this needs.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <mach-o/dyld.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

// This shell's default open-file soft limit (`ulimit -n`) is over a
// million on this machine. VTE's vte_terminal_spawn_async() closes every
// inherited file descriptor below that limit in the child before exec
// (glib's fdwalk() fallback, since macOS has no /proc/self/fd to just
// list the ones that are actually open) - with a million-plus as the
// ceiling, that walk overflows the spawn thread's stack. Confirmed via a
// real crash report (~/Library/Logs/DiagnosticReports/z80-gtk-*.ips):
// EXC_BAD_ACCESS, "Thread stack size exceeded", straight in
// vte::base::SpawnContext::exec -> fdwalk -> __chkstk_darwin. Capping
// RLIMIT_NOFILE down here, before any GTK/VTE call, keeps that walk
// bounded - bin/z80 itself never needs more than a handful of fds.
static void lower_fd_limit(void) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) return;
    const rlim_t cap = 4096;
    if (rl.rlim_cur > cap) {
        rl.rlim_cur = cap;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
}

// Locates the real bin/z80 binary as a sibling of this one (both build
// into bin/), using _NSGetExecutablePath() (the standard macOS way to
// find your own binary's real path, robust to how it was invoked - via
// PATH, a relative path, a symlink, etc. - unlike trusting argv[0]
// as-is). Caller frees the result.
static char *find_sibling_z80(void) {
    char path[4096];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) return NULL;
    char *real = realpath(path, NULL);
    if (!real) return NULL;
    char *slash = strrchr(real, '/');
    if (!slash) {
        free(real);
        return NULL;
    }
    *slash = '\0';
    size_t len = strlen(real) + strlen("/z80") + 1;
    char *result = malloc(len);
    snprintf(result, len, "%s/z80", real);
    free(real);
    return result;
}

// The bin/z80 argv this launcher was given (everything after its own
// argv[0]) - stashed here since GTK's own activate callback takes no
// argument for it. Built once in main() before g_application_run().
static char **g_child_argv = NULL;

// bin/z80 resolves .com paths (e.g. "cpm_disk/hanoi.com") relative to
// its own working directory, same as running it directly from a shell -
// captured explicitly here and passed to the spawn call rather than
// relying on whatever NULL's own default working directory turns out
// to be.
static char g_cwd[4096];

static void on_child_exited(VteTerminal *terminal, int status, gpointer user_data) {
    (void)terminal;
    (void)status;
    (void)user_data;
    // Deliberately left open (not auto-closing the window) - matches
    // real terminal-app behavior of leaving the pane open after the
    // child exits, so the program's own final output (bin/z80's own
    // "Program terminated normally..."/T-state count) stays visible
    // until the user closes it themselves.
}

static void on_spawn_complete(VteTerminal *terminal, GPid pid, GError *error, gpointer user_data) {
    (void)user_data;
    if (error != NULL) {
        g_printerr("Failed to launch bin/z80: %s\n", error->message);
        return;
    }
    (void)terminal;
    (void)pid;
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Z80 / CP/M");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 500);

    GtkWidget *terminal = vte_terminal_new();
    gtk_window_set_child(GTK_WINDOW(window), terminal);
    g_signal_connect(terminal, "child-exited", G_CALLBACK(on_child_exited), NULL);

    vte_terminal_spawn_async(
        VTE_TERMINAL(terminal),
        VTE_PTY_DEFAULT,
        g_cwd,            // working directory: same as ours (see g_cwd's comment)
        g_child_argv,     // argv: bin/z80 <its own args...>
        NULL,             // envv: inherit ours
        G_SPAWN_DEFAULT,
        NULL, NULL, NULL, // child_setup
        -1,               // no timeout
        NULL,             // cancellable
        on_spawn_complete,
        NULL);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char *argv[]) {
    lower_fd_limit();

    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        printf("Usage:\n");
        printf("  %s <program.com> [args...]   Run a CP/M .com file in a GTK window\n", argv[0]);
        printf("  %s --ccp [ccp.com]           Boot a CP/M CCP shell in a GTK window\n", argv[0]);
        printf("\n");
        printf("A thin wrapper: every argument is passed straight through to bin/z80\n");
        printf("(found as a sibling of this binary), running inside a VTE terminal widget.\n");
        return argc < 2 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    char *z80_path = find_sibling_z80();
    if (!z80_path) {
        fprintf(stderr, "Could not locate bin/z80 next to this binary.\n");
        return EXIT_FAILURE;
    }

    if (!getcwd(g_cwd, sizeof(g_cwd))) {
        fprintf(stderr, "Could not determine the current directory.\n");
        return EXIT_FAILURE;
    }

    // Build the child's argv: [z80_path, argv[1], argv[2], ..., NULL].
    g_child_argv = malloc((size_t)argc * sizeof(char *));
    g_child_argv[0] = z80_path;
    for (int i = 1; i < argc; i++) {
        g_child_argv[i] = argv[i];
    }
    g_child_argv[argc] = NULL;

    GtkApplication *app = gtk_application_new(NULL, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    // Passing 0/NULL rather than the real argc/argv: GTK's own
    // command-line handling would otherwise try to interpret
    // bin/z80's arguments (a .com path, "--ccp") as its own options.
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);

    free(z80_path);
    free(g_child_argv);
    return status;
}
