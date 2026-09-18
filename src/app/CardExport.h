#pragma once

#include <QObject>
#include <QString>

// Where the year-in-review card is written, and how the player is shown it.
//
// This lives in C++ rather than in QML so the card lands in the same place every time and so the
// path is something a test can assert rather than something the QML composes.
class CardExport final : public QObject {
  Q_OBJECT

public:
  explicit CardExport(QObject* parent = nullptr);

  // <Pictures>/Omakade/omakade-<label>-in-review.png, with the folder created. An empty return
  // means no pictures or home location could be resolved, which the screen reports rather than
  // writing somewhere surprising.
  [[nodiscard]] Q_INVOKABLE QString pathFor(const QString& label) const;
  // The folder the cards live in.
  [[nodiscard]] Q_INVOKABLE QString folder() const;
  // Hands the file's folder to the desktop so the player can find what was just written.
  [[nodiscard]] Q_INVOKABLE bool reveal(const QString& path) const;
  // Called once when a one-shot export finishes, so a headless run can report the outcome and
  // exit with it. Nothing is connected to this unless an export was requested on the command line,
  // so a card saved from the screen leaves the app running.
  Q_INVOKABLE void reportExport(bool written);

signals:
  void exportReported(bool written);
};
