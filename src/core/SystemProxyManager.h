// src/core/SystemProxyManager.h
#pragma once

#include <QCoreApplication>
#include <QHostAddress>
#include <QHostInfo>
#include <QMap>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <functional>
#include <memory>

class SystemProxyManager : public QNetworkProxyFactory {
public:
    using LogCallback = std::function<void(const QString& category,
                                           const QString& summary,
                                           const QString& payload)>;

    enum class ProxyProtocol {
        Direct,
        Http,
        Socks5,
        Socks5h,
        Socks4,
        Socks4a,
        SocksAuto // Системный "socks=host:port": пробует SOCKS5, при отказе прозрачно переходит на SOCKS4a/SOCKS4
    };

    struct ProxyDescriptor {
        ProxyProtocol protocol = ProxyProtocol::Direct;
        QString host;
        quint16 port = 0;
        QString user;
        QString password;
        QString source;

        bool isValid() const {
            return protocol != ProxyProtocol::Direct && !host.isEmpty() && port > 0;
        }

        QString key() const {
            return QString("%1:%2:%3:%4")
            .arg(static_cast<int>(protocol))
                .arg(host.toLower())
                .arg(port)
                .arg(user);
        }
    };

    static void install() {
        QNetworkProxyFactory::setApplicationProxyFactory(new SystemProxyManager());
    }

    static void setLogger(LogCallback cb) {
        s_logger = std::move(cb);
    }

    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query = QNetworkProxyQuery()) override {
        thread_local bool inQuery = false;
        if (inQuery) {
            return {QNetworkProxy(QNetworkProxy::NoProxy)};
        }
        inQuery = true;
        auto guard = qScopeGuard([&]() { inQuery = false; });

        const QUrl url = query.url();
        QString targetHost = query.peerHostName().trimmed();
        if (targetHost.isEmpty()) {
            targetHost = url.host().trimmed();
        }
        int targetPort = query.peerPort();
        if (targetPort <= 0) {
            targetPort = url.port(url.scheme().compare("https", Qt::CaseInsensitive) == 0 ? 443 : 80);
        }

        // 1. Локальные адреса (Ollama, KoboldCpp, llama-server) всегда идут напрямую
        if (isLoopbackOrLocalHost(targetHost)) {
            return {QNetworkProxy(QNetworkProxy::NoProxy)};
        }

        const QString scheme = query.protocolTag().isEmpty()
                                   ? url.scheme().toLower()
                                   : query.protocolTag().toLower();

        // 2. Проверка списка исключений NO_PROXY / Windows ProxyOverride
        if (isHostBypassed(targetHost, targetPort)) {
            return {QNetworkProxy(QNetworkProxy::NoProxy)};
        }

        // 3. Определение системного прокси (Env -> Windows Registry -> OS SystemConfig)
        ProxyDescriptor desc = resolveSystemProxyDescriptor(query, scheme, targetHost, targetPort);
        if (!desc.isValid()) {
            return {QNetworkProxy(QNetworkProxy::NoProxy)};
        }

        // Если для SocksAuto уже было определено, что сервер поддерживает чистый SOCKS5
        const QString hostPortKey = QString("%1:%2").arg(desc.host.toLower()).arg(desc.port);
        if (desc.protocol == ProxyProtocol::SocksAuto) {
            const int detectedVer = s_detectedSocksVersion.value(hostPortKey, 0);
            if (detectedVer == 5) {
                desc.protocol = ProxyProtocol::Socks5h;
            } else if (detectedVer == 4) {
                desc.protocol = ProxyProtocol::Socks4a;
            }
        }

        // 4. Формирование QNetworkProxy
        if (desc.protocol == ProxyProtocol::Http) {
            QNetworkProxy proxy(QNetworkProxy::HttpProxy, desc.host, desc.port, desc.user, desc.password);
            logProxyOnce(targetHost, targetPort, desc, "HTTP/HTTPS Proxy");
            return {proxy};
        }

        if (desc.protocol == ProxyProtocol::Socks5 || desc.protocol == ProxyProtocol::Socks5h) {
            QNetworkProxy proxy(QNetworkProxy::Socks5Proxy, desc.host, desc.port, desc.user, desc.password);
            auto caps = proxy.capabilities() | QNetworkProxy::TunnelingCapability;
            if (desc.protocol == ProxyProtocol::Socks5h) {
                caps |= QNetworkProxy::HostNameLookupCapability;
            } else {
                caps &= ~QNetworkProxy::HostNameLookupCapability;
            }
            proxy.setCapabilities(caps);
            logProxyOnce(targetHost, targetPort, desc,
                         desc.protocol == ProxyProtocol::Socks5h ? "SOCKS5h (Remote DNS)" : "SOCKS5");
            return {proxy};
        }

        // 5. Для SOCKS4, SOCKS4a и первичного SocksAuto поднимаем локальный loopback-мост
        quint16 bridgePort = ensureLocalBridge(desc);
        if (bridgePort > 0) {
            QNetworkProxy proxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), bridgePort);
            proxy.setCapabilities(QNetworkProxy::TunnelingCapability | QNetworkProxy::HostNameLookupCapability);
            const QString label = (desc.protocol == ProxyProtocol::Socks4)  ? "SOCKS4"
                                  : (desc.protocol == ProxyProtocol::Socks4a) ? "SOCKS4a"
                                                                              : "SOCKS4/5 Auto-Detect";
            logProxyOnce(targetHost, targetPort, desc, label);
            return {proxy};
        }

        // Резервный возврат нативного SOCKS5, если мост не удалось открыть
        QNetworkProxy fallback(QNetworkProxy::Socks5Proxy, desc.host, desc.port, desc.user, desc.password);
        fallback.setCapabilities(fallback.capabilities() |
                                 QNetworkProxy::TunnelingCapability |
                                 QNetworkProxy::HostNameLookupCapability);
        return {fallback};
    }

