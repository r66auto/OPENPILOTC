#pragma once

#include <QLineEdit>
#include <QString>

#include "selfdrive/ui/qt/widgets/dialog.h"
#include "selfdrive/ui/qt/widgets/keyboard.h"

class InputDialog : public DialogBase {
  Q_OBJECT

public:
  explicit InputDialog(const QString &title, QWidget *parent, const QString &subtitle = "", bool secret = false);
  static QString getText(const QString &title, QWidget *parent, const QString &subtitle = "",
                         bool secret = false, int minLength = -1, const QString &defaultText = "");

  QString text() { return line->text(); }

  void setMessage(const QString &message, bool clearInputField = true) {
    label->setText(message);
    if (clearInputField) {
      line->setText("");
    }
  }

  void setMinLength(int length) { minLength = length; }
  void show() { setMainWindow(this); }

private:
  int minLength;
  QLineEdit *line;
  Keyboard *k;
  QLabel *label;
  QLabel *sublabel;
  QVBoxLayout *main_layout;
  QPushButton *eye_btn;

private slots:
  void handleEnter();

signals:
  void cancel();
  void emitText(const QString &text);
};
