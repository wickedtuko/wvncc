#include "mainwindow.h"

#include <QApplication>
#include <iostream>

int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <server_ip> <port> [password]" << std::endl;
        std::cout << "Example: " << argv[0] << " 192.168.1.100 5900 mypassword" << std::endl;
        return 1;
    }

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    
    // Connect to VNC server
    std::string serverIp = argv[1];
    int serverPort = std::atoi(argv[2]);
    std::string password = (argc > 3) ? argv[3] : "";
    w.connectToServer(serverIp, serverPort, password);
    
    return a.exec();
}