private:
    struct BridgeInstance {
        QTcpServer* server = nullptr;
        ProxyDescriptor descriptor;
        quint16 localPort = 0;
    };

    inline static LogCallback s_logger;
    inline static QMap<QString, int> s_detectedSocksVersion; // "host:port" -> 4 или 5
    inline static QMap<QString, BridgeInstance> s_bridges;
    inline static QSet<QString> s_loggedRoutes;

    static void logEvent(const QString& summary, const QString& payload = {}) {
        if (s_logger) {
            s_logger(QStringLiteral("PROXY"), summary, payload);
        }
    }

    static void logProxyOnce(const QString& targetHost,
                             int targetPort,
                             const ProxyDescriptor& desc,
                             const QString& protoLabel) {
        const QString routeKey = QString("%1:%2->%3:%4/%5")
        .arg(targetHost.toLower())
            .arg(targetPort)
            .arg(desc.host.toLower())
            .arg(desc.port)
            .arg(protoLabel);
        if (s_loggedRoutes.contains(routeKey)) return;
        s_loggedRoutes.insert(routeKey);
        logEvent(QString("Routing %1:%2 via %3 (%4:%5)")
                     .arg(targetHost)
                     .arg(targetPort)
                     .arg(protoLabel, desc.host)
                     .arg(desc.port),
                 QString("Source: %1").arg(desc.source));
    }

    static bool isLoopbackOrLocalHost(const QString& host) {
        if (host.isEmpty()) return true;
        const QString lower = host.toLower();
        if (lower == "localhost" || lower == "localhost.localdomain" ||
            lower == "127.0.0.1" || lower == "::1" || lower == "[::1]" || lower == "0.0.0.0") {
            return true;
        }
        QHostAddress addr(host);
        return !addr.isNull() && addr.isLoopback();
    }

    static bool matchesBypassPattern(const QString& host, int port, const QString& rawPattern) {
        QString pat = rawPattern.trimmed();
        if (pat.isEmpty()) return false;
        if (pat == "*") return true;
        if (pat.compare("<local>", Qt::CaseInsensitive) == 0) {
            return !host.contains('.');
        }

        const QString hostLower = host.toLower();
        const QString hostWithPort = QString("%1:%2").arg(hostLower).arg(port);

        if (pat.startsWith('.')) {
            const QString suffix = pat.toLower();
            const QString bare = suffix.mid(1);
            return hostLower == bare || hostLower.endsWith(suffix);
        }

        if (pat.contains('*') || pat.contains('?')) {
            QRegularExpression rx(QRegularExpression::wildcardToRegularExpression(pat),
                                  QRegularExpression::CaseInsensitiveOption);
            return rx.match(hostLower).hasMatch() || rx.match(hostWithPort).hasMatch();
        }

        const QString patLower = pat.toLower();
        if (patLower.startsWith("*.")) {
            const QString bare = patLower.mid(2);
            return hostLower == bare || hostLower.endsWith("." + bare);
        }

        return hostLower == patLower || hostWithPort == patLower ||
               hostLower.endsWith("." + patLower);
    }

    static bool isHostBypassed(const QString& host, int port) {
        QStringList patterns;
        const QString noProxyEnv = qEnvironmentVariable("NO_PROXY", qEnvironmentVariable("no_proxy"));
        if (!noProxyEnv.isEmpty()) {
            QString norm = noProxyEnv;
            norm.replace(';', ',');
            patterns.append(norm.split(',', Qt::SkipEmptyParts));
        }

#ifdef Q_OS_WIN
        QSettings winInet(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"),
                          QSettings::NativeFormat);
        if (winInet.value("ProxyEnable", 0).toInt() != 0) {
            const QString overrideStr = winInet.value("ProxyOverride").toString();
            if (!overrideStr.isEmpty()) {
                QString norm = overrideStr;
                norm.replace(',', ';');
                patterns.append(norm.split(';', Qt::SkipEmptyParts));
            }
        }
#endif

        for (const QString& pat : patterns) {
            if (matchesBypassPattern(host, port, pat)) {
                return true;
            }
        }
        return false;
    }

    static ProxyDescriptor parseProxyUrlOrHostPort(const QString& rawSpec,
                                                   ProxyProtocol defaultProtocol,
                                                   const QString& sourceName) {
        QString spec = rawSpec.trimmed();
        if (spec.isEmpty()) return {};

        ProxyProtocol proto = defaultProtocol;
        bool hasScheme = false;

        static const QRegularExpression rxScheme(R"(^([A-Za-z0-9+\-\.]+)://(.*)$)");
        auto m = rxScheme.match(spec);
        if (m.hasMatch()) {
            hasScheme = true;
            const QString s = m.captured(1).toLower();
            if (s == "socks4") {
                proto = ProxyProtocol::Socks4;
            } else if (s == "socks4a") {
                proto = ProxyProtocol::Socks4a;
            } else if (s == "socks5") {
                proto = ProxyProtocol::Socks5;
            } else if (s == "socks5h") {
                proto = ProxyProtocol::Socks5h;
            } else if (s == "socks") {
                proto = ProxyProtocol::SocksAuto;
            } else if (s == "http" || s == "https") {
                proto = ProxyProtocol::Http;
            } else {
                return {};
            }
        }

        QUrl url(hasScheme ? spec : (QStringLiteral("http://") + spec));
        if (!url.isValid() || url.host().isEmpty()) {
            return {};
        }

        int defaultPort = (proto == ProxyProtocol::Http) ? 8080 : 1080;
        int port = url.port(defaultPort);
        if (port <= 0 || port > 65535) {
            return {};
        }

        ProxyDescriptor out;
        out.protocol = proto;
        out.host = url.host();
        out.port = static_cast<quint16>(port);
        out.user = url.userName();
        out.password = url.password();
        out.source = sourceName;
        return out;
    }

    static ProxyDescriptor resolveFromEnvironment(const QString& scheme) {
        auto checkVars = [](std::initializer_list<const char*> names, ProxyProtocol defProto) -> ProxyDescriptor {
            for (const char* name : names) {
                const QString val = qEnvironmentVariable(name).trimmed();
                if (!val.isEmpty()) {
                    auto d = parseProxyUrlOrHostPort(val, defProto, QString("Env (%1)").arg(name));
                    if (d.isValid()) return d;
                }
            }
            return {};
        };

        if (scheme == "https") {
            if (auto d = checkVars({"HTTPS_PROXY", "https_proxy"}, ProxyProtocol::Http); d.isValid()) return d;
        } else if (scheme == "http") {
            if (auto d = checkVars({"HTTP_PROXY", "http_proxy"}, ProxyProtocol::Http); d.isValid()) return d;
        }

        if (auto d = checkVars({"ALL_PROXY", "all_proxy"}, ProxyProtocol::SocksAuto); d.isValid()) return d;
        if (auto d = checkVars({"SOCKS5_PROXY", "socks5_proxy"}, ProxyProtocol::Socks5h); d.isValid()) return d;
        if (auto d = checkVars({"SOCKS4_PROXY", "socks4_proxy"}, ProxyProtocol::Socks4a); d.isValid()) return d;
        if (auto d = checkVars({"SOCKS_PROXY", "socks_proxy"}, ProxyProtocol::SocksAuto); d.isValid()) return d;
        if (auto d = checkVars({"HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy"}, ProxyProtocol::Http); d.isValid()) return d;

        return {};
    }

