/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android-base/logging.h>

#include "hidl_return_util.h"
#include "hidl_struct_util.h"
#include "wifi_ap_iface.h"
#include "wifi_status_util.h"

namespace android {
namespace hardware {
namespace wifi {
namespace V1_4 {
namespace implementation {
using hidl_return_util::validateAndCall;

WifiApIface::WifiApIface(
    const std::string& ifname,
    const std::weak_ptr<legacy_hal::WifiLegacyHal> legacy_hal,
    const std::weak_ptr<iface_util::WifiIfaceUtil> iface_util)
    : ifname_(ifname),
      legacy_hal_(legacy_hal),
      iface_util_(iface_util),
      is_valid_(true) {}

void WifiApIface::invalidate() {
    legacy_hal_.reset();
    is_valid_ = false;
}

bool WifiApIface::isValid() { return is_valid_; }

std::string WifiApIface::getName() { return ifname_; }

Return<void> WifiApIface::getName(getName_cb hidl_status_cb) {
    return validateAndCall(this, WifiStatusCode::ERROR_WIFI_IFACE_INVALID,
                           &WifiApIface::getNameInternal, hidl_status_cb);
}

Return<void> WifiApIface::getType(getType_cb hidl_status_cb) {
    return validateAndCall(this, WifiStatusCode::ERROR_WIFI_IFACE_INVALID,
                           &WifiApIface::getTypeInternal, hidl_status_cb);
}

Return<void> WifiApIface::setCountryCode(const hidl_array<int8_t, 2>& code,
                                         setCountryCode_cb hidl_status_cb) {
    return validateAndCall(this, WifiStatusCode::ERROR_WIFI_IFACE_INVALID,
                           &WifiApIface::setCountryCodeInternal, hidl_status_cb,
                           code);
}

Return<void> WifiApIface::getValidFrequenciesForBand(
    V1_0::WifiBand band, getValidFrequenciesForBand_cb hidl_status_cb) {
    return validateAndCall(this, WifiStatusCode::ERROR_WIFI_IFACE_INVALID,
                           &WifiApIface::getValidFrequenciesForBandInternal,
                           hidl_status_cb, band);
}

Return<void> WifiApIface::setMacAddress(const hidl_array<uint8_t, 6>& mac,
                                        setMacAddress_cb hidl_status_cb) {
    return validateAndCall(this, WifiStatusCode::ERROR_WIFI_IFACE_INVALID,
                           &WifiApIface::setMacAddressInternal, hidl_status_cb,
                           mac);
}

Return<void> WifiApIface::getFactoryMacAddress(
    getFactoryMacAddress_cb hidl_status_cb) {
    return validateAndCall(this, WifiStatusCode::ERROR_WIFI_IFACE_INVALID,
                           &WifiApIface::getFactoryMacAddressInternal,
                           hidl_status_cb);
}

std::pair<WifiStatus, std::string> WifiApIface::getNameInternal() {
    return {createWifiStatus(WifiStatusCode::SUCCESS), ifname_};
}

std::pair<WifiStatus, IfaceType> WifiApIface::getTypeInternal() {
    return {createWifiStatus(WifiStatusCode::SUCCESS), IfaceType::AP};
}

WifiStatus WifiApIface::setCountryCodeInternal(
    const std::array<int8_t, 2>& code) {
    legacy_hal::wifi_error legacy_status =
        legacy_hal_.lock()->setCountryCode(ifname_, code);
    return createWifiStatusFromLegacyError(legacy_status);
}

std::pair<WifiStatus, std::vector<WifiChannelInMhz>>
WifiApIface::getValidFrequenciesForBandInternal(V1_0::WifiBand band) {
    static_assert(sizeof(WifiChannelInMhz) == sizeof(uint32_t),
                  "Size mismatch");
    legacy_hal::wifi_error legacy_status;
    std::vector<uint32_t> valid_frequencies;
    std::tie(legacy_status, valid_frequencies) =
        legacy_hal_.lock()->getValidFrequenciesForBand(
            ifname_, hidl_struct_util::convertHidlWifiBandToLegacy(band));
    return {createWifiStatusFromLegacyError(legacy_status), valid_frequencies};
}

WifiStatus WifiApIface::setMacAddressInternal(
    const std::array<uint8_t, 6>& mac) {
    bool status = iface_util_.lock()->setMacAddress(ifname_, mac);
    if (!status) {
        return createWifiStatus(WifiStatusCode::ERROR_UNKNOWN);
    }
    return createWifiStatus(WifiStatusCode::SUCCESS);
}

std::pair<WifiStatus, std::array<uint8_t, 6>>
WifiApIface::getFactoryMacAddressInternal() {
    std::array<uint8_t, 6> mac =
        iface_util_.lock()->getFactoryMacAddress(ifname_);
    if (mac[0] == 0 && mac[1] == 0 && mac[2] == 0 && mac[3] == 0 &&
        mac[4] == 0 && mac[5] == 0) {
        return {createWifiStatus(WifiStatusCode::ERROR_UNKNOWN), mac};
    }
    return {createWifiStatus(WifiStatusCode::SUCCESS), mac};
}
}  // namespace implementation
}  // namespace V1_4
}  // namespace wifi
}  // namespace hardware
}  // namespace android
