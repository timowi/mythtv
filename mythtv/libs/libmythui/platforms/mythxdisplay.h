#ifndef MYTHXDISPLAY_X_
#define MYTHXDISPLAY_X_

// Qt
#include <QString>
#include <QMutex>
#include <QSize>
#include <QRect>

// MythTV
#include "mythuiexp.h"

// X11
#include <X11/Xlib.h>

// Std
#include <vector>

#define XLOCK(dpy, arg) { (dpy)->Lock(); arg; (dpy)->Unlock(); }

class MUI_PUBLIC MythXDisplay
{
  public:

    static MythXDisplay* OpenMythXDisplay(bool Warn = true);
    static void          SetQtX11Display (const QString &Display);
    static bool          DisplayIsRemote (void);

    MythXDisplay() = default;
    ~MythXDisplay();
    Display *GetDisplay(void)          { return m_disp;        }
    QString  GetDisplayName(void) const{ return m_displayName; }
    int      GetScreen(void) const     { return m_screenNum;   }
    void     Lock(void)                { m_lock.lock();        }
    void     Unlock(void)              { m_lock.unlock();      }
    int      GetDepth(void) const      { return m_depth;       }
    Window   GetRoot(void) const       { return m_root;        }
    bool     Open(void);
    QSize    GetDisplayDimensions(void);
    void     Sync(bool Flush = false);
    void     StartLog(void);
    bool     StopLog(void);

  private:
    bool CheckErrors(Display *Disp = nullptr);
    void CheckOrphanedErrors(void);

    static QString s_QtX11Display;

    Display      *m_disp       { nullptr };
    int           m_screenNum  { 0 };
    Screen       *m_screen     { nullptr };
    int           m_depth      { 0 };
    Window        m_root       { 0 };
    QMutex        m_lock       { QMutex::Recursive };
    QString       m_displayName{ };
};

// These X11 defines conflict with the QT key event enum values
#undef KeyPress
#undef KeyRelease

#endif // MYTHXDISPLAY_X_
