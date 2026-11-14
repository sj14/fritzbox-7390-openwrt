# The setup network that an image without AVM's LAN firmware brings up at its
# first start (/etc/board.d/02_network), the only way onto a box whose lan0 can't
# start, until avm-firmware-install removes it. Its key is published in the
# README. The SSID ends with the last four hex digits of the LAN address.
SETUP_SSID_PREFIX=OpenWrt-7390-Setup-
SETUP_KEY=fritzbox7390-setup
