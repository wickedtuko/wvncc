#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QPainter>
#include <QMouseEvent>
#include <QCloseEvent>
#include <iostream>

extern "C" {
#include "rfb/rfbclient.h"
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("wvncc - VNC Client");
    setMouseTracking(true);
}

MainWindow::~MainWindow()
{
    // Ensure clean shutdown
    m_connected = false;
    if (m_vncThread && m_vncThread->joinable()) {
        m_vncThread->join();
        delete m_vncThread;
    }
    delete ui;
}

void MainWindow::framebufferUpdateCallback(rfbClient *client)
{
    MainWindow *viewer = static_cast<MainWindow*>(rfbClientGetClientData(client, nullptr));
    if (viewer) {
        viewer->handleFramebufferUpdate(client);
    }
}

char* MainWindow::getPasswordCallback(rfbClient *client)
{
    MainWindow *viewer = static_cast<MainWindow*>(rfbClientGetClientData(client, nullptr));
    if (viewer && !viewer->m_password.empty()) {
        return strdup(viewer->m_password.c_str());
    }
    return nullptr;
}

void MainWindow::handleFramebufferUpdate(rfbClient *client)
{
    // Create QImage from framebuffer (RGB16 format)
    m_framebuffer = QImage(client->frameBuffer, client->width, client->height, 
                           QImage::Format_RGB16);
    update();
}

void MainWindow::connectToServer(const std::string& serverIp, int serverPort, const std::string& password)
{
    // Store password for callback
    m_password = password;
    
    // Initialize VNC client
    m_client = rfbGetClient(8, 3, 4);
    if (!m_client) {
        std::cerr << "[ERROR] Failed to create VNC client" << std::endl;
        return;
    }
    
    // Configure client for RGB16 format (5-6-5)
    m_client->format.depth = 16;
    m_client->format.bitsPerPixel = 16;
    m_client->format.redShift = 11;
    m_client->format.greenShift = 5;
    m_client->format.blueShift = 0;
    m_client->format.redMax = 0x1f;
    m_client->format.greenMax = 0x3f;
    m_client->format.blueMax = 0x1f;
    
    // Set compression and quality
    m_client->appData.compressLevel = 9;
    m_client->appData.qualityLevel = 1;
    m_client->appData.encodingsString = "tight ultra";
    m_client->appData.useRemoteCursor = TRUE;
    
    // Set callbacks
    m_client->FinishedFrameBufferUpdate = framebufferUpdateCallback;
    m_client->GetPassword = getPasswordCallback;
    
    // Set server connection info
    m_client->serverHost = strdup(serverIp.c_str());
    m_client->serverPort = serverPort;
    
    // Store this pointer for callback
    rfbClientSetClientData(m_client, nullptr, this);
    
    // Initialize connection
    if (!rfbInitClient(m_client, 0, nullptr)) {
        std::cerr << "[ERROR] Failed to connect to VNC server" << std::endl;
        m_client = nullptr;
        return;
    }
    
    m_connected = true;
    std::cout << "[INFO] Connected to " << serverIp << ":" << serverPort << std::endl;
    std::cout << "[INFO] Screen size: " << m_client->width << "x" << m_client->height << std::endl;
    
    // Resize window to match remote framebuffer
    resize(m_client->width, m_client->height);
    
    // Start VNC message processing thread
    m_vncThread = new std::thread([this]() {
        while (m_connected && m_client) {
            int result = WaitForMessage(m_client, 500);
            if (result < 0) {
                std::cout << "[INFO] Connection lost" << std::endl;
                rfbClientCleanup(m_client);
                m_connected = false;
                break;
            }
            
            if (result > 0 && !HandleRFBServerMessage(m_client)) {
                std::cout << "[INFO] Disconnected from server" << std::endl;
                rfbClientCleanup(m_client);
                m_connected = false;
                break;
            }
        }
    });
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    event->accept();
    
    if (!m_framebuffer.isNull()) {
        QPainter painter(this);
        painter.drawImage(rect(), m_framebuffer);
    } else {
        // Draw placeholder when not connected
        QMainWindow::paintEvent(event);
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_connected && m_client) {
        int x = event->position().x() / width() * m_client->width;
        int y = event->position().y() / height() * m_client->height;
        int buttonMask = (event->buttons() & Qt::LeftButton) ? 1 : 0;
        SendPointerEvent(m_client, x, y, buttonMask);
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (m_connected && m_client) {
        int x = event->position().x() / width() * m_client->width;
        int y = event->position().y() / height() * m_client->height;
        SendPointerEvent(m_client, x, y, 1);
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_connected && m_client) {
        int x = event->position().x() / width() * m_client->width;
        int y = event->position().y() / height() * m_client->height;
        SendPointerEvent(m_client, x, y, 0);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_connected = false;
    if (m_vncThread && m_vncThread->joinable()) {
        m_vncThread->join();
    }
    QMainWindow::closeEvent(event);
}
