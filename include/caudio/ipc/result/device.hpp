#pragma once

/**
 * @file device.hpp
 * @brief Audio output device results for the caudio IPC protocol.
 * @ingroup caudio_ipc
 */

#include <caudio/types/record.hpp>
#include <vector>

namespace caudio::ipc {

// ---------------------------------------------------------------------------
// Device Results
// ---------------------------------------------------------------------------

/**
 * @brief Audio output device information.
 * @ingroup caudio_ipc
 * @details Same-type alias of `caudio::types::DeviceInfo`; kept for IPC source
 * compatibility. JSON shape is unchanged.
 *
 * @json_example {"id": "hw:0,0", "name": "Built-in Audio", "isDefault": true}
 *
 * @param id Device identifier.
 * @param name Human-readable device name.
 * @param isDefault Whether this is the default device.
 */
using DeviceInfo = caudio::types::DeviceInfo;

/**
 * @struct Devices
 * @brief List of audio output devices (returned by DeviceList).
 * @ingroup caudio_ipc
 *
 * @json_example {"type": "Devices", "devices": [{"id": "hw:0,0", "name": "Built-in Audio",
 * "isDefault": true}, ...]}
 *
 * @param devices Vector of device information.
 */
struct Devices final {
    /**
     * @brief Vector of device information.
     */
    std::vector<DeviceInfo> devices{};
};

} // namespace caudio::ipc
