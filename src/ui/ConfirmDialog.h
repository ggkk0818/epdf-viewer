#pragma once

#include "Page.h"

namespace ui {

// Reusable modal confirmation dialog.
//
// Renders a centered box with title, content, and two buttons
// ("cancel" on the left, "confirm" on the right). Navigation:
//   UpLeft / DownRight — move focus between buttons
//   Enter (short BOOT) — activate focused button
//   Back (long BOOT)   — equivalent to Cancel
//
// On confirm, the user-supplied callback runs, then the dialog pops itself.
// On cancel, the dialog just pops itself.
class ConfirmDialog : public Page {
public:
    using ConfirmCallback = void (*)(::app::AppController& app, void* ctx);

    ConfirmDialog(const String& title,
                  const String& content,
                  const String& cancelText,
                  const String& confirmText,
                  ConfirmCallback onConfirm,
                  void* ctx)
        : title_(title),
          content_(content),
          cancelText_(cancelText),
          confirmText_(confirmText),
          onConfirm_(onConfirm),
          ctx_(ctx) {}

    void onEvent(::app::InputEvent e, ::app::AppController& app) override;
    void render(modules::DisplayModule& dm, UiCommon& ui) override;

private:
    String title_;
    String content_;
    String cancelText_;
    String confirmText_;
    ConfirmCallback onConfirm_ = nullptr;
    void*  ctx_                = nullptr;
    // false = cancel focused, true = confirm focused.
    bool focusConfirm_ = false;
};

} // namespace ui
