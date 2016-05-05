#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

// Compile with command:
// gcc gtkudpserver.c -o gtkudpserver `pkg-config --cflags --libs gtk+-2.0`

// Data structure that holds all of the main application data
typedef struct{
    GtkWidget *DMainWindow;
    GtkWidget *DVerticalBox;
    GtkWidget *DHorizontalBox;
    GtkWidget *DScrolledWindow;
    GtkWidget *DTextView;
    GtkWidget *DTextEntry;
    GtkWidget *DSetButton;
    GIOChannel *DIOChannel;
    int DFileDescriptor;
    char DMessage[128];
} SApplicationData, *SApplicationDataRef;

void error(const char *message){
    perror(message);
    exit(1);
}

// This is a callback function, the data argument is assumed SApplicationDataRef
static void SetButtonClick( GtkWidget *widget, gpointer data){
    SApplicationDataRef AppData = (SApplicationDataRef)data;
    
    // Assign the text of the DTextEntry into DMessage for future replies
    strcpy(AppData->DMessage, gtk_entry_get_text(GTK_ENTRY(AppData->DTextEntry)));
    g_print("Set message to %s\n", AppData->DMessage);
}

// This is a callback function to signify that the delete windows has been called.
// If you return FALSE in the "delete-event" signal handler, GTK will emit the 
// "destroy" signal. Returning TRUE means you don't want the window to be 
// destroyed. This is useful for popping up 'are you sure you want to quit?'
// type dialogs.
static gboolean MainWindowDeleteEvent(GtkWidget *widget, GdkEvent *event, gpointer data){
    SApplicationDataRef AppData = (SApplicationDataRef)data;

    g_print("Delete event occurred\n");    
    close(AppData->DFileDescriptor);
    return FALSE;
}

// The destroy window callback
static void MainWindowDestroy(GtkWidget *widget, gpointer data){
    gtk_main_quit ();
}

// This is a callback for the socket fd. The condition and source are ignored 
// because only one is used and condition is always for POLLIN. Return FALSE if
// the event source should be removed.
gboolean SocketCallback(GIOChannel *source, GIOCondition condition, gpointer data){
    SApplicationDataRef AppData = (SApplicationDataRef)data;
    socklen_t ClientLength;
    char Buffer[1024];
    struct sockaddr_in ClientAddress;
    int Result;
    GtkTextIter TextIterator;
    GtkTextBuffer *TextBuffer;
    
    ClientLength = sizeof(ClientAddress);
    bzero(Buffer, sizeof(Buffer));
    // Receive message from client
    Result = recvfrom(AppData->DFileDescriptor, Buffer, sizeof(Buffer), 0, (struct sockaddr *)&ClientAddress, &ClientLength);
    if(0 > Result){
        g_print("ERROR receive from client");
    }
    
    // Add message to the last line of the TextView
    TextBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(AppData->DTextView));
    gtk_text_buffer_get_end_iter(TextBuffer, &TextIterator);
    gtk_text_buffer_insert(TextBuffer, &TextIterator, Buffer, -1);
    
    // Send message to client
    Result = sendto(AppData->DFileDescriptor, AppData->DMessage, strlen(AppData->DMessage), 0, (struct sockaddr *)&ClientAddress, ClientLength);
    if(0 > Result){
        g_print("ERROR sending to client");
    }
    
    return TRUE;
}

