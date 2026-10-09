#include "guide/GuidePause.h"
#include "guide/GuideAnr.h"

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
  QString anrToken;
  const auto environment = QProcessEnvironment::systemEnvironment();
  const auto resume = [&] { paused.resume(); GuideAnr::release(anrToken, environment); anrToken.clear(); };
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
        resume();
        anrToken = request.value("anrToken").toString();
        std::function<bool(const QJsonObject&)> pin;
        if (request.value("recoverable").toBool()) pin = [](const QJsonObject& identity) {
          const auto report = QJsonDocument(QJsonObject{{"pin", identity}}).toJson(QJsonDocument::Compact) + '\n';
          if (::write(STDOUT_FILENO, report.constData(), report.size()) != report.size()) return false;
          pollfd owner{STDIN_FILENO, POLLIN | POLLHUP, 0};
          if (::poll(&owner, 1, 1500) <= 0) return false;
          char ack[64]; const auto size = ::read(STDIN_FILENO, ack, sizeof(ack));
          return size == 7 && QByteArray(ack, size) == "pin-ok\n";
        };
        ok = anrToken.isEmpty() || GuideAnr::acquire(anrToken, environment);
        if (ok) ok = paused.stop(request.value("pid").toInteger(), request.value("start").toInteger(), &error, pin);
        else error = "Hyprland's unresponsive dialog could not be suppressed safely.";
        if (!ok) resume();
      } else if (request.value("action") == "resume") {
        resume();
      } else {
        ok = false;
      }
      const auto reply = QJsonDocument(QJsonObject{{"ok", ok}, {"error", error}, {"stopped", paused.identities()}}).toJson(QJsonDocument::Compact) + '\n';
      if (::write(STDOUT_FILENO, reply.constData(), reply.size()) < 0) return 1;
    }
  }
  // EOF is the ownership boundary, including a crash or SIGKILL of Omakade.
  resume();
  return 0;
}
