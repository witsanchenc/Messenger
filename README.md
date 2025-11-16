# Messenger
Messenger 是一个轻量、类型安全的发布/订阅消息总线，用于 Qt 应用内跨对象/跨线程通信。
- 支持 Token 过滤、自动元类型注册与 `QVariant` 载荷、基于 `QMetaObject::invokeMethod` 的异步分发。
- 使用 `QPointer` 弱引用跟踪接收者，避免悬挂指针；`Cleanup()` 可清理已析构对象的订阅。
- 适合模块解耦、事件广播、后台任务通知、跨线程消息转发等场景。


使用说明：
- 使用 Qt Creator 打开 `Messenger.pro` 并选择合适的 Kit（如 MinGW/MSYS2；Qt 5.15）。
- 通过 qmake 生成构建系统，点击构建并运行即可。
- 也可在命令行执行：
  1) `qmake Messenger.pro`
  2) `mingw32-make` 或 `make`
  3) 运行生成的可执行文件。

系统与依赖：
- Windows（已在 MSYS2/MinGW 环境下开发与调试）。
- Qt 5.15（Widgets/核心模块）。

使用示例：
- 定义消息类型（需注册到 Qt 元类型系统）：
  
  ```cpp
  struct MyMessage { int code = 0; QString payload; };
  DECLARE_MESSAGE_TYPE(MyMessage) // 见 `Messenger.h` 中宏定义
  ```

- 订阅消息（成员函数或 Lambda，支持可选 Token 过滤）：
  
  ```cpp
  class TestReceiver : public QObject {
  public:
      void onMessage(const MyMessage& msg) { /* 处理消息 */ }
  } receiver;

  auto& bus = Messenger::Default();
  // 成员函数订阅
  bus.Register<MyMessage>(&receiver, &TestReceiver::onMessage);
  // Lambda 订阅并带 Token
  bus.Register<MyMessage>(&receiver,
      [](const MyMessage& m){ /* 处理 m */ }, MessageToken{"alpha"});
  ```

- 发布消息（可选按 Token 分发）：
  
  ```cpp
  MyMessage msg{42, "hello"};
  bus.Send<MyMessage>(msg);                      // 广播到所有 MyMessage 订阅者
  bus.Send<MyMessage>(msg, MessageToken{"alpha"}); // 仅投递到 token=alpha 的订阅者
  ```

- 注销与清理：
  
  ```cpp
  bus.Unregister(&receiver);                                 // 按接收者移除所有订阅
  bus.Unregister<MyMessage>(&receiver, MessageToken{"alpha"}); // 按类型+Token 精确移除
  bus.Cleanup(); // 移除接收者已析构的弱引用条目
  ```

设计原理：
- 类型隔离：以 `typeid(TMsg).hash_code()` 作为类型键，保证不同消息类型互不干扰（`Messenger.h:65-70`）。
- 载荷封装：使用 `QVariant` 承载消息实例，配合 `Q_DECLARE_METATYPE` 与 `qRegisterMetaType` 完成跨线程安全投递（宏 `DECLARE_MESSAGE_TYPE`，`Messenger.h:116-125`）。
- Token 过滤：订阅可绑定 `MessageToken`；空 Token 作为通配符，匹配逻辑为 `sub.token.isEmpty() || token.isEmpty() || sub.token == token`（`Messenger.cpp:36`）。
- 异步分发：通过 `QMetaObject::invokeMethod(..., Qt::AutoConnection)` 按接收者线程语义分发；同线程直接调用，跨线程排队到目标线程执行（`Messenger.cpp:40-42`）。
- 接收者管理：以 `QPointer<QObject>` 保存接收者弱引用，避免悬挂指针；`Cleanup()` 清除已析构对象的订阅（`Messenger.h:97-105`, `Messenger.cpp:19-27`）。
- 线程使用建议：并发发送是安全的（只读订阅表）；请避免与注册/注销并发交织，建议在主线程完成订阅管理后再进行跨线程发送。

鼓励加星：
- 如果该项目对你有帮助，请在仓库页面为它点个星（Star）。
- 你的支持能帮助我们持续维护与改进功能，感谢！

反馈与贡献：
- 欢迎提交 Issue 或 Pull Request 来反馈问题与贡献代码。
