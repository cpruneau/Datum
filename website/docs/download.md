# Versions and download

## Current version: Datum 1.0

Datum 1.0 (November 2026) is the first published version of the package, and the ninth version of CAP, the
Correlation Analysis Package. It is a redesign around its own object model, data model and graphics, written in C++17:

- histograms of any number of dimensions, data points, tables, parameterised functions and fits, stored in a versioned
  binary format (`.datum`) that later versions still read; ROOT is no longer needed (it remains an option, to convert
  `.root` files);
- analyses configured by XML workflows, run in up to three stages, in sub-samples, locally or on a Slurm cluster;
- the graphical interface DatumGui: several windows, plot files, fits, functions, tables of data, and figures, PDF books,
  PowerPoint presentations and LaTeX documents;
- HEPData YAML import and export; a command language (DatumShell).

The programs, the C++ namespace and the files were renamed from CAP to Datum; the former names still work, and files
written by CAP (`.cap` data files, workflows naming `CAP::` classes) are still read.

| Version | Date | Notes |
|---|---|---|
| **1.0** | November 2026 | First published version (tag `v1.0`) |
| CAP 1&ndash;8 | 2019&ndash;2026 | Earlier versions (the Correlation Analysis Package), built on ROOT |

## Download

The sources are on GitHub: [github.com/cpruneau/Datum](https://github.com/cpruneau/Datum).

=== "With git"

    ```
    git clone https://github.com/cpruneau/Datum.git
    cd Datum
    git checkout v1.0          # the release; leave out for the latest development version
    ```

=== "As an archive"

    Download the release from the [releases page](https://github.com/cpruneau/Datum/releases), or directly:
    [Datum-1.0.tar.gz](https://github.com/cpruneau/Datum/archive/refs/tags/v1.0.tar.gz) &middot;
    [Datum-1.0.zip](https://github.com/cpruneau/Datum/archive/refs/tags/v1.0.zip)

## Prerequisites

Required:

- `cmake` 3.14 or later, `make`, and a C++17 compiler (Clang on macOS, GCC on Linux);
- Python 3.7 or later with tkinter, for the windows of `BuildDatum.py` and `RunDatum.py`.

Optional, each enabled when building:

| Package | For | CMake option | Location |
|---|---|---|---|
| [PYTHIA 8](https://pythia.org) | the Pythia generator | `-DDATUM_WITH_PYTHIA8=ON` | `export PYTHIA8=<dir>` |
| [Herwig 7](https://herwig.hepforge.org) | the Herwig generator | (BuildDatum.py) | `DATUM_HERWIG_DIR` |
| [FastJet](https://fastjet.fr) | jet analyses | `-DDATUM_WITH_JETS=ON` | `export FASTJET=<dir>` |
| [ROOT](https://root.cern) | `.root` histogram files, `DatumRootConvert`, readers of ROOT event trees | `-DDATUM_WITH_ROOT=ON` | `source <root>/bin/thisroot.sh` |
| LaTeX with `dvisvgm` | typeset labels in figures; compiled presentations | &mdash; | on the `PATH` |

Pythia 8, Herwig and FastJet are found from `DATUM_PYTHIA8_DIR`, `DATUM_HERWIG_DIR`, `DATUM_FASTJET_DIR` if set,
otherwise from a copy in the Datum folder (`pythia8*`, `Herwig*`, `fastjet*`, or under `external/`), the environment,
or `pythia8-config`, `Herwig-config`, `fastjet-config`. Dear ImGui, GLFW, NanoSVG and yaml-cpp are distributed with
Datum and built with it.

## Build and install

With the build window (Python 3 with tkinter):

```
cd Datum
python3 BuildDatum.py
```

Choose the optional modules and the generators, enter a build folder (several can coexist, for example with and
without Pythia) and click **Build**. From a shell:

```
cd Datum
source SetupDatum.sh
cmake -S src -B build                 # add -DDATUM_WITH_PYTHIA8=ON ... as needed
make -C build -j8 install
cd build && ctest                     # the tests
```

The libraries and the programs are installed in `lib` and `bin`. See [Getting started](manual/getting-started.md)
for what to run next.
