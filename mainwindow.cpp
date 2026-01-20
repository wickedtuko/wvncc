#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QMessageBox>
#include <QMenu>
#include <QToolTip>
#include <QHelpEvent>
#include <QCursor>
#include <QDebug>
#include <QSettings>
#include <QFocusEvent>
#include <QScreen>
#include <QApplication>
#include <QClipboard>
#include <algorithm>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
#include "rfb/rfbclient.h"
}

const int TITLE_BAR_HEIGHT = 32;
const int BUTTON_SIZE = 24;

#ifdef _WIN32
MainWindow* MainWindow::s_instance = nullptr;
#endif

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
    
    // Connect clipboard signal
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::onClipboardChanged);
    connect(this, &MainWindow::clipboardReceived, this, &MainWindow::updateClipboardFromServer);
    
    // Note: Window position, size, and read-only state are restored per-server in connectToServer()
}

MainWindow::~MainWindow()
{
#ifdef _WIN32
    uninstallKeyboardHook();
#endif
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

void MainWindow::gotXCutTextCallback(rfbClient *client, const char *text, int textlen)
{
    MainWindow *viewer = static_cast<MainWindow*>(rfbClientGetClientData(client, nullptr));
    if (viewer) {
        viewer->handleServerClipboard(text, textlen);
    }
}

void MainWindow::handleFramebufferUpdate(rfbClient *client)
{
    // Create QImage from framebuffer (RGB32 format)
    m_framebuffer = QImage(client->frameBuffer, client->width, client->height, 
                           QImage::Format_RGB32);
    update();
}

void MainWindow::handleServerClipboard(const char *text, int textlen)
{
    if (text && textlen > 0) {
        QString clipboardText = QString::fromUtf8(text, textlen);
        emit clipboardReceived(clipboardText);
    }
}

void MainWindow::updateClipboardFromServer(const QString &text)
{
    m_updatingClipboard = true;
    QApplication::clipboard()->setText(text);
    m_updatingClipboard = false;
}

void MainWindow::connectToServer(const std::string& serverIp, int serverPort, const std::string& password)
{
    // Store password for callback
    m_password = password;
    
    // Create server key for per-server settings
    m_serverKey = serverIp + ":" + std::to_string(serverPort);
    
    // Initialize VNC client
    m_client = rfbGetClient(8, 3, 4);
    if (!m_client) {
        std::cerr << "[ERROR] Failed to create VNC client" << std::endl;
        return;
    }
    
    // Configure client for RGB32 format (8-8-8 with padding)
    // This provides true color (16 million colors) for better image quality
    m_client->format.depth = 24;
    m_client->format.bitsPerPixel = 32;
    m_client->format.redShift = 16;
    m_client->format.greenShift = 8;
    m_client->format.blueShift = 0;
    m_client->format.redMax = 0xff;
    m_client->format.greenMax = 0xff;
    m_client->format.blueMax = 0xff;
    
    // Set compression and quality
    m_client->appData.compressLevel = 0;
    m_client->appData.qualityLevel = 9;
    m_client->appData.encodingsString = "hextile raw";
    m_client->appData.useRemoteCursor = TRUE;
    
    // Set callbacks
    m_client->FinishedFrameBufferUpdate = framebufferUpdateCallback;
    m_client->GetPassword = getPasswordCallback;
    m_client->GotXCutText = gotXCutTextCallback;
    
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
    
    // Update window title with desktop name if available
    if (m_client->desktopName) {
        setWindowTitle(QString::fromUtf8(m_client->desktopName));
    }
    
    // Calculate window size to fit available display while respecting VNC aspect ratio
    QScreen* screen = QApplication::primaryScreen();
    QRect availableGeometry = screen->availableGeometry();
    
    int vncWidth = m_client->width;
    int vncHeight = m_client->height;
    int targetWidth = vncWidth;
    int targetHeight = vncHeight + TITLE_BAR_HEIGHT;
    
    QSettings settings("wvncc", "wvncc");
    QString serverKey = QString::fromStdString(m_serverKey);
    
    // Restore per-server read-only mode
    if (settings.contains(serverKey + "/readOnlyMode")) {
        m_readOnly = settings.value(serverKey + "/readOnlyMode").toBool();
        isToggled = m_readOnly;
        update();
    }
    
    // If VNC fits at 1:1, use 1:1 scale (don't resize if saved geometry exists to preserve position)
    if (targetWidth <= availableGeometry.width() && targetHeight <= availableGeometry.height()) {
        if (settings.contains(serverKey + "/windowSize")) {
            // Restore saved position and size for this server
            QPoint pos = settings.value(serverKey + "/windowPosition").toPoint();
            QSize size = settings.value(serverKey + "/windowSize").toSize();
            move(pos);
            resize(size);
            std::cout << "[INFO] Using saved window geometry for " << m_serverKey << std::endl;
        } else {
            // First run for this server: set 1:1 size
            resize(targetWidth, targetHeight);
            std::cout << "[INFO] Window sized 1:1 to " << targetWidth << "x" << targetHeight << std::endl;
        }
    } else {
        // VNC too large for 1:1
        if (settings.contains(serverKey + "/windowSize")) {
            // Use saved geometry (user's preferred scaled size)
            QPoint pos = settings.value(serverKey + "/windowPosition").toPoint();
            QSize size = settings.value(serverKey + "/windowSize").toSize();
            move(pos);
            resize(size);
            std::cout << "[INFO] Using saved window geometry for " << m_serverKey << std::endl;
        } else {
            // First run - scale down to fit
            double scaleWidth = static_cast<double>(availableGeometry.width()) / targetWidth;
            double scaleHeight = static_cast<double>(availableGeometry.height()) / targetHeight;
            double scale = std::min(scaleWidth, scaleHeight);
            
            targetWidth = static_cast<int>(targetWidth * scale);
            targetHeight = static_cast<int>(targetHeight * scale);
            
            resize(targetWidth, targetHeight);
            std::cout << "[INFO] Scaled window to " << targetWidth << "x" << targetHeight 
                      << " (scale factor: " << scale << ")" << std::endl;
        }
    }

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
    QPixmap& currentIcon = isToggled ? hideIcon : eyeIcon;
    if (!currentIcon.isNull()) {
        int iconX = buttonRect.center().x() - currentIcon.width() / 2;
        int iconY = buttonRect.center().y() - currentIcon.height() / 2;
        painter.drawPixmap(iconX, iconY, currentIcon);
    }
    
    // Draw separator line
    painter.setPen(QColor(150, 150, 150));
    painter.drawLine(0, TITLE_BAR_HEIGHT, width(), TITLE_BAR_HEIGHT);
    
    // Draw VNC framebuffer content scaled to fit window while maintaining aspect ratio
    if (!m_framebuffer.isNull()) {
        // Enable high-quality rendering
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.setRenderHint(QPainter::Antialiasing, true);
        
        QRect targetRect(0, TITLE_BAR_HEIGHT, width(), height() - TITLE_BAR_HEIGHT);
        QSize scaledSize = m_framebuffer.size().scaled(targetRect.size(), Qt::KeepAspectRatio);
        
        // Center the scaled image
        int x = (targetRect.width() - scaledSize.width()) / 2;
        int y = targetRect.y() + (targetRect.height() - scaledSize.height()) / 2;
        
        QRect destRect(x, y, scaledSize.width(), scaledSize.height());
        painter.drawImage(destRect, m_framebuffer);
        
        // Fill letterbox/pillarbox areas
        if (x > 0) {
            painter.fillRect(0, TITLE_BAR_HEIGHT, x, height() - TITLE_BAR_HEIGHT, Qt::black);
            painter.fillRect(x + scaledSize.width(), TITLE_BAR_HEIGHT, width() - (x + scaledSize.width()), height() - TITLE_BAR_HEIGHT, Qt::black);
        }
        if (y > TITLE_BAR_HEIGHT) {
            painter.fillRect(0, TITLE_BAR_HEIGHT, width(), y - TITLE_BAR_HEIGHT, Qt::black);
            painter.fillRect(0, y + scaledSize.height(), width(), height() - (y + scaledSize.height()), Qt::black);
        }
    } else {
        painter.fillRect(0, TITLE_BAR_HEIGHT, width(), height() - TITLE_BAR_HEIGHT, Qt::white);
    }
}

uint32_t MainWindow::qtKeyToX11Keysym(int qtKey, Qt::KeyboardModifiers modifiers, const QString& text)
{
    // Handle printable characters from text when available
    if (!text.isEmpty() && text[0].isPrint()) {
        return text[0].unicode();
    }
    
    // Map Qt special keys to X11 keysyms
    switch (qtKey) {
        case Qt::Key_Backspace: return XK_BackSpace;
        case Qt::Key_Tab: return XK_Tab;
        case Qt::Key_Return:
        case Qt::Key_Enter: return XK_Return;
        case Qt::Key_Escape: return XK_Escape;
        case Qt::Key_Delete: return XK_Delete;
        case Qt::Key_Home: return XK_Home;
        case Qt::Key_End: return XK_End;
        case Qt::Key_PageUp: return XK_Page_Up;
        case Qt::Key_PageDown: return XK_Page_Down;
        case Qt::Key_Left: return XK_Left;
        case Qt::Key_Up: return XK_Up;
        case Qt::Key_Right: return XK_Right;
        case Qt::Key_Down: return XK_Down;
        case Qt::Key_Insert: return XK_Insert;
        case Qt::Key_Shift: return XK_Shift_L;
        case Qt::Key_Control: return XK_Control_L;
        case Qt::Key_Alt: return XK_Alt_L;
        case Qt::Key_Meta: return XK_Super_L;
        case Qt::Key_AltGr: return XK_ISO_Level3_Shift;
        case Qt::Key_CapsLock: return XK_Caps_Lock;
        case Qt::Key_NumLock: return XK_Num_Lock;
        case Qt::Key_ScrollLock: return XK_Scroll_Lock;
        case Qt::Key_F1: return XK_F1;
        case Qt::Key_F2: return XK_F2;
        case Qt::Key_F3: return XK_F3;
        case Qt::Key_F4: return XK_F4;
        case Qt::Key_F5: return XK_F5;
        case Qt::Key_F6: return XK_F6;
        case Qt::Key_F7: return XK_F7;
        case Qt::Key_F8: return XK_F8;
        case Qt::Key_F9: return XK_F9;
        case Qt::Key_F10: return XK_F10;
        case Qt::Key_F11: return XK_F11;
        case Qt::Key_F12: return XK_F12;
        default:
            // For unhandled keys, try to return the Qt key value directly
            return qtKey;
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
#ifdef _WIN32
    // Check for Alt+F8 combination to show popup menu
    if (event->key() == Qt::Key_F8 && (event->modifiers() & Qt::AltModifier)) {
        showPopupMenu();
        event->accept();
        return;
    }
#endif
    
    if (m_connected && m_client && !m_readOnly) {
        uint32_t keysym = qtKeyToX11Keysym(event->key(), event->modifiers(), event->text());
        SendKeyEvent(m_client, keysym, TRUE);
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (m_connected && m_client && !m_readOnly) {
        uint32_t keysym = qtKeyToX11Keysym(event->key(), event->modifiers(), event->text());
        SendKeyEvent(m_client, keysym, FALSE);
    }
    QMainWindow::keyReleaseEvent(event);
}

void MainWindow::focusInEvent(QFocusEvent *event)
{
#ifdef _WIN32
    s_instance = this;
    if (m_connected) {
        installKeyboardHook();
    }
#endif
    QMainWindow::focusInEvent(event);
}

void MainWindow::focusOutEvent(QFocusEvent *event)
{
#ifdef _WIN32
    uninstallKeyboardHook();
#endif
    QMainWindow::focusOutEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    // Handle window resizing
    if (isResizing && event->buttons() & Qt::LeftButton) {
        QPoint delta = event->globalPosition().toPoint() - resizeStartPos;
        QRect newGeometry = resizeStartGeometry;
        
        if (resizeEdge & 1) {  // Left edge
            newGeometry.setLeft(resizeStartGeometry.left() + delta.x());
        }
        if (resizeEdge & 2) {  // Right edge
            newGeometry.setRight(resizeStartGeometry.right() + delta.x());
        }
        if (resizeEdge & 4) {  // Top edge
            newGeometry.setTop(resizeStartGeometry.top() + delta.y());
        }
        if (resizeEdge & 8) {  // Bottom edge
            newGeometry.setBottom(resizeStartGeometry.bottom() + delta.y());
        }
        
        // Enforce minimum size
        if (newGeometry.width() >= 200 && newGeometry.height() >= 200) {
            setGeometry(newGeometry);
        }
        return;
    }
    
    // Handle window dragging
    if (isDragging && event->pos().y() < TITLE_BAR_HEIGHT) {
        move(event->globalPosition().toPoint() - dragPosition);
        return;
    }
    
    // Check for resize edge detection and set cursor
    int posX = event->pos().x();
    int posY = event->pos().y();
    int winWidth = width();
    int winHeight = height();
    
    bool onLeft = posX < RESIZE_BORDER;
    bool onRight = posX > winWidth - RESIZE_BORDER;
    bool onTop = posY < RESIZE_BORDER;
    bool onBottom = posY > winHeight - RESIZE_BORDER;
    
    if ((onLeft || onRight) && (onTop || onBottom)) {
        // Corner resize
        if ((onLeft && onTop) || (onRight && onBottom)) {
            setCursor(Qt::SizeFDiagCursor);
        } else {
            setCursor(Qt::SizeBDiagCursor);
        }
    } else if (onLeft || onRight) {
        setCursor(Qt::SizeHorCursor);
    } else if (onTop || onBottom) {
        setCursor(Qt::SizeVerCursor);
    } else if (buttonHovered) {
        setCursor(Qt::PointingHandCursor);
    } else if (event->pos().y() < TITLE_BAR_HEIGHT) {
        setCursor(Qt::ArrowCursor);
    }
    
    bool wasHovered = buttonHovered;
    buttonHovered = buttonRect.contains(event->pos());
    
    if (wasHovered != buttonHovered) {
        update();
    }
    
    if (buttonHovered) {
        setCursor(Qt::PointingHandCursor);
    } else if (event->pos().y() < TITLE_BAR_HEIGHT && !onLeft && !onRight && !onTop && !onBottom) {
        setCursor(Qt::ArrowCursor);
    } else if (m_connected && m_client && !m_readOnly && event->pos().y() >= TITLE_BAR_HEIGHT && !onLeft && !onRight && !onTop && !onBottom) {
        setCursor(Qt::ArrowCursor);
        
        QRect scaledRect = getScaledFramebufferRect();
        if (scaledRect.contains(event->pos())) {
            int x = std::round((event->position().x() - scaledRect.x()) / static_cast<double>(scaledRect.width()) * m_client->width);
            int y = std::round((event->position().y() - scaledRect.y()) / static_cast<double>(scaledRect.height()) * m_client->height);
            
            x = std::clamp(x, 0, m_client->width - 1);
            y = std::clamp(y, 0, m_client->height - 1);
            
            SendPointerEvent(m_client, x, y, m_buttonMask);
            m_pointerSyncedSinceToggle = true;
        }
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
    
    // Check for resize edge and start resize operation
    int posX = event->pos().x();
    int posY = event->pos().y();
    int winWidth = width();
    int winHeight = height();
    
    bool onLeft = posX < RESIZE_BORDER;
    bool onRight = posX > winWidth - RESIZE_BORDER;
    bool onTop = posY < RESIZE_BORDER;
    bool onBottom = posY > winHeight - RESIZE_BORDER;
    
    if ((onLeft || onRight || onTop || onBottom) && event->button() == Qt::LeftButton) {
        isResizing = true;
        resizeStartPos = event->globalPosition().toPoint();
        resizeStartGeometry = frameGeometry();
        resizeEdge = 0;
        
        if (onLeft) resizeEdge |= 1;       // Left
        if (onRight) resizeEdge |= 2;      // Right
        if (onTop) resizeEdge |= 4;        // Top
        if (onBottom) resizeEdge |= 8;     // Bottom
        return;
    }
    
    // Enable dragging from title bar
    if (event->pos().y() < TITLE_BAR_HEIGHT) {
        if (event->button() == Qt::RightButton) {
            showPopupMenu();
            return;
        }
        isDragging = true;
        dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        return;
    }
    
    if (m_connected && m_client && !m_readOnly && event->pos().y() >= TITLE_BAR_HEIGHT) {
        QRect scaledRect = getScaledFramebufferRect();
        if (!scaledRect.contains(event->pos())) {
            return;
        }
        
        int x = std::round((event->position().x() - scaledRect.x()) / static_cast<double>(scaledRect.width()) * m_client->width);
        int y = std::round((event->position().y() - scaledRect.y()) / static_cast<double>(scaledRect.height()) * m_client->height);

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
    isResizing = false;
    
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
        
        QRect scaledRect = getScaledFramebufferRect();
        int x = std::round((event->position().x() - scaledRect.x()) / static_cast<double>(scaledRect.width()) * m_client->width);
        int y = std::round((event->position().y() - scaledRect.y()) / static_cast<double>(scaledRect.height()) * m_client->height);
        
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

    QRect scaledRect = getScaledFramebufferRect();
    if (!scaledRect.contains(localPos)) {
        return;
    }

    int x = std::round((localPos.x() - scaledRect.x()) / static_cast<double>(scaledRect.width()) * m_client->width);
    int y = std::round((localPos.y() - scaledRect.y()) / static_cast<double>(scaledRect.height()) * m_client->height);

    x = std::clamp(x, 0, m_client->width - 1);
    y = std::clamp(y, 0, m_client->height - 1);

    SendPointerEvent(m_client, x, y, m_buttonMask);
}

QRect MainWindow::getScaledFramebufferRect() const
{
    if (!m_framebuffer.isNull()) {
        QRect targetRect(0, TITLE_BAR_HEIGHT, width(), height() - TITLE_BAR_HEIGHT);
        QSize scaledSize = m_framebuffer.size().scaled(targetRect.size(), Qt::KeepAspectRatio);
        
        int x = (targetRect.width() - scaledSize.width()) / 2;
        int y = targetRect.y() + (targetRect.height() - scaledSize.height()) / 2;
        
        return QRect(x, y, scaledSize.width(), scaledSize.height());
    }
    return QRect();
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
            QString tooltipText = isToggled ? "Read-Only, click to activate" : "Active, click to make it read-only";
            QToolTip::showText(helpEvent->globalPos(), tooltipText, this);
        } else {
            QToolTip::hideText();
            event->ignore();
        }
        return true;
    }
    return QMainWindow::event(event);
}

void MainWindow::onClipboardChanged()
{
    // Don't send clipboard updates if we're updating it from the server
    if (m_updatingClipboard || !m_connected || !m_client || m_readOnly) {
        return;
    }
    
    QString clipboardText = QApplication::clipboard()->text();
    if (!clipboardText.isEmpty()) {
        QByteArray utf8Text = clipboardText.toUtf8();
        SendClientCutText(m_client, utf8Text.data(), utf8Text.length());
    }
}

void MainWindow::showPopupMenu()
{
    QMenu menu(this);
    
    // Minimize action
    QAction* minimizeAction = menu.addAction("Mi&nimize");
    connect(minimizeAction, &QAction::triggered, this, [this]() {
#ifdef _WIN32
        ShowWindow((HWND)winId(), SW_MINIMIZE);
#else
        showMinimized();
#endif
    });
    
    // Maximize/Restore action
    QAction* maximizeAction = menu.addAction(isMaximized() ? "&Restore" : "Ma&ximize");
    connect(maximizeAction, &QAction::triggered, this, [this]() {
#ifdef _WIN32
        if (isMaximized()) {
            ShowWindow((HWND)winId(), SW_NORMAL);
        } else {
            ShowWindow((HWND)winId(), SW_MAXIMIZE);
        }
#else
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
#endif
    });
    
    menu.addSeparator();
    
    // Reset to 1:1 scale action
    QAction* resetAction = menu.addAction("Reset to &1:1 Scale");
    connect(resetAction, &QAction::triggered, this, &MainWindow::resetWindowTo1To1);
    
    // Toggle read-only/active action
    QAction* toggleAction = menu.addAction(m_readOnly ? "&Switch to Active Mode" : "&Switch to Read-Only Mode");
    connect(toggleAction, &QAction::triggered, this, [this]() {
        m_readOnly = !m_readOnly;
        isToggled = m_readOnly;
        if (!m_readOnly) {
            m_buttonMask = 0;
            m_pointerSyncedSinceToggle = false;
            syncPointerToCurrentCursor();
        } else {
            m_buttonMask = 0;
            syncPointerToCurrentCursor();
        }
        update();
    });
    
    menu.addSeparator();
    
    // Close action
    QAction* closeAction = menu.addAction("&Close Application");
    connect(closeAction, &QAction::triggered, this, &MainWindow::close);
    
    // Show menu at cursor position
    menu.exec(QCursor::pos());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Save per-server window position, size, and read-only state to settings
    QSettings settings("wvncc", "wvncc");
    QString serverKey = QString::fromStdString(m_serverKey);
    settings.setValue(serverKey + "/windowPosition", pos());
    settings.setValue(serverKey + "/windowSize", size());
    settings.setValue(serverKey + "/readOnlyMode", m_readOnly);
    
#ifdef _WIN32
    // Reset Win key state and uninstall hook before closing
    m_winKeyPressed = false;
    m_winKeySentToVNC = false;
    uninstallKeyboardHook();
#endif
    
    m_connected = false;
    if (m_vncThread && m_vncThread->joinable()) {
        m_vncThread->join();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::resetWindowTo1To1()
{
    if (!m_client || m_framebuffer.isNull()) {
        return;
    }
    
    int targetWidth = m_client->width;
    int targetHeight = m_client->height + TITLE_BAR_HEIGHT;
    
    // Get available screen geometry
    QScreen* screen = QApplication::primaryScreen();
    QRect availableGeometry = screen->availableGeometry();
    
    // If it fits on screen at 1:1, use that size
    if (targetWidth <= availableGeometry.width() && targetHeight <= availableGeometry.height()) {
        resize(targetWidth, targetHeight);
    } else {
        // Otherwise scale down proportionally to fit
        double scaleWidth = static_cast<double>(availableGeometry.width()) / targetWidth;
        double scaleHeight = static_cast<double>(availableGeometry.height()) / targetHeight;
        double scale = std::min(scaleWidth, scaleHeight);
        
        targetWidth = static_cast<int>(targetWidth * scale);
        targetHeight = static_cast<int>(targetHeight * scale);
        
        resize(targetWidth, targetHeight);
    }
}

#ifdef _WIN32
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (eventType == "windows.nativeEvent") {
        MSG* msg = static_cast<MSG*>(message);
        
        if (msg->message == WM_NCHITTEST) {
            QPoint globalPos = QCursor::pos();
            QPoint localPos = mapFromGlobal(globalPos);
            
            int posX = localPos.x();
            int posY = localPos.y();
            int winWidth = width();
            int winHeight = height();
            
            bool onLeft = posX < RESIZE_BORDER;
            bool onRight = posX > winWidth - RESIZE_BORDER;
            bool onTop = posY < RESIZE_BORDER;
            bool onBottom = posY > winHeight - RESIZE_BORDER;
            
            // Return appropriate NCHITTEST value for resize areas
            if (onLeft && onTop) {
                *result = HTTOPLEFT;
                return true;
            }
            if (onRight && onTop) {
                *result = HTTOPRIGHT;
                return true;
            }
            if (onLeft && onBottom) {
                *result = HTBOTTOMLEFT;
                return true;
            }
            if (onRight && onBottom) {
                *result = HTBOTTOMRIGHT;
                return true;
            }
            if (onLeft) {
                *result = HTLEFT;
                return true;
            }
            if (onRight) {
                *result = HTRIGHT;
                return true;
            }
            if (onTop) {
                *result = HTTOP;
                return true;
            }
            if (onBottom) {
                *result = HTBOTTOM;
                return true;
            }
            
            // Check if in title bar for dragging
            if (posY < TITLE_BAR_HEIGHT) {
                *result = HTCAPTION;
                return true;
            }
        }
    }
    
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

#ifdef _WIN32
LRESULT CALLBACK MainWindow::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && s_instance && s_instance->m_connected) {
        KBDLLHOOKSTRUCT* pKeyboard = (KBDLLHOOKSTRUCT*)lParam;
        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        
        // Intercept Windows keys (VK_LWIN and VK_RWIN)
        if (pKeyboard->vkCode == VK_LWIN || pKeyboard->vkCode == VK_RWIN) {
            s_instance->m_winKeyPressed = isKeyDown;
            
            // Only send to VNC server if in active mode
            if (!s_instance->m_readOnly && s_instance->m_client) {
                uint32_t keysym = (pKeyboard->vkCode == VK_LWIN) ? XK_Super_L : XK_Super_R;
                SendKeyEvent(s_instance->m_client, keysym, isKeyDown ? TRUE : FALSE);
                if (isKeyDown) {
                    s_instance->m_winKeySentToVNC = true;
                }
                // Block the key from reaching the OS in active mode
                return 1;
            }
            // In read-only mode, let the Windows key through so OS sees press/release correctly
            return 0;
        }
        
        // Intercept Alt key (VK_LMENU and VK_RMENU)
        if (pKeyboard->vkCode == VK_LMENU || pKeyboard->vkCode == VK_RMENU) {
            s_instance->m_altKeyPressed = isKeyDown;
            
            // Only block and send Alt if in active mode, but allow it to reach Qt for Alt+F8
            if (!s_instance->m_readOnly && s_instance->m_client) {
                uint32_t keysym = (pKeyboard->vkCode == VK_LMENU) ? XK_Alt_L : XK_Alt_R;
                SendKeyEvent(s_instance->m_client, keysym, isKeyDown ? TRUE : FALSE);
                // Block Alt from reaching the OS in active mode (but let Qt see it for Alt+F8)
                // We return 0 here to let it reach Qt, then Qt will handle it
                return 0;
            }
        }
        
        // Intercept Tab when Alt is pressed to handle Alt+Tab
        if (pKeyboard->vkCode == VK_TAB && s_instance->m_altKeyPressed) {
            // Only block Alt+Tab if in active mode
            if (!s_instance->m_readOnly && s_instance->m_client) {
                // Send Tab to VNC server
                SendKeyEvent(s_instance->m_client, XK_Tab, isKeyDown ? TRUE : FALSE);
                // Block Tab from reaching the OS
                return 1;
            }
        }
    }
    
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void MainWindow::installKeyboardHook()
{
    if (!m_keyboardHook) {
        m_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    }
}

void MainWindow::uninstallKeyboardHook()
{
    if (m_keyboardHook) {
        UnhookWindowsHookEx(m_keyboardHook);
        m_keyboardHook = nullptr;
    }
}
#endif
