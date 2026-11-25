# API reference

The classes, functions and files of Datum are documented from the comments of the sources by Doxygen.

<a href="doxygen/index.html" class="md-button md-button--primary">Open the API reference</a>

## Where to start

| Folder | Libraries | Start with |
|---|---|---|
| `src/core` | Base, Math, Graphics, Latex, Interpreter, HEPData, RootConvert | `Datum::Object`, `Datum::DatumFile`, `Datum::Task`, `Datum::MATH::Histogram`, `Datum::MATH::ParameterizedFunction`, `Datum::GRAPHICS::Canvas` |
| `src/analysis` | Particles, ParticleSingle, ParticlePair, PtPt, NuDyn, Global, Spherocity, Jets, SubSample, Performance, Spreadsheet | `Datum::AnalysisTask`, `Datum::EventIterator`, the analyzers of each library, `Datum::TableReader` |
| `src/models` | BasicEventGen, Glauber, Therminator3, Pythia, Herwig, Epos, PHSD | the generator task of each library |
| `src/gui` | DatumGui | `Datum::GUI::DatumApp`, `Datum::GUI::DatumGui` |

The [manual](manual/index.md) describes how these pieces are used; each of its pages names the classes involved.

!!! note
    The API reference is made from the sources of the version on this site (Datum 1.0). In a copy of the sources,
    the same pages are made with `website/build_site.sh --api`.
