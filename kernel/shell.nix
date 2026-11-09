{ pkgs ? import ./nixpkgs.nix }:

let
  mips = pkgs.pkgsCross.mips-linux-gnu;
in
pkgs.mkShell {
  packages = [
    mips.pkgsStatic.stdenv.cc
    mips.pkgsStatic.stdenv.cc.bintools
    pkgs.bc
    pkgs.bison
    pkgs.curl
    pkgs.flex
    pkgs.gnumake
    pkgs.gnupatch
    pkgs.perl
    pkgs.python3
    pkgs.squashfsTools
    pkgs.unzip
    pkgs.xz
    pkgs.zstd
    pkgs.zig
  ];

  # A static MIPS build, which goes into the initramfs.
  HOSTAPD = "${import ./hostapd.nix { inherit pkgs; }}/bin/hostapd";

  # Reads the root file system of AVM's firmware, for tools/fetch-avm-firmware.sh.
  UNSQUASHFS_AVM = "${import ./unsquashfs-avm.nix { inherit pkgs; }}/bin/unsquashfs3-multi";

  # cfg80211 needs this database for 5 GHz.
  REGDB = "${pkgs.wireless-regdb}/lib/firmware";
}
