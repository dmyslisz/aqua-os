# Aqua WindowServer (Quartz FreeBSD)

Autorski serwer wyświetlania i kompozytor okien dla systemu FreeBSD (14+ / CURRENT) na laptopie **HP 15s-fq2011nw (Intel Core i3-1115G4 Tiger Lake GT2 / Iris Xe Graphics)**.

Projekt o zerowej zależności od protokołu Wayland, biblioteki wlroots oraz serwera X11.

## Wymagania wstępne (FreeBSD)

Upewnij się, że masz zainstalowane pakiety graficzne i narzędzia:

```sh
pkg install -y cmake ninja pkgconf mesa-libs mesa-dri libdrm git
```

Oraz załadowany moduł jądra dla grafiki Intela:
```sh
kldload i915kms
```

## Kompilacja i uruchomienie

```sh
mkdir build
cd build
cmake -G Ninja ..
ninja
./aqua-server
```

Po uruchomieniu na ekranie laptopa pojawi się bezpośrednio płynnie animowany gradient w 60 FPS ze sprzętową synchronizacją VSync (Direct DRM/KMS scanout bez żadnego serwera pośredniczącego).
Naciśnij `Ctrl+C`, aby bezpiecznie powrócić do konsoli tekstowej.
