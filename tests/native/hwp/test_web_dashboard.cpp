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
 * copies or substantial portions of the Software.
 */

#include "hwp_web_dashboard.h"
#include "protocol_core.h"

#include <array>
#include <cassert>
#include <cstring>
#include <string>

namespace hwp = esphome::hwp;
namespace protocol = esphome::hwp::protocol;

using Packet = std::array<uint8_t, protocol::FRAME_DATA_LENGTH>;

hwp::BaseFrame make_frame(const Packet& packet, hwp::frame_source_t source) {
    hwp::BaseFrame frame;
    std::memcpy(frame.packet.data, packet.data(), packet.size());
    frame.packet.data_len = packet.size();
    frame.set_source(source);
    assert(frame.packet.is_checksum_valid());
    return frame;
}

void assert_contains(const std::string& value, const std::string& expected) {
    assert(value.find(expected) != std::string::npos);
}

void test_packet_ring_buffer_and_changed_bytes() {
    hwp::HWPWebDashboard dashboard;
    dashboard.configure(hwp::HWPWebConfig{true, "/hwp", 1, 4});
    auto first = make_frame(
        Packet{0x82, 0xB1, 0x26, 0x13, 0x56, 0x50, 0x10, 0x1E, 0x03, 0x01, 0x64, 0xA8},
        hwp::SOURCE_HEATER);
    auto second = make_frame(
        Packet{0x82, 0xB1, 0x26, 0x38, 0x56, 0x50, 0x10, 0x1E, 0x03, 0x01, 0x64, 0xCD},
        hwp::SOURCE_HEATER);

    dashboard.record_packet(first, "Same");
    dashboard.record_packet(second, "Chg");

    assert(dashboard.packet_count() == 1);
    assert(dashboard.latest_frame_count() == 1);
    const auto json = dashboard.state_json();
    assert_contains(json, "\"frames\":[");
    assert_contains(json, "\"kind\":\"Chg\"");
    assert_contains(json, "\"checksum_valid\":true");
    assert_contains(json, "\"changed_bytes\":[false,false,false,true");
}

void test_immediate_packet_repeats_are_collapsed() {
    hwp::HWPWebDashboard dashboard;
    dashboard.configure(hwp::HWPWebConfig{true, "/hwp", 4, 4});
    auto frame = make_frame(
        Packet{0x86, 0xB1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x37},
        hwp::SOURCE_HEATER);

    dashboard.record_packet(frame, "Same");
    dashboard.record_packet(frame, "Same");
    dashboard.record_packet(frame, "Same");

    assert(dashboard.packet_count() == 1);
    const auto json = dashboard.state_json();
    assert_contains(json, "\"frame\":\"0x86\"");
    assert(json.find("\"packets\":[{") != std::string::npos);
    assert(json.find("},{", json.find("\"packets\":[")) == std::string::npos);
}

void test_field_snapshot_and_graph_trim() {
    hwp::HWPWebDashboard dashboard;
    dashboard.configure(hwp::HWPWebConfig{true, "/hwp", 4, 2});
    hwp::heat_pump_data_t data;

    data.t02_temperature_inlet = 12.5f;
    data.t04_temperature_coil = 4.0f;
    data.t_aux_cond2_temperature = 20.0f;
    dashboard.update_fields(data, "Connected", hwp::BUSMODE_RX);
    const auto first_json = dashboard.state_json();
    data.t02_temperature_inlet = 13.0f;
    dashboard.update_fields(data, "Connected", hwp::BUSMODE_RX);
    const auto unchanged_json = dashboard.state_json();
    data.t02_temperature_inlet = 13.5f;
    dashboard.update_fields(data, "Connected", hwp::BUSMODE_RX);

    assert(dashboard.graph_point_count("t02_inlet") == 2);
    const auto json = dashboard.state_json();
    assert_contains(json, "\"label\":\"T02 Inlet\"");
    assert_contains(json, "\"id\":\"t_aux_cond2\"");
    assert_contains(json, "\"label\":\"COND_2 Auxiliary\"");
    assert_contains(json, "\"value\":\"13.5\"");
    assert_contains(json, "\"changed\":true");
    assert_contains(json, "\"bus_mode\":\"RX\"");
    assert_contains(json, "\"graphs\":{}");
    const auto first_seen = first_json.find("\"seen_ms\":");
    assert(first_seen != std::string::npos);
    assert(unchanged_json.find(first_json.substr(first_seen, 20)) != std::string::npos);

    const auto graphs = dashboard.graph_state_json();
    assert_contains(graphs, "\"graphs\":{");
    assert_contains(graphs, "\"t02_inlet\":[");
    assert_contains(graphs, "\"value\":13.5");

    const auto event_json = dashboard.event_json();
    assert_contains(event_json, "\"packet_sequence\":");
    assert(event_json.find("\"fields\"") == std::string::npos);
    assert(event_json.find("\"packets\"") == std::string::npos);
    assert(event_json.find("\"graphs\"") == std::string::npos);
}

void test_field_snapshot_retains_previous_fields_on_partial_update() {
    hwp::HWPWebDashboard dashboard;
    dashboard.configure(hwp::HWPWebConfig{true, "/hwp", 4, 4});
    hwp::heat_pump_data_t data;

    data.t02_temperature_inlet = 12.5f;
    data.t04_temperature_coil = 4.0f;
    dashboard.update_fields(data, "Connected", hwp::BUSMODE_RX);

    hwp::heat_pump_data_t partial;
    partial.t02_temperature_inlet = 13.0f;
    dashboard.update_fields(partial, "", hwp::BUSMODE_RX);

    const auto json = dashboard.state_json();
    assert_contains(json, "\"status\":\"Connected\"");
    assert_contains(json, "\"id\":\"t02_inlet\"");
    assert_contains(json, "\"value\":\"13.0\"");
    assert_contains(json, "\"id\":\"t04_coil\"");
    assert_contains(json, "\"value\":\"4.0\"");
    assert(dashboard.graph_point_count("t02_inlet") == 2);
    assert(dashboard.graph_point_count("t04_coil") == 1);
}

