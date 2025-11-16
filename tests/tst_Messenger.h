#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include "../Messenger.h"

// 说明：本文件定义了用于测试的消息类型和接收者类，
// 以及测试类的各个测试槽函数声明。
// 通过这些测试可以学习 Messenger 的注册/发送/注销/清理、
// Token 过滤、异步分发、多线程压力等行为。

// 基础消息类型：包含一个代码和字符串载荷，用于多数测试用例
struct MyMessage {
    int code = 0;
    QString payload;
    bool operator==(const MyMessage& other) const {
        return code == other.code && payload == other.payload;
    }
};
DECLARE_MESSAGE_TYPE(MyMessage)

// 另一种消息类型：用于验证不同类型的隔离与并发交织
struct AnotherMessage {
    int value = 0;
    QString text;
    bool operator==(const AnotherMessage& other) const {
        return value == other.value && text == other.text;
    }
};
DECLARE_MESSAGE_TYPE(AnotherMessage)

// 接收者类型：保存收到的 MyMessage，并提供成员函数回调；
// 同时发射 signal 以支持异步用例中的等待。
class TestReceiver : public QObject {
    Q_OBJECT
public:
    QList<MyMessage> received;

    void onMessage(const MyMessage& msg);
signals:
    void messageReceived();
};

// 测试类：包含所有针对 Messenger 的测试用例
class MessengerTest : public QObject {
    Q_OBJECT
private:
    TestReceiver memberReceiver;
    QObject lambdaReceiver;
    QList<MyMessage> lambdaReceived;

    static void waitForDispatch(int ms = 20);

private slots:
    void init();                                  // 每个用例前的初始化与清理订阅
    void cleanup();                               // 每个用例后的清理订阅与弱引用
    void register_member_and_send();              // 成员函数注册与基本发送
    void register_lambda_with_token_and_send();   // Lambda 注册并按 Token 过滤发送
    void unregister_all();                        // 全量注销（按接收者）
    void unregister_by_type_and_token();          // 按类型与 Token 注销
    void cleanup_removes_dead_receivers();        // 清理已析构接收者（弱引用）
    void async_dispatch_to_other_thread();        // 跨线程异步分发与信号等待
    void multi_thread_send_pressure();            // 多线程压力发送，校验累计数量
    void token_filter_multi_thread();             // 不同 Token 并发发送，各自接收
    void multiple_receivers_same_type();          // 同类型多接收者同时收到
    void re_register_after_unregister();          // 注销后重新注册恢复接收
    void type_isolation();                        // 不同消息类型相互隔离
    void empty_token_receives_all();              // 空 Token 订阅作为通配符
    void duplicate_register_same_type();          // 重复注册导致重复投递
    void duplicate_unregister_all_safe();         // 重复注销的幂等性
    void unregister_type_all_tokens();            // 不带 Token 的类型级别全量注销
    void order_preservation_single_receiver();    // 单接收者消息顺序保持
    void self_unregister_in_callback();           // 回调内自注销，只处理首次消息
    void send_then_immediate_unregister_race_same_thread(); // 同线程发送后立即注销的竞态
    void send_then_immediate_unregister_race_cross_thread(); // 跨线程发送后立即注销的竞态
    void multi_type_concurrent();                 // 多消息类型并发交织
    void broadcast_many_receivers();              // 大量接收者广播一次消息
};
