#include <gtk/gtk.h>
#include <cstdlib>
#include <cstdio>
#include "Database.h"
#include "BillingServer.h"
#include "MainWindow.h"

int main(int argc, char* argv[]) {
    gtk_init(&argc, &argv);

    // Resolve database path (~/.cafebill.db)
    const char* home = getenv("HOME");
    std::string db_path = home
        ? std::string(home) + "/.cafebill.db"
        : "cafebill.db";

    // 1. Open database
    Database db(db_path);
    if (!db.init()) {
        GtkWidget* err = gtk_message_dialog_new(
            nullptr, GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Failed to open database:\n%s", db_path.c_str());
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        return 1;
    }

    CafeSetting settings = db.loadSettings();

    // 2. Create the window first (server ptr injected below)
    MainWindow window(nullptr, &db);

    // 3. Create server, passing the window
    BillingServer server(&db, &window);

    // 4. Inject server reference into window
    window.setServer(&server);

    // 5. Start listening
    if (!server.start(settings.server_port)) {
        GtkWidget* err = gtk_message_dialog_new(
            nullptr, GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Failed to start server on port %d.\n"
            "The port may already be in use.",
            settings.server_port);
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        return 1;
    }

    // 6. Enter GTK main loop (blocks until window closed)
    window.show();

    server.stop();
    return 0;
}
