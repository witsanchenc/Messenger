#include "Messenger.h"

Messenger& Messenger::Default() {
    static Messenger instance;
    return instance;
}

void Messenger::Unregister(QObject* receiver) {
    if (!receiver) return;
    for (auto it = subscriptions.begin(); it != subscriptions.end(); ) {
        if (it->receiver.data() == receiver) {
            it = subscriptions.erase(it);
        } else {
            ++it;
        }
    }
}

void Messenger::Cleanup() {
    for (auto it = subscriptions.begin(); it != subscriptions.end(); ) {
        if (it->receiver.isNull()) {
            it = subscriptions.erase(it);
        } else {
            ++it;
        }
    }
}

void Messenger::internalRegister(quint64 type, const MessageToken& token, QObject* receiver, std::function<void(const QVariant&)>&& cb) {
    subscriptions.append({type, token, QPointer<QObject>(receiver), std::move(cb)});
}

void Messenger::internalSend(quint64 type, const MessageToken& token, const QVariant& payload) {
    for (const auto& sub : std::as_const(subscriptions)) {
        if (sub.type != type) continue;
        const bool tokenMatch = sub.token.isEmpty() || token.isEmpty() || sub.token == token;
        if (!tokenMatch) continue;
        if (sub.receiver.isNull()) continue;

        QMetaObject::invokeMethod(sub.receiver, [sub, payload] {
            sub.callback(payload);
        }, Qt::AutoConnection);
    }
}