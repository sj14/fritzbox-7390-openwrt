# fix-avm-squashfs and extract-ap-firmware, which take the proprietary files out
# of AVM's firmware image (tools/fetch-avm-firmware.sh). With pkgs set to a cross
# package set, such as pkgsCross.mips-linux-gnu.pkgsStatic, it builds them for
# the box.
{ pkgs ? import ./nixpkgs.nix }:

pkgs.stdenv.mkDerivation {
  pname = "avm-firmware-tools";
  version = "1";
  src = pkgs.lib.fileset.toSource {
    root = ./tools;
    fileset = pkgs.lib.fileset.unions [ ./tools/fix-avm-squashfs.c ./tools/extract-ap-firmware.c ];
  };
  buildPhase = ''
    $CC -O2 -Wall -Wextra -Werror -o fix-avm-squashfs fix-avm-squashfs.c
    $CC -O2 -Wall -Wextra -Werror -o extract-ap-firmware extract-ap-firmware.c
  '';
  installPhase = "install -D -t $out/bin fix-avm-squashfs extract-ap-firmware";
}
