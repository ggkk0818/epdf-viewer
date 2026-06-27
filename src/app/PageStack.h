#pragma once

#include <Arduino.h>
#include "../ui/Page.h"

namespace app {

class PageStack {
public:
    void push(ui::Page* p);
    ui::Page* pop();
    ui::Page* top() const;
    size_t size() const { return count_; }
    void clear();

private:
    static constexpr size_t MAX_DEPTH = 8;
    ui::Page* stack_[MAX_DEPTH] = {};
    size_t count_ = 0;
};

} // namespace app
