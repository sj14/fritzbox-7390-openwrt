# unsquashfs for the root file system of AVM's firmware images, built the way
# Freetz-NG builds it: squashfs 3.4 with Freetz's patches, which add AVM's LZMA
# variant (squashfs version 3:76), linked with the LZMA SDK 4.65 and Freetz's
# patches for it. tools/fetch-avm-firmware.sh uses it.
#
# With pkgs set to a cross package set, such as
# pkgsCross.mips-linux-gnu.pkgsStatic, it builds the same tool for the box.
{ pkgs ? import ./nixpkgs.nix }:

let
  freetz = pkgs.fetchFromGitHub {
    owner = "Freetz-NG";
    repo = "freetz-ng";
    rev = "44284636601bbd56ca91fea862b306e56f43ed63";
    hash = "sha256-c1kTAEpdlMoonJS4MfbTBB8jWZ8hgu8P34wrDZjjBAQ=";
  };
  freetzPatches = name: pkgs.lib.filesystem.listFilesRecursive "${freetz}/make/host-tools/${name}/patches";

  lzma1 = pkgs.stdenv.mkDerivation {
    pname = "lzma1-freetz";
    version = "465";
    src = pkgs.fetchurl {
      url = "mirror://sourceforge/sevenzip/lzma465.tar.bz2";
      sha256 = "c935fd04dd8e0e8c688a3078f3675d699679a90be81c12686837e0880aa0fa1e";
    };
    sourceRoot = ".";
    patchFlags = [ "-p0" ];
    patches = freetzPatches "lzma1-host";
    buildInputs = [ pkgs.zlib ];
    # The makefile names gcc, ar and ranlib itself, which would be the build
    # machine's tools in a cross build.
    buildPhase = ''
      make -f makefile.gcc -C C/LzmaLib CC="$CC" AR="$AR" RANLIB="$RANLIB"
    '';
    # squashfs finds the library and the headers in one directory.
    installPhase = "install -D C/LzmaLib/liblzma.a $out/liblzma1.a; cp -r C $out/C";
  };
in
pkgs.stdenv.mkDerivation {
  pname = "unsquashfs3-multi-freetz";
  version = "3.4";
  src = pkgs.fetchurl {
    url = "mirror://sourceforge/squashfs/squashfs3.4.tar.gz";
    sha256 = "9ea1a9b3bd4f387ca11b5e96f00f8ae996fb81c4c7ad41f7c7f359917628a339";
  };
  patchFlags = [ "-p0" ];
  patches = freetzPatches "squashfs3-host";
  buildInputs = [ pkgs.zlib ];
  # The flags of Freetz's squashfs3-host.mk.
  buildPhase = ''
    make -C squashfs-tools CC="$CC" CFLAGS="-fcommon -std=gnu17 -O2" \
      LZMA1_SUPPORT=1 LZMA_LIBNAME=lzma1 LZMA_DIR=${lzma1} unsquashfs-multi
  '';
  installPhase = "install -D squashfs-tools/unsquashfs-multi $out/bin/unsquashfs3-multi";
}
