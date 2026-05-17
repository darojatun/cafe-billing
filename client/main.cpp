#include <gtk/gtk.h>
#include "ClientWindow.h"

int main(int argc, char* argv[]) {
    gtk_init(&argc, &argv);

    ClientWindow win;
    win.show();

    return 0;
}
