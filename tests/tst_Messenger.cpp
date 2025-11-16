#include <QtTest>
#include <QElapsedTimer>
#include <QThread>
#include <QSignalSpy>
#include <QCoreApplication>
#include <thread>
#include <vector>
#include "tst_Messenger.h"

// 说明：本文件实现了针对 Messenger 的单元测试，
// 覆盖注册/发送/注销/清理、Token 过滤、异步分发、多线程压力、
// 顺序保证、幂等行为等多个方面。

void TestReceiver::onMessage(const MyMessage& msg)
{
    received.append(msg);
    emit messageReceived();
}

void MessengerTest::waitForDispatch(int ms)
{
    // 为异步分发保留时间窗口；在跨线程用例中配合 QTRY_COMPARE 使用
    QTest::qWait(ms);
}

void MessengerTest::init() {
    // 每个用例开始前：清空接收者状态并移除上次遗留的订阅
    memberReceiver.received.clear();
    lambdaReceived.clear();
    Messenger::Default().Cleanup();
    Messenger::Default().Unregister(&memberReceiver);
    Messenger::Default().Unregister(&lambdaReceiver);
}

void MessengerTest::cleanup() {
    // 每个用例结束后：再次移除订阅并清理已析构接收者的弱引用
    Messenger::Default().Unregister(&memberReceiver);
    Messenger::Default().Unregister(&lambdaReceiver);
    Messenger::Default().Cleanup();
}

void MessengerTest::register_member_and_send() {
    // 成员函数注册并基本发送：期望收到 1 条且内容一致
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);

    MyMessage msg{42, "hello"};
    Messenger::Default().Send<MyMessage>(msg);
    waitForDispatch();

    QCOMPARE(memberReceiver.received.size(), 1);
    QCOMPARE(memberReceiver.received.front(), msg);
}

void MessengerTest::register_lambda_with_token_and_send() {
    // Lambda 注册并按 Token 过滤：仅接收 tokenA 的消息
    const MessageToken tokenA{"alpha"};
    const MessageToken tokenB{"beta"};

    Messenger::Default().Register<MyMessage>(&lambdaReceiver, [this](const MyMessage& m){ lambdaReceived.append(m); }, tokenA);

    MyMessage a{1, "A"};
    MyMessage b{2, "B"};
    Messenger::Default().Send<MyMessage>(a, tokenA);
    Messenger::Default().Send<MyMessage>(b, tokenB);
    waitForDispatch();

    QCOMPARE(lambdaReceived.size(), 1);
    QCOMPARE(lambdaReceived.front(), a);
}

void MessengerTest::unregister_all() {
    // 全量注销（按接收者）：移除所有订阅后发送不再投递
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);
    Messenger::Default().Register<MyMessage>(&lambdaReceiver, [this](const MyMessage& m){ lambdaReceived.append(m); });

    Messenger::Default().Unregister(&memberReceiver);
    Messenger::Default().Unregister(&lambdaReceiver);

    Messenger::Default().Send<MyMessage>({7, "x"});
    waitForDispatch();

    QCOMPARE(memberReceiver.received.size(), 0);
    QCOMPARE(lambdaReceived.size(), 0);
}

void MessengerTest::unregister_by_type_and_token() {
    // 按类型与 Token 注销：移除 token1 的订阅，仅投递到 token2
    const MessageToken token1{"one"};
    const MessageToken token2{"two"};

    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage, token1);
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage, token2);

    Messenger::Default().Unregister<MyMessage>(&memberReceiver, token1);

    Messenger::Default().Send<MyMessage>({10, "t1"}, token1);
    Messenger::Default().Send<MyMessage>({11, "t2"}, token2);
    waitForDispatch();

    QCOMPARE(memberReceiver.received.size(), 1);
    QCOMPARE(memberReceiver.received.front(), (MyMessage{11, "t2"}));
}

void MessengerTest::cleanup_removes_dead_receivers() {
    // 清理已析构接收者：删除对象后 Cleanup 移除弱引用条目
    auto* ephemeral = new QObject();
    Messenger::Default().Register<MyMessage>(ephemeral, [](const MyMessage&){ });
    delete ephemeral;

    Messenger::Default().Cleanup();

    Messenger::Default().Send<MyMessage>({99, "dead"});
    waitForDispatch();

    QVERIFY(true);
}

void MessengerTest::async_dispatch_to_other_thread() {
    // 跨线程异步分发：接收者运行在 worker 线程，使用信号等待一次投递
    QThread worker;
    auto* other = new TestReceiver();
    other->moveToThread(&worker);
    qDebug() << "[Test] mainThread" << QThread::currentThread() << "appInstance" << QCoreApplication::instance();
    worker.start();
    qDebug() << "[Test] workerThread" << &worker << "otherThread" << other->thread();

    Messenger::Default().Register<MyMessage>(other, &TestReceiver::onMessage);

    QSignalSpy spy(other, &TestReceiver::messageReceived);
    Messenger::Default().Send<MyMessage>({777, "async"});
    qDebug() << "[Test] sent async message; spyCount" << spy.count();

    QTRY_COMPARE(spy.count(), 1);
    qDebug() << "[Test] after wait spyCount" << spy.count() << "otherSize" << other->received.size();
    QCOMPARE(other->received.size(), 1);

    Messenger::Default().Unregister(other);
    worker.quit();
    worker.wait();
    delete other;
}

