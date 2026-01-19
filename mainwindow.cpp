#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QMessageBox>
#include <QToolTip>
#include <QHelpEvent>
#include <QCursor>
#include <QDebug>
#include <algorithm>
#include <iostream>

extern "C" {
#include "rfb/rfbclient.h"
}

const int TITLE_BAR_HEIGHT = 32;
const int BUTTON_SIZE = 24;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , buttonHovered(false)
    , isDragging(false)
    , isToggled(false)
{
    ui->setupUi(this);
    
    // Make window frameless
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
    setWindowTitle("wvncc - VNC Client");
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    
    // Ensure central widget also has mouse tracking
    if (ui->centralwidget) {
        ui->centralwidget->setMouseTracking(true);
        ui->centralwidget->setAttribute(Qt::WA_Hover, true);
    }
    
    // Load button icons from Qt resources
    hideIcon.load(":/icons/resources/icons8-hide-24.png");
    eyeIcon.load(":/icons/resources/icons8-eye-24.png");
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
    m_readOnly = true;
    isToggled = true;  // Sync button state with read-only mode
    std::cout << "[INFO] Connected to " << serverIp << ":" << serverPort << std::endl;
    std::cout << "[INFO] Screen size: " << m_client->width << "x" << m_client->height << std::endl;
    
    // Resize window to match remote framebuffer plus title bar
    resize(m_client->width, m_client->height + TITLE_BAR_HEIGHT);

    // Send multiple button release events to ensure server clears any stale button state
    for (int i = 0; i < 3; i++) {
        SendPointerEvent(m_client, m_client->width / 2, m_client->height / 2, 0);
    }

    // Initialize server pointer to current cursor location (if inside window)
    syncPointerToCurrentCursor();
    
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
    
    QPainter painter(this);
    
    // Draw title bar
    titleBarRect = QRect(0, 0, width(), TITLE_BAR_HEIGHT);
    painter.fillRect(titleBarRect, QColor(31, 78, 121));
    
    // Draw window title
    painter.setPen(Qt::white);
    painter.setFont(QFont("Segoe UI", 10));
    painter.drawText(10, 0, 200, TITLE_BAR_HEIGHT, Qt::AlignVCenter, windowTitle());
    
    // Draw window control buttons (minimize, maximize, close)
    int buttonSize = TITLE_BAR_HEIGHT - 8;
    int rightOffset = width() - buttonSize - 4;
    
    // Close button
    closeButtonRect = QRect(rightOffset, 4, buttonSize, buttonSize);
    painter.fillRect(closeButtonRect, QColor(232, 17, 35));
    painter.setPen(Qt::white);
    painter.drawLine(closeButtonRect.x() + 8, closeButtonRect.y() + 8, closeButtonRect.x() + closeButtonRect.width() - 8, closeButtonRect.y() + closeButtonRect.height() - 8);
    painter.drawLine(closeButtonRect.x() + closeButtonRect.width() - 8, closeButtonRect.y() + 8, closeButtonRect.x() + 8, closeButtonRect.y() + closeButtonRect.height() - 8);
    
    // Maximize button
    maxButtonRect = QRect(rightOffset - buttonSize - 4, 4, buttonSize, buttonSize);
    painter.fillRect(maxButtonRect, QColor(200, 200, 200));
    painter.setPen(Qt::black);
    painter.drawRect(maxButtonRect.x() + 6, maxButtonRect.y() + 6, buttonSize - 10, buttonSize - 10);
    
    // Minimize button
    minButtonRect = QRect(rightOffset - (buttonSize + 4) * 2, 4, buttonSize, buttonSize);
    painter.fillRect(minButtonRect, QColor(200, 200, 200));
    painter.setPen(Qt::black);
    painter.drawLine(minButtonRect.x() + 6, minButtonRect.y() + minButtonRect.height() - 8, minButtonRect.x() + minButtonRect.width() - 6, minButtonRect.y() + minButtonRect.height() - 8);
    
    // Draw custom button to the left of minimize button
    buttonRect = QRect(minButtonRect.x() - buttonSize - 4, 4, buttonSize, buttonSize);
    
    QColor buttonColor = buttonHovered ? QColor(180, 180, 180) : QColor(200, 200, 200);
    painter.fillRect(buttonRect, buttonColor);
    painter.drawRect(buttonRect);
    
    // Draw icon centered in button
    QPixmap& currentIcon = isToggled ? eyeIcon : hideIcon;
    if (!currentIcon.isNull()) {
        int iconX = buttonRect.center().x() - currentIcon.width() / 2;
        int iconY = buttonRect.center().y() - currentIcon.height() / 2;
        painter.drawPixmap(iconX, iconY, currentIcon);
    }
    
    // Draw separator line
    painter.setPen(QColor(150, 150, 150));
    painter.drawLine(0, TITLE_BAR_HEIGHT, width(), TITLE_BAR_HEIGHT);
    
    // Draw VNC framebuffer content at 100% (no scaling)
    if (!m_framebuffer.isNull()) {
        painter.drawImage(0, TITLE_BAR_HEIGHT, m_framebuffer);
    } else {
        painter.fillRect(0, TITLE_BAR_HEIGHT, width(), height() - TITLE_BAR_HEIGHT, Qt::white);
    }
}
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (m_connected && m_client && !m_readOnly) {
        SendKeyEvent(m_client, event->key(), TRUE);
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (m_connected && m_client && !m_readOnly) {
        SendKeyEvent(m_client, event->key(), FALSE);
    }
    QMainWindow::keyReleaseEvent(event);
}
void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    // Handle window dragging
    if (isDragging && event->pos().y() < TITLE_BAR_HEIGHT) {
        move(event->globalPos() - dragPosition);
        return;
    }
    
    bool wasHovered = buttonHovered;
    buttonHovered = buttonRect.contains(event->pos());
    
    if (wasHovered != buttonHovered) {
        update();
    }
    
    if (buttonHovered) {
        setCursor(Qt::PointingHandCursor);
    } else if (event->pos().y() < TITLE_BAR_HEIGHT) {
        setCursor(Qt::ArrowCursor);
    } else if (m_connected && m_client && !m_readOnly && event->pos().y() >= TITLE_BAR_HEIGHT) {
        setCursor(Qt::ArrowCursor);
        int x = std::round(event->position().x() / static_cast<double>(width()) * m_client->width);
        int y = std::round((event->position().y() - TITLE_BAR_HEIGHT) / static_cast<double>(height() - TITLE_BAR_HEIGHT) * m_client->height);
        
        x = std::clamp(x, 0, m_client->width - 1);
        y = std::clamp(y, 0, m_client->height - 1);
        
        SendPointerEvent(m_client, x, y, m_buttonMask);
        m_pointerSyncedSinceToggle = true;
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (buttonRect.contains(event->pos())) {
        m_readOnly = !m_readOnly;
        isToggled = m_readOnly;
        if (!m_readOnly) {
            m_buttonMask = 0;
            m_pointerSyncedSinceToggle = false;  // Mark for force-sync on next click
            syncPointerToCurrentCursor();
        } else {
            // Clear any pressed state on transition back to read-only
            m_buttonMask = 0;
            syncPointerToCurrentCursor();
        }
        update();
        return;
    }
    
    if (closeButtonRect.contains(event->pos())) {
        close();
        return;
    }
    
    if (maxButtonRect.contains(event->pos())) {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
        return;
    }
    
    if (minButtonRect.contains(event->pos())) {
        showMinimized();
        return;
    }
    
    // Enable dragging from title bar
    if (event->pos().y() < TITLE_BAR_HEIGHT) {
        isDragging = true;
        dragPosition = event->globalPos() - frameGeometry().topLeft();
        return;
    }
    
    if (m_connected && m_client && !m_readOnly && event->pos().y() >= TITLE_BAR_HEIGHT) {
        int x = std::round(event->position().x() / static_cast<double>(width()) * m_client->width);
        int y = std::round((event->position().y() - TITLE_BAR_HEIGHT) / static_cast<double>(height() - TITLE_BAR_HEIGHT) * m_client->height);

        x = std::clamp(x, 0, m_client->width - 1);
        y = std::clamp(y, 0, m_client->height - 1);

        // If pointer hasn't been synced since toggling to active, force a move first
        if (!m_pointerSyncedSinceToggle) {
            SendPointerEvent(m_client, x, y, 0);
            m_pointerSyncedSinceToggle = true;
        }

        // Map Qt mouse buttons to VNC button mask
        int buttonMask = 0;
        if (event->button() == Qt::LeftButton) {
            buttonMask = 1;
        } else if (event->button() == Qt::RightButton) {
            buttonMask = 4;
        } else if (event->button() == Qt::MiddleButton) {
            buttonMask = 2;
        }
        
        m_buttonMask |= buttonMask;
        SendPointerEvent(m_client, x, y, m_buttonMask);
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    isDragging = false;
    if (m_connected && m_client && !m_readOnly && event->pos().y() >= TITLE_BAR_HEIGHT) {
        // Map Qt mouse buttons to VNC button mask
        int buttonMask = 0;
        if (event->button() == Qt::LeftButton) {
            buttonMask = 1;
        } else if (event->button() == Qt::RightButton) {
            buttonMask = 4;
        } else if (event->button() == Qt::MiddleButton) {
            buttonMask = 2;
        }
        
        m_buttonMask &= ~buttonMask;
        
        int x = std::round(event->position().x() / static_cast<double>(width()) * m_client->width);
        int y = std::round((event->position().y() - TITLE_BAR_HEIGHT) / static_cast<double>(height() - TITLE_BAR_HEIGHT) * m_client->height);
        
        x = std::clamp(x, 0, m_client->width - 1);
        y = std::clamp(y, 0, m_client->height - 1);
        
        SendPointerEvent(m_client, x, y, m_buttonMask);
    }
}

void MainWindow::syncPointerToCurrentCursor()
{
    if (!(m_connected && m_client)) {
        return;
    }

    QPoint globalPos = QCursor::pos();
    QPoint localPos = mapFromGlobal(globalPos);

    // Only sync when inside the framebuffer area
    if (localPos.y() < TITLE_BAR_HEIGHT || localPos.x() < 0 || localPos.y() < 0 || localPos.x() >= width() || localPos.y() >= height()) {
        return;
    }

    int x = std::round(localPos.x() / static_cast<double>(width()) * m_client->width);
    int y = std::round((localPos.y() - TITLE_BAR_HEIGHT) / static_cast<double>(height() - TITLE_BAR_HEIGHT) * m_client->height);

    x = std::clamp(x, 0, m_client->width - 1);
    y = std::clamp(y, 0, m_client->height - 1);

    SendPointerEvent(m_client, x, y, m_buttonMask);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Double-click on title bar to maximize/restore
    if (event->pos().y() < TITLE_BAR_HEIGHT && !buttonRect.contains(event->pos())) {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
    }
    QMainWindow::mouseDoubleClickEvent(event);
}

bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::MouseMove) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        mouseMoveEvent(mouseEvent);
        return true;
    }
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);
        if (buttonRect.contains(helpEvent->pos())) {
            QString tooltipText = isToggled ? "Read-Only" : "Active";
            QToolTip::showText(helpEvent->globalPos(), tooltipText, this);
        } else {
            QToolTip::hideText();
            event->ignore();
        }
        return true;
    }
    return QMainWindow::event(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_connected = false;
    if (m_vncThread && m_vncThread->joinable()) {
        m_vncThread->join();
    }
    QMainWindow::closeEvent(event);
}
