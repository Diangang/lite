# drivers/base Mapping Ledger (Auto-Generated)

Lite scope: `/data25/lidg/lite/drivers/base`
Linux ref: `/data25/lidg/lite/linux2.6/drivers/base`

## drivers/base/base.h
- function: 8 (NO_DIRECT_LINUX_MATCH: 8)
- struct: 1 (NO_DIRECT_LINUX_MATCH: 0)
- global: 0 (NO_DIRECT_LINUX_MATCH: 0)
- NO_DIRECT_LINUX_MATCH functions (top 20):
  - bus_devices_klist (line 16)
  - bus_devices_kobj (line 36)
  - bus_devices_kset (line 21)
  - bus_drivers_autoprobe (line 48)
  - bus_drivers_klist (line 31)
  - bus_drivers_kobj (line 42)
  - bus_drivers_kset (line 26)
  - bus_set_drivers_autoprobe (line 53)

## drivers/base/bus.c
- function: 26 (NO_DIRECT_LINUX_MATCH: 23)
- struct: 0 (NO_DIRECT_LINUX_MATCH: 0)
- global: 1 (NO_DIRECT_LINUX_MATCH: 0)
- NO_DIRECT_LINUX_MATCH functions (top 20):
  - bus_at (line 373)
  - bus_attr_show_drivers_autoprobe (line 90)
  - bus_attr_store_drivers_autoprobe (line 97)
  - bus_attr_store_drivers_probe (line 162)
  - bus_count (line 364)
  - bus_default_match (line 275)
  - bus_device_from_node (line 14)
  - bus_device_klist_get (line 24)
  - bus_device_klist_put (line 31)
  - bus_driver_from_node (line 19)
  - bus_driver_klist_get (line 38)
  - bus_driver_klist_put (line 45)
  - bus_emit_text_line (line 65)
  - bus_find (line 385)
  - bus_kobj (line 52)
  - bus_probe_device_modalias (line 137)
  - bus_probe_device_name (line 116)
  - bus_release_kobj (line 82)
  - bus_sysfs_register_subdirs (line 241)
  - bus_sysfs_show (line 206)

## drivers/base/class.c
- function: 10 (NO_DIRECT_LINUX_MATCH: 1)
- struct: 0 (NO_DIRECT_LINUX_MATCH: 0)
- global: 1 (NO_DIRECT_LINUX_MATCH: 0)
- NO_DIRECT_LINUX_MATCH functions (top 20):
  - classes_kset_get (line 90)

## drivers/base/core.c
- function: 37 (NO_DIRECT_LINUX_MATCH: 25)
- struct: 0 (NO_DIRECT_LINUX_MATCH: 0)
- global: 1 (NO_DIRECT_LINUX_MATCH: 0)
- NO_DIRECT_LINUX_MATCH functions (top 20):
  - device_attr_is_visible (line 186)
  - device_attr_show_dev (line 142)
  - device_attr_show_modalias (line 148)
  - device_attr_show_type (line 136)
  - device_default_release (line 41)
  - device_is_bus_view_hidden (line 282)
  - device_kobj_parent (line 271)
  - device_rebind (line 410)
  - device_release_kobj (line 20)
  - device_set_parent (line 330)
  - device_sysfs_add_links (line 287)
  - device_sysfs_init_ktype (line 235)
  - device_sysfs_remove_links (line 308)
  - device_sysfs_show (line 214)
  - device_sysfs_store (line 223)
  - device_unbind (line 349)
  - devices_kset_get (line 14)
  - find_device_by_name (line 510)
  - kobject_child_add (line 242)
  - kobject_child_del (line 251)

## drivers/base/devtmpfs.c
- function: 20 (NO_DIRECT_LINUX_MATCH: 8)
- struct: 1 (NO_DIRECT_LINUX_MATCH: 1)
- global: 8 (NO_DIRECT_LINUX_MATCH: 5)
- NO_DIRECT_LINUX_MATCH functions (top 20):
  - devtmpfs_fill_super (line 361)
  - devtmpfs_get_console (line 304)
  - devtmpfs_get_sb (line 232)
  - devtmpfs_get_tty (line 309)
  - devtmpfs_submit_req (line 286)
  - find_last_slash (line 45)
  - find_slash (line 35)
  - lookup_child (line 57)

## drivers/base/driver.c
- function: 14 (NO_DIRECT_LINUX_MATCH: 8)
- struct: 0 (NO_DIRECT_LINUX_MATCH: 0)
- global: 5 (NO_DIRECT_LINUX_MATCH: 5)
- NO_DIRECT_LINUX_MATCH functions (top 20):
  - driver_attr_is_visible (line 17)
  - driver_bind_device (line 217)
  - driver_deferred_probe_remove (line 144)
  - driver_deferred_probe_remove_index (line 158)
  - driver_sysfs_show (line 67)
  - driver_sysfs_store (line 77)
  - ktype_driver_get (line 306)
  - sysfs_find_driver_bus_device (line 50)

## drivers/base/init.c
- function: 1 (NO_DIRECT_LINUX_MATCH: 0)
- struct: 0 (NO_DIRECT_LINUX_MATCH: 0)
- global: 0 (NO_DIRECT_LINUX_MATCH: 0)

## drivers/base/platform.c
- function: 11 (NO_DIRECT_LINUX_MATCH: 7)
- struct: 0 (NO_DIRECT_LINUX_MATCH: 0)
- global: 0 (NO_DIRECT_LINUX_MATCH: 0)
- NO_DIRECT_LINUX_MATCH functions (top 20):
  - platform_bus_match (line 53)
  - platform_bus_release (line 11)
  - platform_driver_probe (line 77)
  - platform_driver_register (line 102)
  - platform_driver_remove (line 90)
  - platform_get_platform_device (line 36)
  - platform_match_one (line 46)

## drivers/base/uevent.c
- function: 12 (NO_DIRECT_LINUX_MATCH: 12)
- struct: 0 (NO_DIRECT_LINUX_MATCH: 0)
- global: 3 (NO_DIRECT_LINUX_MATCH: 2)
- NO_DIRECT_LINUX_MATCH functions (top 20):
  - buf_append (line 12)
  - buf_append_ch (line 25)
  - buf_append_u32_dec (line 36)
  - buf_append_u32_oct (line 43)
  - device_get_devpath (line 106)
  - device_get_modalias (line 129)
  - device_get_sysfs_path (line 113)
  - device_uevent_emit (line 218)
  - device_uevent_read (line 319)
  - device_uevent_seqnum (line 331)
  - kobject_build_path (line 74)
  - u32_to_hex_fixed (line 64)

## drivers/base/virtual.c
- function: 4 (NO_DIRECT_LINUX_MATCH: 4)
- struct: 1 (NO_DIRECT_LINUX_MATCH: 1)
- global: 1 (NO_DIRECT_LINUX_MATCH: 1)
- NO_DIRECT_LINUX_MATCH functions (top 20):
  - ensure_virtual_root_device (line 36)
  - match_child_name (line 13)
  - virtual_child_device (line 24)
  - virtual_root_device (line 53)

