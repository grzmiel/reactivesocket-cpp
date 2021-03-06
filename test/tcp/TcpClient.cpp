// Copyright 2004-present Facebook. All Rights Reserved.

#include <folly/Memory.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <gmock/gmock.h>
#include "src/NullRequestHandler.h"
#include "src/StandardReactiveSocket.h"
#include "src/folly/FollyKeepaliveTimer.h"
#include "src/framed/FramedDuplexConnection.h"
#include "src/tcp/TcpDuplexConnection.h"
#include "test/simple/PrintSubscriber.h"
#include "test/simple/StatsPrinter.h"

using namespace ::testing;
using namespace ::reactivesocket;
using namespace ::folly;

DEFINE_string(host, "localhost", "host to connect to");
DEFINE_int32(port, 9898, "host:port to connect to");

namespace {
class Callback : public AsyncSocket::ConnectCallback {
 public:
  virtual ~Callback() = default;

  void connectSuccess() noexcept override {}

  void connectErr(const AsyncSocketException& ex) noexcept override {
    std::cerr << "connectErr " << ex.what() << " " << ex.getType() << std::endl;
  }
};
}

int main(int argc, char* argv[]) {
  FLAGS_logtostderr = true;
  FLAGS_minloglevel = 0;

  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  ScopedEventBaseThread eventBaseThread;

  std::unique_ptr<StandardReactiveSocket> reactiveSocket;
  Callback callback;
  StatsPrinter stats;

  eventBaseThread.getEventBase()->runInEventBaseThreadAndWait(
      [&callback, &reactiveSocket, &eventBaseThread, &stats]() {
        folly::AsyncSocket::UniquePtr socket(
            new folly::AsyncSocket(eventBaseThread.getEventBase()));

        folly::SocketAddress addr(FLAGS_host, FLAGS_port, true);

        socket->connect(&callback, addr);

        std::cout << "attempting connection to " << addr.describe()
                  << std::endl;

        std::unique_ptr<DuplexConnection> connection =
            folly::make_unique<TcpDuplexConnection>(std::move(socket), stats);
        std::unique_ptr<DuplexConnection> framedConnection =
            folly::make_unique<FramedDuplexConnection>(std::move(connection));
        std::unique_ptr<RequestHandler> requestHandler =
            folly::make_unique<DefaultRequestHandler>();

        reactiveSocket = StandardReactiveSocket::fromClientConnection(
            *eventBaseThread.getEventBase(),
            std::move(framedConnection),
            std::move(requestHandler),
            ConnectionSetupPayload(
                "text/plain", "text/plain", Payload("meta", "data")),
            stats,
            folly::make_unique<FollyKeepaliveTimer>(
                *eventBaseThread.getEventBase(),
                std::chrono::milliseconds(5000)));

        reactiveSocket->requestSubscription(
            Payload("from client"), std::make_shared<PrintSubscriber>());
      });

  std::string name;
  std::getline(std::cin, name);

  eventBaseThread.getEventBase()->runInEventBaseThreadAndWait(
      [&reactiveSocket]() { reactiveSocket.reset(nullptr); });

  return 0;
}
