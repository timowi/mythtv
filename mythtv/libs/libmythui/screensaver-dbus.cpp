#include "screensaver-dbus.h"

#include <cstdint>
#include <string>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QString>

#include "mythlogging.h"

#define LOC                 QString("ScreenSaverDBus: ")

const std::string kApp         = "MythTV";
const std::string kReason      = "Watching TV";
const std::string kDbusInhibit = "Inhibit";

#define NUM_DBUS_METHODS 4
// Thanks to vlc for the set of dbus services to use.
const std::array<const QString,NUM_DBUS_METHODS> kDbusService {
    "org.freedesktop.ScreenSaver", /**< KDE >= 4 and GNOME >= 3.10 */
    "org.freedesktop.PowerManagement.Inhibit", /**< KDE and GNOME <= 2.26 */
    "org.mate.SessionManager", /**< >= 1.0 */
    "org.gnome.SessionManager", /**< GNOME 2.26..3.4 */
};

const std::array<const QString,NUM_DBUS_METHODS> kDbusPath {
    "/ScreenSaver",
    "/org/freedesktop/PowerManagement",
    "/org/mate/SessionManager",
    "/org/gnome/SessionManager",
};

// Service name is also the interface name in all cases

const std::array<const QString,NUM_DBUS_METHODS> kDbusUnInhibit {
    "UnInhibit",
    "UnInhibit",
    "Uninhibit",
    "Uninhibit",
};

class ScreenSaverDBusPrivate
{
    friend class    ScreenSaverDBus;

  public:
    ScreenSaverDBusPrivate(const QString &dbusService, const QString& dbusPath,
                           const QString &dbusInterface, QDBusConnection *bus) :
        m_bus(bus),
        m_interface(new QDBusInterface(dbusService, dbusPath , dbusInterface, *m_bus)),
        m_serviceUsed(dbusService)
    {
        if (!m_interface->isValid())
        {
            LOG(VB_GENERAL, LOG_DEBUG, LOC + "Could not connect to dbus: " +
                m_interface->lastError().message());
        }
        else
        {
            LOG(VB_GENERAL, LOG_INFO, LOC + "Created for DBus service: " + dbusService);
        }
    }
    ~ScreenSaverDBusPrivate()
    {
        delete m_interface;
    }
    void Inhibit(void)
    {
        if (m_interface->isValid())
        {
            // method uint org.freedesktop.ScreenSaver.Inhibit(QString application_name, QString reason_for_inhibit)
            QDBusMessage msg = m_interface->call(QDBus::Block,
                                                 kDbusInhibit.c_str(),
                                                 kApp.c_str(), kReason.c_str());
            if (msg.type() == QDBusMessage::ReplyMessage)
            {
                QList<QVariant> replylist = msg.arguments();
                QVariant reply = replylist.first();
                m_cookie = reply.toUInt();
                m_inhibited = true;
                LOG(VB_GENERAL, LOG_INFO, LOC +
                    QString("Successfully inhibited screensaver via %1. cookie %2. nom nom")
                    .arg(m_serviceUsed).arg(m_cookie));
            }
            else // msg.type() == QDBusMessage::ErrorMessage
            {
                LOG(VB_GENERAL, LOG_WARNING, LOC + "Failed to disable screensaver: " +
                    msg.errorMessage());
            }
        }
    }
    void UnInhibit(void)
    {
        if (m_interface->isValid())
        {
            // Don't block waiting for the reply, there isn't one
            // method void org.freedesktop.ScreenSaver.UnInhibit(uint cookie) (or equivalent)
            if (m_cookie != 0) {
                m_interface->call(QDBus::NoBlock, m_unInhibit , m_cookie);
                m_cookie = 0;
                m_inhibited = false;
                LOG(VB_GENERAL, LOG_INFO, LOC + QString("Screensaver uninhibited via %1")
                    .arg(m_serviceUsed));
            }
        }
    }
    void SetUnInhibit(const QString &method) { m_unInhibit = method; }

  protected:
    bool            m_inhibited  {false};
    uint32_t        m_cookie     {0};
    QDBusConnection *m_bus       {nullptr};
    QDBusInterface  *m_interface {nullptr};
  private:
    // Disable copying and assignment
    Q_DISABLE_COPY(ScreenSaverDBusPrivate)
    QString         m_unInhibit;
    QString         m_serviceUsed;
};

ScreenSaverDBus::ScreenSaverDBus() :
    m_bus(QDBusConnection::sessionBus())
{
    // service, path, interface, bus - note that interface = service, hence it is used twice
    for (uint i=0; i < NUM_DBUS_METHODS; i++) {
        auto *ssdbp =
            new ScreenSaverDBusPrivate(kDbusService[i], kDbusPath[i], kDbusService[i], &m_bus);
        ssdbp->SetUnInhibit(kDbusUnInhibit[i]);
        m_dbusPrivateInterfaces.push_back(ssdbp);
    }
}

ScreenSaverDBus::~ScreenSaverDBus()
{
    ScreenSaverDBus::Restore();
    while (!m_dbusPrivateInterfaces.isEmpty()) {
        ScreenSaverDBusPrivate *ssdbp = m_dbusPrivateInterfaces.takeLast();
        delete ssdbp;
    }
}

void ScreenSaverDBus::Disable(void)
{
    QList<ScreenSaverDBusPrivate *>::iterator i;
    for (i = m_dbusPrivateInterfaces.begin(); i != m_dbusPrivateInterfaces.end(); ++i) {
        (*i)->Inhibit();
    }
}

void ScreenSaverDBus::Restore(void)
{
    QList<ScreenSaverDBusPrivate *>::iterator i;
    for (i = m_dbusPrivateInterfaces.begin(); i != m_dbusPrivateInterfaces.end(); ++i) {
        (*i)->UnInhibit();
    }
}

void ScreenSaverDBus::Reset(void)
{
    Restore();
}

bool ScreenSaverDBus::Asleep(void)
{
    QList<ScreenSaverDBusPrivate *>::iterator i;
    for (i = m_dbusPrivateInterfaces.begin(); i != m_dbusPrivateInterfaces.end(); ++i) {
        if((*i)->m_inhibited) {
            return true;
        }
    }
    return false;
}
