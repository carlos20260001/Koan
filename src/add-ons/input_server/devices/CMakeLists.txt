# Input devices come from evdev: keyboards through the keyboard add-on, and
# mice, touchpads, tablets, touchscreens and other pointers through
# libinput in the mouse add-on. The Haiku-driver device add-ons (serial
# mouse, tablet, wacom, virtio, Bluetooth LE mouse) are replaced by it
# (PORTING.md, M9 W4 input_server add-ons).
add_subdirectory(keyboard)
add_subdirectory(mouse)
add_subdirectory(virtualkeyboard)
