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
    pkgs.cacert
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
    # Take AVM's files apart, for tools/fetch-avm-firmware.sh.
    (import ./unsquashfs-avm.nix { inherit pkgs; })
    (import ./avm-firmware-tools.nix { inherit pkgs; })
    # OpenWrt's ImageBuilder checks for these on the host, besides those that
    # mkShell brings (coreutils, gawk, grep, tar, gzip, bzip2, patch, gcc).
    pkgs.file
    pkgs.git
    pkgs.rsync
    pkgs.util-linux
    pkgs.wget
    pkgs.which
  ];

  # The same tools built for the box, for avm-firmware-install in the OpenWrt image.
  UNSQUASHFS_AVM_BOX = "${import ./unsquashfs-avm.nix { pkgs = mips.pkgsStatic; }}/bin/unsquashfs3-multi";
  AVM_FIRMWARE_TOOLS_BOX = "${import ./avm-firmware-tools.nix { pkgs = mips.pkgsStatic; }}/bin";

  # cfg80211 needs this database for 5 GHz.
  REGDB = "${pkgs.wireless-regdb}/lib/firmware";
}