int main(int argc, char *argv[]){
    SApplicationData MainData;
    int PortNumber;
    struct sockaddr_in ServerAddress;
    
    // This is called in all GTK applications. Arguments are parsed from the 
    // command line and are returned to the application. All GTK+ specific 
    // arguments are removed from the argc/argv list.
    gtk_init(&argc, &argv);
    
    if(2 > argc){
        fprintf(stderr,"ERROR, no port provided\n");
        exit(1);
    }
    PortNumber = atoi(argv[1]);
    if((1 > PortNumber)||(65535 < PortNumber)){
        fprintf(stderr,"Port %d is an invalid port number\n",PortNumber);
        exit(0);
    }
    // Create UDP/IP socket
    MainData.DFileDescriptor = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(0 > MainData.DFileDescriptor){
        error("ERROR opening socket");
    }
    // Setup ServerAddress data structure
    bzero((char *) &ServerAddress, sizeof(ServerAddress));
    ServerAddress.sin_family = AF_INET;
    ServerAddress.sin_addr.s_addr = INADDR_ANY;
    ServerAddress.sin_port = htons(PortNumber);
    // Binding socket to port
    if(0 > bind(MainData.DFileDescriptor, (struct sockaddr *)&ServerAddress, sizeof(ServerAddress))){ 
        error("ERROR on binding");
    }
    
    // Create an IO Channel from the file descriptor
    MainData.DIOChannel = g_io_channel_unix_new(MainData.DFileDescriptor);
    // Request notification of POLLIN on IO Channel, SocketCallback is called 
    // with &MainData as call data.
    g_io_add_watch(MainData.DIOChannel, G_IO_IN, SocketCallback, &MainData);
        
    // Initialize the reply message
    strcpy(MainData.DMessage, "I got your message.");
    
    // Create a new main window 
    MainData.DMainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    
    // When the window is given the "delete-event" signal (this is given by the 
    // window manager, usually by the "close" option, or on the titlebar), we 
    // ask it to call the delete_event () function as defined above. The data 
    // passed to the callback function is NULL and is ignored in the callback 
    // function. 
    g_signal_connect(MainData.DMainWindow, "delete-event", G_CALLBACK(MainWindowDeleteEvent), &MainData);
    
    // Here we connect the "destroy" event to a signal handler. This event 
    // occurs when we call gtk_widget_destroy() on the window, or if we return 
    // FALSE in the "delete-event" callback. 
    g_signal_connect(MainData.DMainWindow, "destroy", G_CALLBACK(MainWindowDestroy), &MainData);
    
    // Sets the border width of the window. 
    gtk_container_set_border_width(GTK_CONTAINER(MainData.DMainWindow), 10);
    
    // Creates a vertical and a horizontal box, used for packing widgets
    MainData.DVerticalBox = gtk_vbox_new(FALSE, 2);
    MainData.DHorizontalBox = gtk_hbox_new(FALSE, 2);
    
    // Add vertical box to the main window for spliting TextView from bottom
    // horizontal box
    gtk_container_add(GTK_CONTAINER(MainData.DMainWindow), MainData.DVerticalBox);
    
    // Create a ScrolledWindow for the TextView
    MainData.DScrolledWindow = gtk_scrolled_window_new(NULL, NULL);
    // Create a TextView for the received messages
    MainData.DTextView = gtk_text_view_new();
    // Set the TextView so that users can't input text
    gtk_text_view_set_editable(GTK_TEXT_VIEW(MainData.DTextView), FALSE);
    // Create a TextEntry for the reply message. Set the max length of input.
    MainData.DTextEntry = gtk_entry_new_with_max_length(64);
    
    // Creates a new button with the label "Set" to set the reply message. 
    MainData.DSetButton = gtk_button_new_with_label ("Set");
    
    // When the button receives the "clicked" signal, it will call the function 
    // SetButtonClick() passing it &MainData as its argument.  The SetButtonClick()
    // function is defined above. 
    g_signal_connect(MainData.DSetButton, "clicked", G_CALLBACK (SetButtonClick), &MainData);
    
    // This packs the TextEntry and Button into Horizontal box
    gtk_box_pack_start(GTK_BOX(MainData.DHorizontalBox), MainData.DTextEntry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(MainData.DHorizontalBox), MainData.DSetButton, FALSE, FALSE, 0);
    // This adds the TextView to the ScrolledWindow
    gtk_container_add(GTK_CONTAINER(MainData.DScrolledWindow), MainData.DTextView);
    // This packs the ScrolledWindow and Horizontal box into Vertical box
    gtk_box_pack_start(GTK_BOX(MainData.DVerticalBox), MainData.DScrolledWindow, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(MainData.DVerticalBox), MainData.DHorizontalBox, FALSE, FALSE, 0);
    
    // Show all widgets so they are displayed
    gtk_widget_show(MainData.DTextEntry);
    gtk_widget_show(MainData.DSetButton);
    gtk_widget_show(MainData.DHorizontalBox);
    gtk_widget_show(MainData.DTextView);
    gtk_widget_show(MainData.DScrolledWindow);
    gtk_widget_show(MainData.DVerticalBox);
    gtk_widget_show(MainData.DMainWindow);
    
    
    // All GTK applications must have a gtk_main(). Control ends here and waits 
    // for an event to occur (like a key press or mouse event). 
    gtk_main ();
    
    return 0;
}

