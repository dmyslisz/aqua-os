# Aqua WindowServer (Quartz FreeBSD)

Autorski, niezależny serwer wyświetlania, kompozytor okien oraz framework UI (`libaqua`) dla systemu **FreeBSD 14+** na laptopie **HP 15s-fq2011nw (Intel Core i3-1115G4 Tiger Lake GT2 / Iris Xe Graphics)**.

Projekt o **zerowej zależności** od protokołu Wayland, biblioteki wlroots oraz serwera X11. Działa bezpośrednio na czystym sprzęcie i kernelu (`i915kms`, `/dev/dri/card0`, DRM/KMS, GBM, EGL 1.5 i OpenGL ES 3.0).

---

## Architektura i Kluczowe Możliwości

1. **Direct DRM/KMS + GBM + EGL Scanout:**
   - Bezpośrednie renderowanie na buforze skanowania układu Intel Iris Xe GT2 (`/dev/dri/card0`) w rozdzielczości eDP-1 1920x1080@60Hz.
   - Płynny, nieblokujący `drmModePageFlip` ze sprzętową synchronizacją VSync.

2. **Wieloprzebiegowy Dual-Kawase Frosted Glass Blur (macOS Vibrancy):**
   - Piramida 3 poziomów FBO (960x540, 480x270, 240x135) z 5-tapowym próbkowaniem downsample oraz 8-tapowym filtrem namiotowym upsample.
   - Sprzętowo akcelerowany, aksamitny efekt szkła Frosted Glass za Paskiem Menu i Dockiem w czasie poniżej 0.35 ms na GPU Iris Xe.

3. **macOS Desktop Shell:**
   - **Top Menu Bar:** renderowany w wektorze FreeType 2 (`DejaVuSans.ttf`), zegar na żywo, wektorowe ikony stanu Aqua (Apple Logo, Wi-Fi, Bateria, Dźwięk, Spotlight).
   - **Floating Dock:** dynamiczne powiększenie ikon w fali cosinusowej (macOS Sequoia magnification), zakotwiczenie do dołu, unoszenie w górę, tooltips.

4. **Architektura Okien macOS:**
   - Zaokrąglone rogi (12 px) i miękkie cienie (Drop Shadow) liczone przez Signed Distance Fields (SDF).
   - Pasek kontrolek **Traffic Lights** (Zamknij, Minimalizuj, Maksymalizuj) z dyskretnym wykrywaniem najechania kursorem i symbolami (X, -).
   - Przeciąganie okien oraz 8-kierunkowa zmiana rozmiaru (krawędzie i narożniki).

5. **Dwukierunkowy Protokół IPC (`/tmp/aqua-server.sock`):**
   - Przekazywanie deskryptorów buforów przez `SCM_RIGHTS` (zarówno `dma-buf` z GBM jak i pamięć współdzielona POSIX SHM).
   - Przekazywanie zdarzeń myszy (ruch, kliknięcie), klawiatury i negocjacji zmiany geometrii okna.

6. **Biblioteka Kliencka `libaqua` (C++20 UI Toolkit):**
   - `aqua::Application`: pętla zdarzeń, automatyczne łączenie z serwerem, oszczędzanie energii (sleep na `poll()`).
   - `aqua::AppWindow`: zarządzanie oknem, automatyczna synchronizacja bufora SHM, obsługa `on_draw`, `on_mouse_down`, `on_resize`, `on_key_down`, `on_close`.
   - `aqua::Canvas`: software rasterizer 2D – linie, prostokąty, zaokrąglone prostokąty, okręgi, gradienty, blendowanie alfa, wbudowana czcionka 8x16 (obsługa skalowania 1x, 2x, 3x, wyśrodkowania, wyrównania do prawej).

7. **Aplikacje Użytkowe w Zestawie:**
   - `aqua-calc`: autorski kalkulator w stylu macOS (ciemny motyw, pomarańczowe operatory, zaokrąglone przyciski, obsługa myszy i klawiatury fizycznej).
   - `aqua-sysmon`: monitor aktywności i specyfikacji FreeBSD (dane z `uname` i `sysctl`, paski zużycia RAM/CPU, animowany wykres obciążenia procesora na żywo).
   - `aqua-demo-client`: interaktywny klient testowy 60 FPS z animacją fal i reakcją na kliknięcia.

---

## Wymagania wstępne (FreeBSD)

Upewnij się, że masz zainstalowane pakiety graficzne i narzędzia:

```sh
pkg install -y cmake ninja pkgconf mesa-libs mesa-dri libdrm libinput libudev-devd freetype2 git
```

Oraz załadowany moduł jądra dla grafiki Intela:
```sh
kldload i915kms
```

---

## Kompilacja

```sh
git pull
cd build
ninja
```

---

## Uruchomienie

1. Uruchom serwer okien (wymaga uprawnień root dla dostępu do DRM/KMS i evdev):
```sh
sudo ./aqua-server
```

2. Na innym terminalu (lub przez SSH / w tle), uruchom aplikacje klienckie:
```sh
./aqua-calc &
./aqua-sysmon &
./aqua-demo-client &
```

Możesz dowolnie przesuwać okna, klikać przyciski, wpisywać liczby z klawiatury w kalkulatorze, zmieniać rozmiar okien przeciągając za krawędzie, oraz zamykać okna czerwonym przyciskiem Traffic Light!