#ifdef Q_OS_WIN
    static ProxyDescriptor resolveFromWindowsRegistry(const QString& scheme) {
        QSettings winInet(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"),
                          QSettings::NativeFormat);
        if (winInet.value("ProxyEnable", 0).toInt() == 0) {
            return {};
        }

        const QString proxyServer = winInet.value("ProxyServer").toString().trimmed();
        if (proxyServer.isEmpty()) {
            return {};
        }

        // Формат списка протоколов: "http=127.0.0.1:8080;https=127.0.0.1:8080;socks=127.0.0.1:1080"
        if (proxyServer.contains('=')) {
            QMap<QString, QString> map;
            const QStringList parts = proxyServer.split(QRegularExpression(R"([;\s]+)"), Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                const int eq = part.indexOf('=');
                if (eq > 0) {
                    map.insert(part.left(eq).trimmed().toLower(), part.mid(eq + 1).trimmed());
                }
            }

            if (scheme == "https" && map.contains("https")) {
                if (auto d = parseProxyUrlOrHostPort(map.value("https"), ProxyProtocol::Http, "Windows Registry (https=)"); d.isValid())
                    return d;
            }
            if ((scheme == "http" || scheme == "https") && map.contains("http")) {
                if (auto d = parseProxyUrlOrHostPort(map.value("http"), ProxyProtocol::Http, "Windows Registry (http=)"); d.isValid())
                    return d;
            }
            if (map.contains("socks5")) {
                if (auto d = parseProxyUrlOrHostPort(map.value("socks5"), ProxyProtocol::Socks5h, "Windows Registry (socks5=)"); d.isValid())
                    return d;
            }
            if (map.contains("socks4")) {
                if (auto d = parseProxyUrlOrHostPort(map.value("socks4"), ProxyProtocol::Socks4a, "Windows Registry (socks4=)"); d.isValid())
                    return d;
            }
            if (map.contains("socks")) {
                if (auto d = parseProxyUrlOrHostPort(map.value("socks"), ProxyProtocol::SocksAuto, "Windows Registry (socks=)"); d.isValid())
                    return d;
            }
            return {};
        }

        // Единая строка прокси в Windows 10/11 (может содержать socks5://, socks4://, socks:// или host:port)
        return parseProxyUrlOrHostPort(proxyServer, ProxyProtocol::Http, "Windows Registry (ProxyServer)");
    }