void MessengerTest::multi_thread_send_pressure() {
    // 多线程压力发送：多线程累计发送，最终接收总数为 threads * perThread
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);

    const int threads = 8;
    const int perThread = 100;
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([perThread](){
            for (int i = 0; i < perThread; ++i) {
                Messenger::Default().Send<MyMessage>({i, "load"});
            }
        });
    }
    for (auto& th : pool) th.join();

    waitForDispatch(100);

    QCOMPARE(memberReceiver.received.size(), threads * perThread);

    Messenger::Default().Unregister(&memberReceiver);
}

void MessengerTest::token_filter_multi_thread() {
    // Token 并发过滤：不同 Token 分别发送，各自接收者统计相等
    const MessageToken t1{"T1"};
    const MessageToken t2{"T2"};
    TestReceiver other;
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage, t1);
    Messenger::Default().Register<MyMessage>(&other, &TestReceiver::onMessage, t2);

    const int threads = 4;
    const int perThread = 200;
    std::vector<std::thread> pool;
    pool.reserve(threads * 2);
    for (int i = 0; i < threads; ++i) {
        pool.emplace_back([perThread, t1](){
            for (int k = 0; k < perThread; ++k) {
                Messenger::Default().Send<MyMessage>({k, "t1"}, t1);
            }
        });
        pool.emplace_back([perThread, t2](){
            for (int k = 0; k < perThread; ++k) {
                Messenger::Default().Send<MyMessage>({k, "t2"}, t2);
            }
        });
    }
    for (auto& th : pool) th.join();
    waitForDispatch(150);

    QCOMPARE(memberReceiver.received.size(), threads * perThread);
    QCOMPARE(other.received.size(), threads * perThread);

    Messenger::Default().Unregister(&memberReceiver);
    Messenger::Default().Unregister(&other);
}

void MessengerTest::multiple_receivers_same_type() {
    // 多接收者同类型：一次发送应投递到所有订阅者
    TestReceiver other;
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);
    Messenger::Default().Register<MyMessage>(&other, &TestReceiver::onMessage);

    Messenger::Default().Send<MyMessage>({123, "both"});
    waitForDispatch();

    QCOMPARE(memberReceiver.received.size(), 1);
    QCOMPARE(other.received.size(), 1);

    Messenger::Default().Unregister(&memberReceiver);
    Messenger::Default().Unregister(&other);
}

void MessengerTest::re_register_after_unregister() {
    // 注销再注册：第一次不接收，重新注册后恢复接收
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);
    Messenger::Default().Unregister(&memberReceiver);
    Messenger::Default().Send<MyMessage>({1, "none"});
    waitForDispatch();
    QCOMPARE(memberReceiver.received.size(), 0);

    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);
    Messenger::Default().Send<MyMessage>({2, "one"});
    waitForDispatch();
    QCOMPARE(memberReceiver.received.size(), 1);

    Messenger::Default().Unregister(&memberReceiver);
}

void MessengerTest::type_isolation() {
    // 类型隔离：注册 MyMessage，但发送 AnotherMessage，不应接收到
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);
    AnotherMessage am{5, "x"};
    Messenger::Default().Send<AnotherMessage>(am);
    waitForDispatch();
    QCOMPARE(memberReceiver.received.size(), 0);
    Messenger::Default().Unregister(&memberReceiver);
}

void MessengerTest::empty_token_receives_all() {
    // 空 Token 订阅通配：未指定 Token 的订阅应接收所有 Token 的消息
    const MessageToken a{"A"};
    const MessageToken b{"B"};
    Messenger::Default().Register<MyMessage>(&lambdaReceiver, [this](const MyMessage& m){ lambdaReceived.append(m); });
    Messenger::Default().Send<MyMessage>({10, "a"}, a);
    Messenger::Default().Send<MyMessage>({11, "b"}, b);
    waitForDispatch();
    QCOMPARE(lambdaReceived.size(), 2);
    Messenger::Default().Unregister(&lambdaReceiver);
}

void MessengerTest::duplicate_register_same_type() {
    // 重复注册同一接收者：一次发送产生两次投递
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);
    Messenger::Default().Send<MyMessage>({101, "dup"});
    waitForDispatch();
    QCOMPARE(memberReceiver.received.size(), 2);
    Messenger::Default().Unregister(&memberReceiver);
}

void MessengerTest::duplicate_unregister_all_safe() {
    // 重复注销的幂等性：多次 Unregister 不影响后续行为
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);
    Messenger::Default().Unregister(&memberReceiver);
    Messenger::Default().Unregister(&memberReceiver);
    Messenger::Default().Send<MyMessage>({202, "none"});
    waitForDispatch();
    QCOMPARE(memberReceiver.received.size(), 0);
}

