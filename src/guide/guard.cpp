#include "guide/GuidePause.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <csignal>
#include <poll.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  struct sigaction action {};
  action.sa_handler = interrupt;
  ::sigemptyset(&action.sa_mask);
  ::sigaction(SIGTERM, &action, nullptr);
  ::sigaction(SIGINT, &action, nullptr);
  ::signal(SIGPIPE, SIG_IGN);
  GuidePause paused;
  QByteArray pending;
  while (!interrupted) {
    pollfd input{STDIN_FILENO, POLLIN | POLLHUP, 0};
    const int result = ::poll(&input, 1, 250);
    if (result < 0) continue;
    if (!result) continue;
    char buffer[4096];
    const auto count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
    if (count <= 0) break;
    pending.append(buffer, count);
    if (pending.size() > 8192) break;
    while (pending.contains('\n')) {
      const auto end = pending.indexOf('\n');
      const auto request = QJsonDocument::fromJson(pending.left(end)).object();
      pending.remove(0, end + 1);
      QString error;
      bool ok = true;
      if (request.value("action") == "pause") {
        ok = paused.stop(request.value("pid").toInteger(), request.value("start").toInteger(), &error);
      } else if (request.value("action") == "resume") {
        paused.resume();
      } else {
        ok = false;
      }
      const auto reply = QJsonDocument(QJsonObject{{"ok", ok}, {"error", error}, {"stopped", paused.identities()}}).toJson(QJsonDocument::Compact) + '\n';
      if (::write(STDOUT_FILENO, reply.constData(), reply.size()) < 0) return 1;
    }
  }
  // EOF is the ownership boundary, including a crash or SIGKILL of Omakade.
  return 0;
}
