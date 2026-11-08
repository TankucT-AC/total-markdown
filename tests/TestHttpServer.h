// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_TESTS_TESTHTTPSERVER_H
#define TOTALMARKDOWN_TESTS_TESTHTTPSERVER_H

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

/*
 * A one-shot HTTP fixture on loopback for the network tests: script exact
 * responses (status, headers, body, or "accept and hang" for the timeout path)
 * and count how many times each path was requested, which is how cache hits,
 * coalescing and "never reached the wire" are proved.
 */
class TestHttpServer
{
public:
    struct Route {
        int status = 200;
        QList<QPair<QByteArray, QByteArray>> headers;
        QByteArray body;
        bool respond = true; // false: accept the connection and never answer
    };

    TestHttpServer()
    {
        QObject::connect(&m_listener, &QTcpServer::newConnection, &m_listener, [this] {
            onConnection();
        });
        m_listener.listen(QHostAddress::LocalHost, 0);
    }

    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_listener.serverPort()).arg(path));
    }
    void setRoute(const QString &path, const Route &route)
    {
        m_routes.insert(path, route);
    }
    int hits(const QString &path) const
    {
        return m_hits.value(path);
    }
    bool isListening() const
    {
        return m_listener.isListening();
    }

private:
    void onConnection()
    {
        QTcpSocket *socket = m_listener.nextPendingConnection();
        socket->setParent(&m_listener);
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
            // A request may arrive in several packets; wait for the blank line
            // that ends the headers before parsing, or a split read would
            // truncate the path and the route would look missing.
            QByteArray &buffer = m_buffers[socket];
            buffer += socket->readAll();
            if (!buffer.contains("\r\n\r\n"))
                return;
            const qsizetype start = buffer.indexOf(' ') + 1;
            const qsizetype end = buffer.indexOf(' ', start);
            const QString path = QString::fromLatin1(buffer.mid(start, end - start));
            m_buffers.remove(socket);
            QObject::disconnect(socket, &QTcpSocket::readyRead, nullptr, nullptr);

            const Route route = m_routes.value(path, Route{404, {}, QByteArrayLiteral("no such route"), true});
            m_hits[path]++;
            if (!route.respond)
                return; // keep the socket open: the fetcher must hit its timeout

            QByteArray response = "HTTP/1.1 " + QByteArray::number(route.status) + " x\r\n";
            for (const auto &[key, value] : route.headers)
                response += key + ": " + value + "\r\n";
            response += "Content-Length: " + QByteArray::number(route.body.size()) + "\r\n";
            response += "Connection: close\r\n\r\n";
            response += route.body;
            socket->write(response);
            socket->disconnectFromHost();
        });
    }

    QTcpServer m_listener;
    QHash<QString, Route> m_routes;
    QHash<QString, int> m_hits;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

#endif // TOTALMARKDOWN_TESTS_TESTHTTPSERVER_H