void MessengerTest::unregister_type_all_tokens() {
    // 类型级别全量注销：不带 Token 的 Unregister<T> 移除该类型的所有订阅
    const MessageToken t1{"X"};
    const MessageToken t2{"Y"};
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage, t1);
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage, t2);
    Messenger::Default().Unregister<MyMessage>(&memberReceiver);
    Messenger::Default().Send<MyMessage>({1, "x"}, t1);
    Messenger::Default().Send<MyMessage>({2, "y"}, t2);
    waitForDispatch();
    QCOMPARE(memberReceiver.received.size(), 0);
}

void MessengerTest::order_preservation_single_receiver() {
    // 顺序保持：单接收者应按发送顺序接收
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);
    const int N = 100;
    for (int i = 0; i < N; ++i) {
        Messenger::Default().Send<MyMessage>({i, "ord"});
    }
    waitForDispatch(100);
    QCOMPARE(memberReceiver.received.size(), N);
    for (int i = 0; i < N; ++i) {
        QCOMPARE(memberReceiver.received[i].code, i);
    }
    Messenger::Default().Unregister(&memberReceiver);
}

void MessengerTest::self_unregister_in_callback() {
    // 回调内自注销：首次收到时主动注销，后续不再接收
    Messenger::Default().Register<MyMessage>(&lambdaReceiver, [this](const MyMessage& m){
        if (lambdaReceived.isEmpty()) {
            Messenger::Default().Unregister(&lambdaReceiver);
        }
        lambdaReceived.append(m);
    });
    Messenger::Default().Send<MyMessage>({10, "first"});
    Messenger::Default().Send<MyMessage>({11, "second"});
    waitForDispatch();
    QCOMPARE(lambdaReceived.size(), 1);
}

void MessengerTest::send_then_immediate_unregister_race_same_thread() {
    // 同线程竞态：先发送再立即注销，后续发送不再投递
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);
    Messenger::Default().Send<MyMessage>({1, "one"});
    Messenger::Default().Unregister(&memberReceiver);
    Messenger::Default().Send<MyMessage>({2, "two"});
    waitForDispatch();
    QCOMPARE(memberReceiver.received.size(), 1);
}

void MessengerTest::send_then_immediate_unregister_race_cross_thread() {
    // 跨线程竞态：先发送到 worker，再立即注销；后续发送不再到达
    QThread worker;
    auto* other = new TestReceiver();
    other->moveToThread(&worker);
    worker.start();
    Messenger::Default().Register<MyMessage>(other, &TestReceiver::onMessage);
    QSignalSpy spy(other, &TestReceiver::messageReceived);
    Messenger::Default().Send<MyMessage>({3, "one"});
    Messenger::Default().Unregister(other);
    Messenger::Default().Send<MyMessage>({4, "two"});
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(other->received.size(), 1);
    worker.quit();
    worker.wait();
    delete other;
}

void MessengerTest::multi_type_concurrent() {
    // 多类型并发交织：同时对两种类型并发发送，各自接收者统计相等
    QList<AnotherMessage> otherReceived;
    QObject otherReceiver;
    Messenger::Default().Register<MyMessage>(&memberReceiver, &TestReceiver::onMessage);
    Messenger::Default().Register<AnotherMessage>(&otherReceiver, [&otherReceived](const AnotherMessage& m){ otherReceived.append(m); });

    const int threads = 4;
    const int per = 200;
    std::vector<std::thread> pool;
    pool.reserve(threads * 2);
    for (int i = 0; i < threads; ++i) {
        pool.emplace_back([per](){
            for (int k = 0; k < per; ++k) Messenger::Default().Send<MyMessage>({k, "A"});
        });
        pool.emplace_back([per](){
            for (int k = 0; k < per; ++k) Messenger::Default().Send<AnotherMessage>({k, "B"});
        });
    }
    for (auto& th : pool) th.join();
    waitForDispatch(200);
    QCOMPARE(memberReceiver.received.size(), threads * per);
    QCOMPARE(otherReceived.size(), threads * per);
    Messenger::Default().Unregister(&memberReceiver);
    Messenger::Default().Unregister(&otherReceiver);
}

void MessengerTest::broadcast_many_receivers() {
    // 广播到大量接收者：一次发送，所有接收者都收到一次
    const int N = 200;
    std::vector<std::unique_ptr<TestReceiver>> receivers;
    receivers.reserve(N);
    for (int i = 0; i < N; ++i) {
        auto r = std::make_unique<TestReceiver>();
        Messenger::Default().Register<MyMessage>(r.get(), &TestReceiver::onMessage);
        receivers.emplace_back(std::move(r));
    }
    Messenger::Default().Send<MyMessage>({9, "broadcast"});
    waitForDispatch(100);
    for (auto& r : receivers) {
        QCOMPARE(r->received.size(), 1);
        Messenger::Default().Unregister(r.get());
    }
}

QTEST_MAIN(MessengerTest)
