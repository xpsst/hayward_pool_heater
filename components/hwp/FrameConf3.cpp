/**
 *
 * Copyright (c) 2024 S. Leclerc (sle118@hotmail.com)
 *
 * This file is part of the Pool Heater Controller component project.
 *
 * @project Pool Heater Controller Component
 * @developer S. Leclerc (sle118@hotmail.com)
 *
 * @license MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * @disclaimer Use at your own risk. The developer assumes no responsibility
 * for any damage or loss caused by the use of this software.
 */

#include "FrameConf3.h"
#include "CS.h"
#include "Schema.h"
#include <cmath>
namespace esphome {
namespace hwp {
CLASS_ID_DECLARATION(esphome::hwp::FrameConf3);
static constexpr char TAG[] = "hwp";
std::shared_ptr<BaseFrame> FrameConf3::create() {
    return std::make_shared<FrameConf3>(); // Create a FrameTemperature if type matches
}
// FRAME_ID_t FrameConf3::get_type() const { return FRAME_SETPOINT_LIMITS; }
const char* FrameConf3::type_string() const { return "CONFIG_3  "; }
bool FrameConf3::matches(BaseFrame& secialized, BaseFrame& base) {
    return base.packet.get_type() == FRAME_ID_CONF_3;
}
std::string FrameConf3::format(bool no_diff) const {
    if (!this->data_.has_value()) return "N/A";
    return this->format(*data_, (no_diff ? *data_ : prev_data_.value_or(*data_)));
}

std::string FrameConf3::format_prev() const {
    if (!this->prev_data_.has_value()) return "N/A";
    return this->format(this->prev_data_.value(), this->prev_data_.value());
}
void FrameConf3::traits(climate::ClimateTraits& traits, heat_pump_data_t& hp_data) {
    traits.set_visual_min_temperature(hp_data.get_min_target());
    traits.set_visual_max_temperature(hp_data.get_max_target());
    traits.set_visual_temperature_step(0.5);
}

std::string FrameConf3::format(const conf_3_t& val, const conf_3_t& ref) const {
    CS oss;
    bool changed = (val != ref);
    oss.set_changed_base_color(changed);

    oss << "[" << val.unknown_1.diff(ref.unknown_1, ", ")
        << val.unknown_2.diff(ref.unknown_2, ", ")
        << val.unknown_3.diff(ref.unknown_3, ", ")
        << val.unknown_4.diff(ref.unknown_4, ", ")
        << val.unknown_5.diff(ref.unknown_5, "] ") << "r08_min_cool_setpoint: "
        << val.r08_min_cool_setpoint.diff(ref.r08_min_cool_setpoint, ", ")
        << "r09_max_cooling_setpoint: "
        << val.r09_max_cooling_setpoint.diff(ref.r09_max_cooling_setpoint, ", ")
        << "r10_min_heating_setpoint: "
        << val.r10_min_heating_setpoint.diff(ref.r10_min_heating_setpoint, ", ")
        << "r11_max_heating_setpoint: "
        << val.r11_max_heating_setpoint.diff(ref.r11_max_heating_setpoint);
    return oss.str();
}
/**
 * @brief Parses the frame data and places it into the canonical
 *        heat_pump_data_t structure.
 *
 * The heat_pump_data_t is a canonical representation between the heat pump bus
 * data and the esphome heat pump component.
 * 
 * @param hp_data The heat_pump_data_t structure to fill
 * @see heat_pump_data_t
 * @note current elements identified in this frame are setpoint limits
 */
void FrameConf3::parse(heat_pump_data_t& hp_data) {
    hp_data.r08_min_cool_setpoint = data_->r08_min_cool_setpoint.decode();
    hp_data.r09_max_cooling_setpoint = data_->r09_max_cooling_setpoint.decode();
    hp_data.r10_min_heating_setpoint = data_->r10_min_heating_setpoint.decode();
    hp_data.r11_max_heating_setpoint = data_->r11_max_heating_setpoint.decode();

    const float observed_r11 = data_->r11_max_heating_setpoint.decode();
    if (hp_data.xps100_pc1001_detected) {
        ESP_LOGD(TAG, "XPS100 debug: CONFIG_3 (%s) R11 max heating %.1fC (raw 0x%02X)",
            this->source_string(), observed_r11, data_->r11_max_heating_setpoint.raw);
    }
    if (pending_xps100_r11_.has_value() && this->get_source() == SOURCE_HEATER) {
        if (std::fabs(observed_r11 - pending_xps100_r11_.value()) < 0.01f) {
            ESP_LOGI(TAG, "XPS100 R11 heater echo confirmed at %.1fC", observed_r11);
            pending_xps100_r11_.reset();
            pending_xps100_r11_mismatch_logged_ = false;
        } else if (!pending_xps100_r11_mismatch_logged_) {
            ESP_LOGW(TAG, "XPS100 R11 waiting for %.1fC heater echo; observed %.1fC",
                pending_xps100_r11_.value(), observed_r11);
            pending_xps100_r11_mismatch_logged_ = true;
        }
    }

    active_modes_t active_mode = STATE_HEATING_MODE;

    // Manually check the type and cast if it's FrameTemperature
    if (hp_data.mode.has_value() && hp_data.mode.value() == climate::CLIMATE_MODE_OFF) {
        active_mode = STATE_HEATING_MODE;
    }

    // Manually check the type and cast if it's FrameConf3
    auto min_heating_setpoint = data_->r10_min_heating_setpoint.decode();
    auto max_heating_setpoint = data_->r11_max_heating_setpoint.decode();
    auto min_cooling_setpoint = data_->r08_min_cool_setpoint.decode();
    auto max_cooling_setpoint = data_->r09_max_cooling_setpoint.decode();
    bits_details_t r08_bits;
    r08_bits.raw = data_->r08_min_cool_setpoint.raw;
    bits_details_t r09_bits;
    r09_bits.raw = data_->r09_max_cooling_setpoint.raw;
    bits_details_t r10_bits;
    r10_bits.raw = data_->r10_min_heating_setpoint.raw;
    bits_details_t r11_bits;
    r11_bits.raw = data_->r11_max_heating_setpoint.raw;

    switch (active_mode) {
    case STATE_HEATING_MODE:
        hp_data.min_target_temperature = min_heating_setpoint;
        hp_data.max_target_temperature = max_heating_setpoint;
        break;
    case STATE_COOLING_MODE:
        hp_data.min_target_temperature = min_cooling_setpoint;
        hp_data.max_target_temperature = max_cooling_setpoint;
        break;
    default:
        hp_data.min_target_temperature = min_cooling_setpoint;
        hp_data.max_target_temperature = max_heating_setpoint;
    }
}
optional<std::shared_ptr<BaseFrame>> FrameConf3::control(const HWPCall& call) {
    if (!call.xps100_r11_max_heating_setpoint.has_value()) return nullopt;

    if (!call.hp_data.xps100_pc1001_detected) {
        ESP_LOGW(TAG, "XPS100 R11 control rejected: PC1001 signature not detected");
        call.component.status_momentary_warning("PC1001 signature not detected", 5000);
        return nullopt;
    }
    if (!this->data_.has_value() || this->packet.data_len != frame_data_length) {
        ESP_LOGW(TAG, "XPS100 R11 control rejected: waiting for full CONFIG_3 frame");
        call.component.status_momentary_warning("Waiting for CONFIG_3 heater state", 5000);
        return nullopt;
    }

    const float requested = call.xps100_r11_max_heating_setpoint.value();
    const float half_steps = requested / XPS100_R11_STEP_C;
    const bool valid_step = std::fabs(half_steps - std::round(half_steps)) < 0.01f;
    if (requested < XPS100_R11_MIN_C || requested > XPS100_R11_MAX_C || !valid_step) {
        ESP_LOGE(TAG,
            "XPS100 R11 control rejected: %.2fC must be 35.0C to 45.0C in 0.5C steps",
            requested);
        call.component.status_momentary_warning("XPS100 R11 must be 35.0C to 45.0C", 5000);
        return nullopt;
    }

    FrameConf3 command_frame(*this);
    const float previous = command_frame.data().r11_max_heating_setpoint.decode();
    command_frame.data().r11_max_heating_setpoint = requested;
    if (!command_frame.is_changed()) {
        ESP_LOGD(TAG, "XPS100 R11 control: %.1fC already active", requested);
        return nullopt;
    }

    // Preserve every received CONFIG_3 byte except R11; finalize recalculates the checksum.
    command_frame.finalize();
    this->pending_xps100_r11_ = requested;
    this->pending_xps100_r11_mismatch_logged_ = false;
    ESP_LOGI(TAG,
        "XPS100 R11 control: %.1fC -> %.1fC (raw 0x%02X), waiting for heater echo",
        previous, requested, command_frame.data().r11_max_heating_setpoint.raw);
    command_frame.print("TXQ", TAG, ESPHOME_LOG_LEVEL_VERBOSE, __LINE__);
    return optional<std::shared_ptr<FrameConf3>>{
        std::make_shared<FrameConf3>(command_frame)};
}
} // namespace hwp
} // namespace esphome
REGISTER_FRAME_ID_DEFAULT(esphome::hwp::FrameConf3);