void test_loxone_state_snapshot() {
    hwp::HWPWebDashboard dashboard;
    dashboard.configure(hwp::HWPWebConfig{true, "/hwp", 4, 4, true});
    hwp::heat_pump_data_t data;
    data.xps100_pc1001_detected = true;
    data.mode = esphome::climate::CLIMATE_MODE_HEAT;
    data.action = esphome::climate::CLIMATE_ACTION_HEATING;
    data.S02_water_flow = hwp::FlowMeterEnable::Enabled;
    data.t02_temperature_inlet = 28.5f;
    data.t03_temperature_outlet = 29.5f;
    data.t04_temperature_coil = 8.0f;
    data.t05_temperature_ambient = 18.5f;
    data.target_temperature = 31.0f;
    data.r11_max_heating_setpoint = 42.0f;
    data.last_heater_frame = millis();

    dashboard.update_loxone_state(data, "S00", true, false);
    const auto json = dashboard.loxone_state_json();
    assert_contains(json, "\"online\":1");
    assert_contains(json, "\"control_enabled\":0");
    assert_contains(json, "\"xps100_detected\":1");
    assert_contains(json, "\"mode_code\":1");
    assert_contains(json, "\"mode_name\":\"HEAT\"");
    assert_contains(json, "\"action_code\":2");
    assert_contains(json, "\"action_name\":\"HEATING\"");
    assert_contains(json, "\"water_flow\":1");
    assert_contains(json, "\"fault\":0");
    assert_contains(json, "\"heater_status_code\":\"S00\"");
    assert_contains(json, "\"current_temperature\":28.5");
    assert_contains(json, "\"outlet_temperature\":29.5");
    assert_contains(json, "\"ambient_temperature\":18.5");
    assert_contains(json, "\"coil_temperature\":8");
    assert_contains(json, "\"target_temperature\":31");
    assert_contains(json, "\"max_heating_temperature\":42");
}

void test_loxone_control_parser_and_callback() {
    hwp::HWPWebDashboard dashboard;
    dashboard.configure(hwp::HWPWebConfig{true, "/hwp", 4, 4, true});
    bool callback_called = false;
    dashboard.set_loxone_control_callback(
        [&callback_called](const hwp::HWPLoxoneControlRequest& request) {
            callback_called = true;
            assert(request.mode == esphome::climate::CLIMATE_MODE_HEAT);
            assert(request.target_temperature.has_value());
            assert(request.target_temperature.value() == 31.5f);
            return hwp::HWPLoxoneControlResult{202, "{\"accepted\":true}"};
        });

    auto result = dashboard.handle_loxone_control(std::string("HEAT"), std::string("31.5"));
    assert(callback_called);
    assert(result.status_code == 202);
    assert_contains(result.body, "\"accepted\":true");

    result = dashboard.handle_loxone_control(std::string("boost"), esphome::nullopt);
    assert(result.status_code == 400);
    assert_contains(result.body, "invalid_mode");
    result = dashboard.handle_loxone_control(esphome::nullopt, std::string("31C"));
    assert(result.status_code == 400);
    assert_contains(result.body, "invalid_target");
    callback_called = false;
    result = dashboard.handle_loxone_control(std::string("heat"), std::string("31,5"));
    assert(callback_called);
    assert(result.status_code == 202);
    result = dashboard.handle_loxone_control(esphome::nullopt, esphome::nullopt);
    assert(result.status_code == 400);
    assert_contains(result.body, "missing_command");
}

void test_index_html_contains_annotation_helper() {
    const std::string html = hwp::HWPWebDashboard::index_html();
    assert_contains(html, "Values");
    assert_contains(html, "Frames");
    assert_contains(html, "Packets");
    assert_contains(html, "Graphs");
    assert_contains(html, "Recent graph history is retained on the device");
    assert_contains(html, "graphs.json");
    assert_contains(html, "setInterval(refresh,2500)");
    assert_contains(html, "function uptime()");
    assert_contains(html, "state.local_sync_ms=Date.now()");
    assert_contains(html, "hold:true");
    assert_contains(html, "visible window");
    assert_contains(html, "devicePixelRatio");
    assert_contains(html, "Setpoints And Limits");
    assert_contains(html, "Differentials And Timers");
    assert_contains(html, "Fan Values");
    assert_contains(html, "Annotate");
    assert_contains(html, "class=annotate");
    assert_contains(html, "position:fixed");
    assert_contains(html, "Start");
    assert_contains(html, "Mark Event");
    assert_contains(html, "Export JSON");
    assert_contains(html, "hwp.web.annotations.v1");
    assert_contains(html, "localStorage");
    assert_contains(html, "hwp-web-annotations.json");
}

int main() {
    test_packet_ring_buffer_and_changed_bytes();
    test_immediate_packet_repeats_are_collapsed();
    test_field_snapshot_and_graph_trim();
    test_field_snapshot_retains_previous_fields_on_partial_update();
    test_loxone_state_snapshot();
    test_loxone_control_parser_and_callback();
    test_index_html_contains_annotation_helper();
    return 0;
}
