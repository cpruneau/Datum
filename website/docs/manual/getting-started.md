# Getting started

## The folders of Datum

| Folder | Contents |
|---|---|
| `src/core` | What does not depend on particles or events: Base (objects, folders, reflection, tasks, configuration, XML and binary files), Math (data, functions, fits, statistics), Graphics, Latex, Interpreter, HEPData, RootConvert |
| `src/analysis` | The event processing: Particles (events, particles, filters, efficiencies, the base classes of the analyses) and one library per family of observables; Spreadsheet (tables of data) |
| `src/models` | The event generators and readers |
| `src/gui` | DatumGui |
| `DB` | Data read by the jobs: the particle database, the inputs of the thermal model, the XML templates of the tasks |
| `projects` | Example workflows of the generators |
| `external` | Libraries distributed with Datum (Dear ImGui, GLFW, NanoSVG, yaml-cpp) |
| `bin`, `lib` | The programs and libraries, once installed |
| `docs` | Notes and the paper |

The libraries form layers, each using only those below it: Base depends on no other library of Datum, Math only on
Base; Graphics, Latex and Interpreter use Math but no part of the analyses. The analyses and the models use Particles,
and never each other, so that a new generator works with every analysis, and a new analysis with every generator.

## The programs

| Program | Purpose |
|---|---|
| `BuildDatum.py` | Configures and runs the build (see [Download](../download.md#build-and-install)) |
| `RunDatum.py` | Composes, checks and runs workflows ([Running analyses](workflows.md)) |
| `bin/Datum` | Runs one stage of a workflow, the job itself |
| `bin/DatumGui` | The graphical interface ([DatumGui](datumgui.md)) |
| `bin/PlotHistos` | Draws plot files without a window ([Plots in batch](batch-and-shell.md)) |
| `bin/DatumShell` | The command language ([DatumShell](batch-and-shell.md#datumshell)) |
| `bin/DatumRootConvert` | Converts histograms between ROOT and Datum files (with ROOT) |

The former names of CAP (`bin/CAP`, `bin/CapGui`, `RunCAP.py`, `CAP_...` variables) still work.

## Environment

`source SetupDatum.sh` sets the paths of Datum, of its libraries, programs, database and projects. The folders of
your own histograms and data can be set in your shell profile:

```
export DATUM_USER_HISTO_IMPORT_PATH=<folder>   DATUM_USER_HISTO_EXPORT_PATH=<folder>
export DATUM_USER_DATA_IMPORT_PATH=<folder>    DATUM_USER_DATA_EXPORT_PATH=<folder>
```

## A first run

1. Build Datum ([Download](../download.md)).
2. Start `python3 RunDatum.py`, open one of the example workflows of `projects` (for instance the Gaussian test, or
   Pythia if it was built), and click **Run all stages**.
3. Open the output file in DatumGui:

    ```
    bin/DatumGui <output folder>/<output file>.datum
    ```

    Click a histogram in the browser to draw it; right-click the graph to fit, compare or rebin it.

4. Save the session as a plot file (Plots &gt; Save As) to draw the same figures again, or to make a PDF book, a
   PowerPoint presentation or a LaTeX document of them ([Plots in batch](batch-and-shell.md)).
