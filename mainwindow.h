#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QRect>
#include <QPixmap>
#include <thread>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

// Forward declare rfbClient to avoid exposing C header in header file
struct _rfbClient;
typedef struct _rfbClient rfbClient;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void connectToServer(const std::string& serverIp, int serverPort, const std::string& password = "");

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool event(QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    Ui::MainWindow *ui;
    
    // VNC client state
    bool m_connected = false;
    bool m_readOnly = true;
    bool m_pointerSyncedSinceToggle = false;
    QImage m_framebuffer;
    rfbClient *m_client = nullptr;
    std::thread *m_vncThread = nullptr;
    std::string m_password;
    std::string m_serverKey;  // serverIp:port for per-server settings
    
    // Static callbacks for rfbClient
    static void framebufferUpdateCallback(rfbClient *client);
    static char* getPasswordCallback(rfbClient *client);
    
    // Instance method for framebuffer updates
    void handleFramebufferUpdate(rfbClient *client);
    void syncPointerToCurrentCursor();
    uint32_t qtKeyToX11Keysym(int qtKey, Qt::KeyboardModifiers modifiers, const QString& text);
    QRect getScaledFramebufferRect() const;
    void showPopupMenu();
    void resetWindowTo1To1();
    
#ifdef _WIN32
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static MainWindow* s_instance;
    HHOOK m_keyboardHook = nullptr;
    bool m_winKeyPressed = false;
    bool m_winKeySentToVNC = false;
    bool m_altKeyPressed = false;
    bool m_popupMenuOpen = false;
    void installKeyboardHook();
    void uninstallKeyboardHook();
#endif
    
    // Custom title bar elements
    QRect titleBarRect;
    QRect buttonRect;
    QRect closeButtonRect;
    QRect maxButtonRect;
    QRect minButtonRect;
    bool buttonHovered;
    QPoint dragPosition;
    bool isDragging;
    int m_buttonMask = 0;
    QPixmap hideIcon;
    QPixmap eyeIcon;
    bool isToggled;
    bool isResizing = false;
    int resizeEdge = 0;
    QPoint resizeStartPos;
    QRect resizeStartGeometry;
    const int RESIZE_BORDER = 5;
};
#endif // MAINWINDOW_H
