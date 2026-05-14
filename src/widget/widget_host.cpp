#include "widget/widget_host.h"

void WidgetHost::AddWidgetAnimation(WidgetAnimationPtr animation) {
    if (animation == nullptr) {
        return;
    }
    WidgetAnimationStatePtr target = animation->TargetState();
    if (target == nullptr) {
        return;
    }
    animation->Draw(Renderer(), *target);
}
