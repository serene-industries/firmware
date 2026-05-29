# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_BLE)
  target_sources(app PRIVATE bt_indicator.c)
  target_sources(app PRIVATE icebreaker_ble_name.c)
endif()

# USB serial number override - uses hardware ID for unique serial
if(CONFIG_USB_DEVICE_STACK)
  zephyr_library()
  zephyr_library_sources(usb_serial_number.c)
  zephyr_library_include_directories(${ZEPHYR_BASE}/subsys/usb/device)
endif()
