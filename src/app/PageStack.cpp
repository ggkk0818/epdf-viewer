#include "PageStack.h"

namespace app {

void PageStack::push(ui::Page* p) {
    if (!p) return;
    if (count_ < MAX_DEPTH) {
        stack_[count_++] = p;
    }
}

ui::Page* PageStack::pop() {
    if (count_ == 0) return nullptr;
    return stack_[--count_];
}

ui::Page* PageStack::top() const {
    if (count_ == 0) return nullptr;
    return stack_[count_ - 1];
}

void PageStack::clear() {
    while (count_ > 0) {
        ui::Page* p = pop();
        if (p) delete p;
    }
}

} // namespace app
