// Messenger.h
#pragma once
#include <QObject>
#include <QtCore/qglobal.h>
#include <QVariant>
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QThread>
#include <typeinfo>
#include <functional>
#include <qDebug>

class Messenger;

#ifdef MESSAGING_LIBRARY
#  define MESSAGING_API Q_DECL_EXPORT
#else
#  define MESSAGING_API Q_DECL_IMPORT
#endif


struct IMessage { virtual ~IMessage() = default; };

class MessageToken {
    QString id;
public:
    explicit MessageToken(const QString& token = QString()) : id(token) {}
    bool operator==(const MessageToken& other) const { return id == other.id; }
    bool operator!=(const MessageToken& other) const { return !(*this == other); }
    bool isEmpty() const { return id.isEmpty(); }
    QString toString() const { return id; }
};
inline uint qHash(const MessageToken& token, uint seed = 0) noexcept { return qHash(token.toString(), seed); }


class MESSAGING_API Messenger {
public:
    static Messenger& Default();

    // ----------------------------------------------------------
    // Register: 成员函数
    // ----------------------------------------------------------
    template<typename TMsg, typename TReceiver>
    void Register(TReceiver* receiver, void (TReceiver::*method)(const TMsg&), const MessageToken& token = MessageToken()) {
        Register<TMsg>(receiver, [receiver, method](const TMsg& msg) {
            (receiver->*method)(msg);
        }, token);
    }

    // ----------------------------------------------------------
    // Register: lambda / std::function
    // ----------------------------------------------------------
    template<typename TMsg, typename TFunc>
    void Register(QObject* receiver, TFunc&& callback, const MessageToken& token = MessageToken()) {
        auto wrapper = [callback = std::forward<TFunc>(callback)](const QVariant& var) {
            callback(var.value<TMsg>());
        };
        internalRegister(typeid(TMsg).hash_code(), token, receiver, std::move(wrapper));
    }

    // ----------------------------------------------------------
    // Send
    // ----------------------------------------------------------
    template<typename TMsg>
    void Send(const TMsg& message, const MessageToken& token = MessageToken()) {
        QVariant payload;
        payload.setValue(message);
        internalSend(typeid(TMsg).hash_code(), token, payload);
    }

    // ----------------------------------------------------------
    // Unregister（全部 / 按类型 / 按 Token）
    // ----------------------------------------------------------
    void Unregister(QObject* receiver);

    template<typename TMsg>
    void Unregister(QObject* receiver, const MessageToken& token = MessageToken()) {
        quint64 type = typeid(TMsg).hash_code();
        for (auto it = subscriptions.begin(); it != subscriptions.end(); ) {
            if (it->receiver.data() == receiver && it->type == type &&
                (token.isEmpty() || it->token == token)) {
                it = subscriptions.erase(it);
            } else {
                ++it;
            }
        }
    }

    // ----------------------------------------------------------
    // Cleanup（可选：清理已析构的弱引用）
    // ----------------------------------------------------------
    void Cleanup();

private:
    struct Subscription {
        quint64 type;
        MessageToken token;
        QPointer<QObject> receiver;  // 弱引用
        std::function<void(const QVariant&)> callback;
    };

    QList<Subscription> subscriptions;

    Messenger() = default;
    Q_DISABLE_COPY_MOVE(Messenger)

    void internalRegister(quint64 type, const MessageToken& token, QObject* receiver, std::function<void(const QVariant&)>&& cb);

    void internalSend(quint64 type, const MessageToken& token, const QVariant& payload);
};

// ──────────────────────────────────────────────────────────────
// 自动注册元类型（宏）
// ──────────────────────────────────────────────────────────────
#define DECLARE_MESSAGE_TYPE(T) \
    Q_DECLARE_METATYPE(T) \
    namespace { \
        struct __Register_##T { \
            __Register_##T() { \
                qRegisterMetaType<T>(#T); \
            } \
        }; \
        static __Register_##T __reg_##T; \
    }
