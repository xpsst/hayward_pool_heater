#include "base_frame.h"

#include "FrameClock.h"
#include "FrameConditions1.h"
#include "FrameConditions1B.h"
#include "FrameConditions2.h"
#include "FrameConditions2B.h"
#include "FrameConditionsD.h"
#include "FrameConf1.h"
#include "FrameConf2.h"
#include "FrameConf3.h"
#include "FrameConf4.h"
#include "FrameConf5.h"
#include "FrameConf6.h"

namespace esphome {
namespace hwp {

size_t BaseFrame::ensure_builtin_frame_classes_linked() {
    // Explicit references keep the self-registering decoder objects in ESP-IDF's static archive.
    const size_t* const class_type_ids[] = {
        &FrameClock::class_type_id,
        &FrameConditions1::class_type_id,
        &FrameConditions1B::class_type_id,
        &FrameConditions2::class_type_id,
        &FrameConditions2B::class_type_id,
        &FrameConditionsD::class_type_id,
        &FrameConf1::class_type_id,
        &FrameConf2::class_type_id,
        &FrameConf3::class_type_id,
        &FrameConf4::class_type_id,
        &FrameConf5::class_type_id,
        &FrameConf6::class_type_id,
    };

    const auto& registry = get_registry();
    size_t linked_count = 0;
    for (const size_t* class_type_id : class_type_ids) {
        if (*class_type_id < registry.size()) {
            linked_count++;
        }
    }
    return linked_count;
}

} // namespace hwp
} // namespace esphome
