#include "ConfirmDialog.h"
#include "../app/AppController.h"
#include "../config/Config.h"

namespace ui {

namespace {

constexpr uint16_t COLOR_BG = GxEPD_WHITE;
constexpr uint16_t COLOR_FG = GxEPD_BLACK;

// Layout, in pixels. The dialog is centered in the 640x960 viewport.
constexpr uint16_t DIALOG_W       = 240;
constexpr uint16_t DIALOG_H       = 200;
constexpr uint16_t DIALOG_X       = (cfg::display::WIDTH - DIALOG_W) / 2;
constexpr uint16_t DIALOG_Y       = (cfg::display::HEIGHT - DIALOG_H) / 2;
constexpr uint16_t DIALOG_RADIUS  = 6;
constexpr uint16_t DIALOG_BORDER  = 2;

constexpr uint16_t PAD_X          = 12;
constexpr uint16_t TITLE_Y_OFFSET = 10;       // baseline relative to dialog top
constexpr uint16_t TITLE_H        = 18;
constexpr uint16_t CONTENT_TOP    = DIALOG_Y + TITLE_H + 4;
constexpr uint16_t CONTENT_H      = DIALOG_H - TITLE_H - 4 - 44; // leave room for buttons
constexpr uint16_t BTN_H          = 28;
constexpr uint16_t BTN_Y          = DIALOG_Y + DIALOG_H - BTN_H - 10;
constexpr uint16_t BTN_GAP        = 8;
constexpr uint16_t BTN_W          = (DIALOG_W - PAD_X * 2 - BTN_GAP) / 2;

} // namespace

void ConfirmDialog::onEvent(::app::InputEvent e, ::app::AppController& app) {
    bool changed = false;
    bool confirm = false;
    ConfirmCallback cb = nullptr;
    void* cbCtx = nullptr;
    bool closeDialog = false;

    app.mutateUiState([&] {
        switch (e) {
            case ::app::InputEvent::UpLeft:
            case ::app::InputEvent::DownRight:
                focusConfirm_ = !focusConfirm_;
                changed = true;
                break;
            case ::app::InputEvent::Enter:
                if (focusConfirm_ && onConfirm_) {
                    confirm = true;
                    cb = onConfirm_;
                    cbCtx = ctx_;
                }
                closeDialog = true;
                break;
            case ::app::InputEvent::Back:
                closeDialog = true;
                break;
            default: break;
        }
    });

    if (changed) {
        app.requestRender();
        return;
    }
    if (confirm && cb) {
        cb(app, cbCtx);
    }
    if (closeDialog) {
        app.popPage();
    }
}

void ConfirmDialog::render(modules::DisplayModule& dm, UiCommon& /*ui*/) {
    auto& g = dm.gfx();
    auto& f = dm.fonts();

    // Dim the surrounding area so the modal reads as overlay on e-ink.
    g.fillRect(0, 0, cfg::display::WIDTH, cfg::display::HEIGHT, COLOR_FG);

    // Dialog body.
    g.fillRect(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H, COLOR_BG);
    g.drawRoundRect(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H, DIALOG_RADIUS, COLOR_FG);
    // Re-assert a thicker border (drawRoundRect is 1px).
    for (uint16_t i = 1; i < DIALOG_BORDER; i++) {
        g.drawRoundRect(DIALOG_X - i, DIALOG_Y - i,
                        DIALOG_W + 2 * i, DIALOG_H + 2 * i,
                        DIALOG_RADIUS + i, COLOR_FG);
    }

    f.setFont(u8g2_font_wqy12_t_gb2312);
    f.setForegroundColor(COLOR_FG);
    f.setBackgroundColor(COLOR_BG);

    // Title (single line, left-aligned).
    f.setCursor(DIALOG_X + PAD_X, DIALOG_Y + TITLE_Y_OFFSET + 8);
    f.print(title_);

    // Separator under title.
    g.drawLine(DIALOG_X + PAD_X, DIALOG_Y + TITLE_H,
               DIALOG_X + DIALOG_W - PAD_X, DIALOG_Y + TITLE_H, COLOR_FG);

    // Content — wrap by UTF-8 character until line width is exhausted.
    {
        const uint16_t maxW = DIALOG_W - PAD_X * 2;
        uint16_t lineY = CONTENT_TOP + 12;
        const size_t n = content_.length();
        size_t i = 0;
        while (i < n) {
            uint16_t acc = 0;
            size_t k = i;
            while (k < n) {
                uint8_t c = (uint8_t)content_[k];
                size_t step = 1;
                if      ((c & 0x80) == 0x00) step = 1;
                else if ((c & 0xE0) == 0xC0) step = 2;
                else if ((c & 0xF0) == 0xE0) step = 3;
                else if ((c & 0xF8) == 0xF0) step = 4;

                String single = content_.substring(k, k + step);
                uint16_t cw = f.getUTF8Width(single.c_str());
                if (acc + cw > maxW) break;
                acc += cw;
                k += step;
            }
            if (k == i) k = i + 1; // safety: never loop forever

            f.setCursor(DIALOG_X + PAD_X, lineY);
            f.print(content_.substring(i, k));
            lineY += 14;
            i = k;
            if (lineY > CONTENT_TOP + CONTENT_H - 4) break;
        }
    }

    // Buttons.
    auto drawButton = [&](uint16_t x, const String& text, bool focused) {
        if (focused) {
            g.fillRect(x, BTN_Y, BTN_W, BTN_H, COLOR_FG);
            f.setForegroundColor(COLOR_BG);
            f.setBackgroundColor(COLOR_FG);
        } else {
            g.drawRect(x, BTN_Y, BTN_W, BTN_H, COLOR_FG);
            f.setForegroundColor(COLOR_FG);
            f.setBackgroundColor(COLOR_BG);
        }
        uint16_t tw = f.getUTF8Width(text.c_str());
        f.setCursor(x + (BTN_W - tw) / 2, BTN_Y + BTN_H / 2 + 4);
        f.print(text);
    };

    uint16_t cancelX  = DIALOG_X + PAD_X;
    uint16_t confirmX = cancelX + BTN_W + BTN_GAP;
    drawButton(cancelX,  cancelText_,  !focusConfirm_);
    drawButton(confirmX, confirmText_,  focusConfirm_);

    f.setForegroundColor(COLOR_FG);
    f.setBackgroundColor(COLOR_BG);
}

} // namespace ui