#endif

    static ProxyDescriptor resolveSystemProxyDescriptor(const QNetworkProxyQuery& query,
                                                        const QString& scheme,
                                                        const QString& targetHost,
                                                        int targetPort) {
        // 1. Переменные окружения (на всех ОС)
        if (auto envDesc = resolveFromEnvironment(scheme); envDesc.isValid()) {
            return envDesc;
        }

#ifdef Q_OS_WIN
        // 2. Прямое чтение настроек Windows (корректно обрабатывает socks=, socks4://, socks5://)
        if (auto winDesc = resolveFromWindowsRegistry(scheme); winDesc.isValid()) {
            return winDesc;
        }
#endif

        // 3. Системный вызов Qt (WinHTTP PAC/WPAD, macOS SystemConfiguration, Linux GNOME/KDE/libproxy)
        QList<QNetworkProxy> sysList = QNetworkProxyFactory::systemProxyForQuery(query);
        auto extractFromQtList = [](const QList<QNetworkProxy>& list, const QString& src) -> ProxyDescriptor {
            for (const QNetworkProxy& p : list) {
                if (p.type() == QNetworkProxy::NoProxy || p.hostName().isEmpty() || p.port() <= 0) {
                    continue;
                }
                if (p.type() == QNetworkProxy::Socks5Proxy) {
                    return {ProxyProtocol::SocksAuto, p.hostName(), p.port(), p.user(), p.password(), src};
                }
                if (p.type() == QNetworkProxy::HttpProxy || p.type() == QNetworkProxy::HttpCachingProxy) {
                    return {ProxyProtocol::Http, p.hostName(), p.port(), p.user(), p.password(), src};
                }
            }
            return {};
        };

        if (auto sysDesc = extractFromQtList(sysList, "OS SystemProxy (UrlRequest)"); sysDesc.isValid()) {
            return sysDesc;
        }

        // 4. Резервный запрос TcpSocket (в GNOME/KDE возвращает системный SOCKS-прокси, когда HTTP-прокси пуст)
        QNetworkProxyQuery tcpQuery(targetHost, targetPort, QStringLiteral("socks"), QNetworkProxyQuery::TcpSocket);
        if (auto tcpDesc = extractFromQtList(QNetworkProxyFactory::systemProxyForQuery(tcpQuery),
                                             "OS SystemProxy (SOCKS TcpSocket)");
            tcpDesc.isValid()) {
            return tcpDesc;
        }

        return {};
    }

    // =========================================================================
    // Локальный асинхронный мост SOCKS5 (Qt) -> SOCKS4 / SOCKS4a / Auto-Detect
    // =========================================================================
    static quint16 ensureLocalBridge(const ProxyDescriptor& desc) {
        const QString key = desc.key();
        if (s_bridges.contains(key)) {
            const auto& existing = s_bridges[key];
            if (existing.server && existing.server->isListening()) {
                return existing.localPort;
            }
        }

        auto* server = new QTcpServer(QCoreApplication::instance());
        server->setProxy(QNetworkProxy::NoProxy);
        if (!server->listen(QHostAddress::LocalHost, 0)) {
            delete server;
            return 0;
        }

        BridgeInstance inst;
        inst.server = server;
        inst.descriptor = desc;
        inst.localPort = server->serverPort();
        s_bridges.insert(key, inst);

        QObject::connect(server, &QTcpServer::newConnection, server, [server, desc]() {
            while (server->hasPendingConnections()) {
                QTcpSocket* clientSock = server->nextPendingConnection();
                if (clientSock) {
                    handleBridgeClient(clientSock, desc);
                }
            }
        });

        return inst.localPort;
    }

    struct BridgeSession {
        QPointer<QTcpSocket> client;
        QPointer<QTcpSocket> upstream;
        ProxyDescriptor desc;

        enum class State {
            WaitClientGreeting,
            WaitClientConnectReq,
            WaitUpstreamSocks5Greeting,
            WaitUpstreamSocks5Auth,
            WaitUpstreamSocks5Connect,
            WaitUpstreamSocks4Connect,
            Tunneling,
            Closed
        } state = State::WaitClientGreeting;

        QByteArray clientBuf;
        QByteArray upstreamBuf;
        QByteArray rawSocks5ConnectReq;

        QString targetHost;
        quint32 targetIpv4 = 0;
        quint16 targetPort = 0;
        bool hasIpv4 = false;
        bool triedSocks4aDomain = false;
    };

    static void handleBridgeClient(QTcpSocket* clientSock, const ProxyDescriptor& desc) {
        auto session = std::make_shared<BridgeSession>();
        session->client = clientSock;
        session->desc = desc;

        QObject::connect(clientSock, &QTcpSocket::readyRead, clientSock, [session]() {
            onBridgeClientReadyRead(session);
        });
        QObject::connect(clientSock, &QTcpSocket::disconnected, clientSock, [session]() {
            closeBridgeSession(session);
        });
    }

    static void closeBridgeSession(const std::shared_ptr<BridgeSession>& s) {
        if (!s || s->state == BridgeSession::State::Closed) return;
        s->state = BridgeSession::State::Closed;
        if (s->client) {
            s->client->disconnect();
            s->client->close();
            s->client->deleteLater();
        }
        if (s->upstream) {
            s->upstream->disconnect();
            s->upstream->close();
            s->upstream->deleteLater();
        }
    }

    static void sendClientSocks5Reply(const std::shared_ptr<BridgeSession>& s, quint8 repCode) {
        if (!s || !s->client) return;
        const char reply[10] = {0x05, static_cast<char>(repCode), 0x00, 0x01, 0, 0, 0, 0, 0, 0};
        s->client->write(reply, 10);
        s->client->flush();
    }

    static void enterTunnelingMode(const std::shared_ptr<BridgeSession>& s) {
        if (!s || !s->client || !s->upstream) return;
        s->state = BridgeSession::State::Tunneling;
        sendClientSocks5Reply(s, 0x00);

        if (!s->clientBuf.isEmpty()) {
            s->upstream->write(s->clientBuf);
            s->clientBuf.clear();
        }
        if (!s->upstreamBuf.isEmpty()) {
            s->client->write(s->upstreamBuf);
            s->upstreamBuf.clear();
        }
    }

    static void onBridgeClientReadyRead(const std::shared_ptr<BridgeSession>& s) {
        if (!s || !s->client || s->state == BridgeSession::State::Closed) return;

        if (s->state == BridgeSession::State::Tunneling) {
            if (s->upstream) {
                s->upstream->write(s->client->readAll());
            }
            return;
        }

        s->clientBuf.append(s->client->readAll());

        if (s->state == BridgeSession::State::WaitClientGreeting) {
            if (s->clientBuf.size() < 2) return;
            const quint8 ver = static_cast<quint8>(s->clientBuf[0]);
            const quint8 nmethods = static_cast<quint8>(s->clientBuf[1]);
            if (ver != 0x05) {
                closeBridgeSession(s);
                return;
            }
            if (s->clientBuf.size() < 2 + nmethods) return;
            s->clientBuf.remove(0, 2 + nmethods);

            s->client->write("\x05\x00", 2);
            s->state = BridgeSession::State::WaitClientConnectReq;
        }

        if (s->state == BridgeSession::State::WaitClientConnectReq) {
            if (s->clientBuf.size() < 4) return;
            const quint8 ver = static_cast<quint8>(s->clientBuf[0]);
            const quint8 cmd = static_cast<quint8>(s->clientBuf[1]);
            const quint8 atyp = static_cast<quint8>(s->clientBuf[3]);
            if (ver != 0x05 || cmd != 0x01) {
                sendClientSocks5Reply(s, 0x07);
                closeBridgeSession(s);
                return;
            }

            int reqLen = 0;
            if (atyp == 0x01) {
                reqLen = 10;
                if (s->clientBuf.size() < reqLen) return;
                quint32 ip = (static_cast<quint8>(s->clientBuf[4]) << 24) |
                             (static_cast<quint8>(s->clientBuf[5]) << 16) |
                             (static_cast<quint8>(s->clientBuf[6]) << 8) |
                             static_cast<quint8>(s->clientBuf[7]);
                quint16 port = (static_cast<quint8>(s->clientBuf[8]) << 8) |
                               static_cast<quint8>(s->clientBuf[9]);
                s->targetIpv4 = ip;
                s->targetHost = QHostAddress(ip).toString();
                s->targetPort = port;
                s->hasIpv4 = true;
            } else if (atyp == 0x03) {
                if (s->clientBuf.size() < 5) return;
                const int domLen = static_cast<quint8>(s->clientBuf[4]);
                reqLen = 4 + 1 + domLen + 2;
                if (s->clientBuf.size() < reqLen) return;
                s->targetHost = QString::fromUtf8(s->clientBuf.mid(5, domLen));
                s->targetPort = (static_cast<quint8>(s->clientBuf[5 + domLen]) << 8) |
                                static_cast<quint8>(s->clientBuf[6 + domLen]);
                QHostAddress addr(s->targetHost);
                if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                    s->targetIpv4 = addr.toIPv4Address();
                    s->hasIpv4 = true;
                }
            } else if (atyp == 0x04) {
                reqLen = 22;
                if (s->clientBuf.size() < reqLen) return;
                s->targetPort = (static_cast<quint8>(s->clientBuf[20]) << 8) |
                                static_cast<quint8>(s->clientBuf[21]);
            } else {
                sendClientSocks5Reply(s, 0x08);
                closeBridgeSession(s);
                return;
            }

            s->rawSocks5ConnectReq = s->clientBuf.left(reqLen);
            s->clientBuf.remove(0, reqLen);

            startUpstreamConnection(s);
        }
    }

    static void startUpstreamConnection(const std::shared_ptr<BridgeSession>& s) {
        if (!s || s->state == BridgeSession::State::Closed) return;

        const QString hostPortKey = QString("%1:%2").arg(s->desc.host.toLower()).arg(s->desc.port);
        ProxyProtocol effectiveProto = s->desc.protocol;
        if (effectiveProto == ProxyProtocol::SocksAuto) {
            const int cachedVer = s_detectedSocksVersion.value(hostPortKey, 0);
            if (cachedVer == 5) effectiveProto = ProxyProtocol::Socks5h;
            else if (cachedVer == 4) effectiveProto = ProxyProtocol::Socks4a;
        }

        if (effectiveProto == ProxyProtocol::SocksAuto ||
            effectiveProto == ProxyProtocol::Socks5 ||
            effectiveProto == ProxyProtocol::Socks5h) {
            connectUpstreamSocks5(s, effectiveProto == ProxyProtocol::SocksAuto);
        } else {
            connectUpstreamSocks4(s, effectiveProto == ProxyProtocol::Socks4a);
        }
    }

    static void connectUpstreamSocks5(const std::shared_ptr<BridgeSession>& s, bool allowFallbackToSocks4) {
        if (!s || s->state == BridgeSession::State::Closed) return;

        auto* up = new QTcpSocket();
        up->setProxy(QNetworkProxy::NoProxy);
        s->upstream = up;
        s->upstreamBuf.clear();
        s->state = BridgeSession::State::WaitUpstreamSocks5Greeting;

        const QString hostPortKey = QString("%1:%2").arg(s->desc.host.toLower()).arg(s->desc.port);

        QObject::connect(up, &QTcpSocket::connected, up, [s, up]() {
            if (!s->desc.user.isEmpty()) {
                up->write("\x05\x02\x00\x02", 4);
            } else {
                up->write("\x05\x01\x00", 3);
            }
        });

        QObject::connect(up, &QTcpSocket::readyRead, up, [s, up, allowFallbackToSocks4, hostPortKey]() {
            if (s->state == BridgeSession::State::Tunneling) {
                if (s->client) s->client->write(up->readAll());
                return;
            }

            s->upstreamBuf.append(up->readAll());

            if (s->state == BridgeSession::State::WaitUpstreamSocks5Greeting) {
                if (s->upstreamBuf.size() < 2) return;
                const quint8 ver = static_cast<quint8>(s->upstreamBuf[0]);
                const quint8 method = static_cast<quint8>(s->upstreamBuf[1]);
                if (ver != 0x05 || method == 0xFF) {
                    if (allowFallbackToSocks4) {
                        s_detectedSocksVersion[hostPortKey] = 4;
                        logEvent(QString("Detected SOCKS4 proxy on %1:%2 (falling back from SOCKS5)")
                                     .arg(s->desc.host).arg(s->desc.port));
                        up->disconnect();
                        up->close();
                        up->deleteLater();
                        s->upstream = nullptr;
                        connectUpstreamSocks4(s, true);
                        return;
                    }
                    sendClientSocks5Reply(s, 0x01);
                    closeBridgeSession(s);
                    return;
                }

                s_detectedSocksVersion[hostPortKey] = 5;
                s->upstreamBuf.remove(0, 2);

                if (method == 0x02) {
                    const QByteArray u = s->desc.user.toUtf8();
                    const QByteArray p = s->desc.password.toUtf8();
                    QByteArray auth;
                    auth.append('\x01');
                    auth.append(static_cast<char>(u.size()));
                    auth.append(u);
                    auth.append(static_cast<char>(p.size()));
                    auth.append(p);
                    up->write(auth);
                    s->state = BridgeSession::State::WaitUpstreamSocks5Auth;
                } else {
                    up->write(s->rawSocks5ConnectReq);
                    s->state = BridgeSession::State::WaitUpstreamSocks5Connect;
                }
            }

            if (s->state == BridgeSession::State::WaitUpstreamSocks5Auth) {
                if (s->upstreamBuf.size() < 2) return;
                const quint8 status = static_cast<quint8>(s->upstreamBuf[1]);
                s->upstreamBuf.remove(0, 2);
                if (status != 0x00) {
                    sendClientSocks5Reply(s, 0x01);
                    closeBridgeSession(s);
                    return;
                }
                up->write(s->rawSocks5ConnectReq);
                s->state = BridgeSession::State::WaitUpstreamSocks5Connect;
            }

            if (s->state == BridgeSession::State::WaitUpstreamSocks5Connect) {
                if (s->upstreamBuf.size() < 4) return;
                const quint8 rep = static_cast<quint8>(s->upstreamBuf[1]);
                const quint8 atyp = static_cast<quint8>(s->upstreamBuf[3]);
                int respLen = 10;
                if (atyp == 0x03) {
                    if (s->upstreamBuf.size() < 5) return;
                    respLen = 4 + 1 + static_cast<quint8>(s->upstreamBuf[4]) + 2;
                } else if (atyp == 0x04) {
                    respLen = 22;
                }
                if (s->upstreamBuf.size() < respLen) return;
                s->upstreamBuf.remove(0, respLen);

                if (rep != 0x00) {
                    sendClientSocks5Reply(s, rep);
                    closeBridgeSession(s);
                    return;
                }
                enterTunnelingMode(s);
            }
        });

        QObject::connect(up, &QTcpSocket::disconnected, up, [s, up, allowFallbackToSocks4, hostPortKey]() {
            if (s->state == BridgeSession::State::WaitUpstreamSocks5Greeting && allowFallbackToSocks4) {
                s_detectedSocksVersion[hostPortKey] = 4;
                logEvent(QString("Detected SOCKS4 proxy on %1:%2 (SOCKS5 greeting closed by peer)")
                             .arg(s->desc.host).arg(s->desc.port));
                up->disconnect();
                up->deleteLater();
                s->upstream = nullptr;
                connectUpstreamSocks4(s, true);
                return;
            }
            closeBridgeSession(s);
        });

        QObject::connect(up, &QAbstractSocket::errorOccurred, up, [s](QAbstractSocket::SocketError) {
            if (s->state != BridgeSession::State::WaitUpstreamSocks5Greeting) {
                closeBridgeSession(s);
            }
        });

        up->connectToHost(s->desc.host, s->desc.port);
    }

    static void connectUpstreamSocks4(const std::shared_ptr<BridgeSession>& s, bool useSocks4aDomain) {
        if (!s || s->state == BridgeSession::State::Closed) return;

        // Если требуется чистый SOCKS4 (по IPv4), а у нас доменное имя — резолвим его в IPv4
        if (!s->hasIpv4 && !useSocks4aDomain && !s->targetHost.isEmpty()) {
            QHostInfo::lookupHost(s->targetHost, s->client.data(), [s](const QHostInfo& info) {
                if (s->state == BridgeSession::State::Closed) return;
                for (const QHostAddress& addr : info.addresses()) {
                    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                        s->targetIpv4 = addr.toIPv4Address();
                        s->hasIpv4 = true;
                        break;
                    }
                }
                // Если локальный DNS не смог разрешить имя, пробуем отправить через SOCKS4a (0.0.0.1)
                sendSocks4PacketOverSocket(s, !s->hasIpv4);
            });
            return;
        }

        sendSocks4PacketOverSocket(s, !s->hasIpv4 && useSocks4aDomain);
    }

    static void sendSocks4PacketOverSocket(const std::shared_ptr<BridgeSession>& s, bool sendAsSocks4a) {
        if (!s || s->state == BridgeSession::State::Closed) return;
        if (!s->hasIpv4 && !sendAsSocks4a) {
            sendClientSocks5Reply(s, 0x04);
            closeBridgeSession(s);
            return;
        }

        s->triedSocks4aDomain = sendAsSocks4a;
        auto* up = new QTcpSocket();
        up->setProxy(QNetworkProxy::NoProxy);
        s->upstream = up;
        s->upstreamBuf.clear();
        s->state = BridgeSession::State::WaitUpstreamSocks4Connect;

        QObject::connect(up, &QTcpSocket::connected, up, [s, up, sendAsSocks4a]() {
            QByteArray pkt;
            pkt.append('\x04'); // VN = 4
            pkt.append('\x01'); // CD = 1 (CONNECT)
            pkt.append(static_cast<char>((s->targetPort >> 8) & 0xFF));
            pkt.append(static_cast<char>(s->targetPort & 0xFF));

            if (sendAsSocks4a) {
                // SOCKS4a: IP 0.0.0.1 сигнализирует серверу прочитать доменное имя после USERID\0
                pkt.append('\x00');
                pkt.append('\x00');
                pkt.append('\x00');
                pkt.append('\x01');
            } else {
                pkt.append(static_cast<char>((s->targetIpv4 >> 24) & 0xFF));
                pkt.append(static_cast<char>((s->targetIpv4 >> 16) & 0xFF));
                pkt.append(static_cast<char>((s->targetIpv4 >> 8) & 0xFF));
                pkt.append(static_cast<char>(s->targetIpv4 & 0xFF));
            }

            if (!s->desc.user.isEmpty()) {
                pkt.append(s->desc.user.toUtf8());
            }
            pkt.append('\x00');

            if (sendAsSocks4a) {
                pkt.append(s->targetHost.toUtf8());
                pkt.append('\x00');
            }

            up->write(pkt);
        });

        QObject::connect(up, &QTcpSocket::readyRead, up, [s, up]() {
            if (s->state == BridgeSession::State::Tunneling) {
                if (s->client) s->client->write(up->readAll());
                return;
            }

            s->upstreamBuf.append(up->readAll());
            if (s->state == BridgeSession::State::WaitUpstreamSocks4Connect) {
                if (s->upstreamBuf.size() < 8) return;
                const quint8 vn = static_cast<quint8>(s->upstreamBuf[0]);
                const quint8 cd = static_cast<quint8>(s->upstreamBuf[1]);
                s->upstreamBuf.remove(0, 8);

                if ((vn == 0x00 || vn == 0x04) && cd == 0x5A) {
                    enterTunnelingMode(s);
                } else if (s->triedSocks4aDomain && !s->hasIpv4 && !s->targetHost.isEmpty()) {
                    // Сервер не поддерживает расширение SOCKS4a (0.0.0.1) — резолвим IPv4 и повторяем чистым SOCKS4
                    up->disconnect();
                    up->close();
                    up->deleteLater();
                    s->upstream = nullptr;
                    connectUpstreamSocks4(s, false);
                } else {
                    sendClientSocks5Reply(s, 0x05);
                    closeBridgeSession(s);
                }
            }
        });

        QObject::connect(up, &QTcpSocket::disconnected, up, [s]() {
            closeBridgeSession(s);
        });

        QObject::connect(up, &QAbstractSocket::errorOccurred, up, [s](QAbstractSocket::SocketError) {
            closeBridgeSession(s);
        });

        up->connectToHost(s->desc.host, s->desc.port);
    }
};
