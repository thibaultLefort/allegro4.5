# Allegro 4.5 — macOS Apple Silicon

If you want the english version go [here](#allegro-45--macos-apple-silicon-en)

Allegro 4 patché et prêt à installer sur les **Macs Apple Silicon** (arm64).  
Une fois installé, ça fonctionne comme n'importe quelle bibliothèque système — include, link, c'est parti.

## Prérequis

- macOS sur Apple Silicon (M1 / M2 / M3 / M4)
- [Homebrew](https://brew.sh)

## Installation

### Homebrew (recommandé)

```bash
brew tap thibaultLefort/allegro4
brew install allegro4
```

### Depuis les sources

Nécessite Xcode Command Line Tools (`xcode-select --install`) et CMake (`brew install cmake`).

```bash
./install.sh
```

S'installe dans `/usr/local` par défaut. Pour choisir un autre dossier :

```bash
./install.sh ~/meslibs
```

## Utilisation dans un projet

Le fichier `main.c` doit avoir `END_OF_MAIN()` sur sa propre ligne après l'accolade fermante `}` de `main()` :

```c
int main(void) {
    allegro_init();
    // ...
    return 0;
}
END_OF_MAIN()
```

Cette macro est obligatoire avec Allegro sur macOS — sans elle, le programme ne démarrera pas.

```bash
gcc main.c $(allegro-config --libs) -o monprogramme
./monprogramme
```

Ou manuellement :

```bash
gcc main.c -I/usr/local/include -L/usr/local/lib -lalleg-main -lalleg -framework Cocoa -o monprogramme
```

## Ce qui est installé

| Chemin | Contenu |
|--------|---------|
| `/usr/local/include/allegro.h` | Header principal |
| `/usr/local/include/allegro/` | Sous-headers |
| `/usr/local/lib/liballeg.4.4.dylib` | Bibliothèque partagée |
| `/usr/local/bin/allegro-config` | Outil pour les flags de compilation |

## Note

Cette version cible **arm64 uniquement**. Elle ne fonctionnera pas sur les Macs Intel.


# Allegro 4.5 — macOS Apple Silicon (EN)

Allegro 4 patched and ready to install on **Apple Silicon Macs** (arm64).  
After install it works like any system library — include, link, done.

## Requirements

- macOS on Apple Silicon (M1 / M2 / M3 / M4)
- [Homebrew](https://brew.sh)

## Install

### Homebrew (recommended)

```bash
brew tap thibaultLefort/allegro4
brew install allegro4
```

### From source

Requires Xcode Command Line Tools (`xcode-select --install`) and CMake (`brew install cmake`).

```bash
./install.sh
```

Installs to `/usr/local` by default. Pass a custom prefix if needed:

```bash
./install.sh ~/mylibs
```

## Use in your project

Your `main.c` must have `END_OF_MAIN()` on its own line after the closing `}` of `main()`:

```c
int main(void) {
    allegro_init();
    // ...
    return 0;
}
END_OF_MAIN()
```

This macro is required by Allegro on macOS — without it the program will not start.

```bash
gcc main.c $(allegro-config --libs) -o myprogram
./myprogram
```

Or manually:

```bash
gcc main.c -I/usr/local/include -L/usr/local/lib -lalleg-main -lalleg -framework Cocoa -o myprogram
```

## What gets installed

| Path | Contents |
|------|----------|
| `/usr/local/include/allegro.h` | Main header |
| `/usr/local/include/allegro/` | Sub-headers |
| `/usr/local/lib/liballeg.4.4.dylib` | Shared library |
| `/usr/local/bin/allegro-config` | Compiler flags helper |

## Note

This build targets **arm64 only**. It will not run on Intel Macs.
