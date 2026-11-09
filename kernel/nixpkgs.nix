# The nixpkgs that all Nix files here build with, pinned so that every machine
# gets the same cross compiler, hostapd and wireless-regdb: nixos-26.05 of
# 2026-11-05. To update, change rev and set sha256 to the hash that Nix
# reports for the new tarball.
let
  rev = "c56a87d8e41ce69bd2af57eb35dcc117a9dfca33";
in
import (fetchTarball {
  url = "https://github.com/NixOS/nixpkgs/archive/${rev}.tar.gz";
  sha256 = "sha256-/cpdPHaySDIyCgZI6EWngiM6jK3YLUNzv1b7lTzy6Bk=";
}) { }
