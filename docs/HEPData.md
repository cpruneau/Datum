# HEPData YAML: import and export

Datum reads and writes [HEPData](https://www.hepdata.net) YAML, the format in which most published results of high
energy physics are archived. Histograms, sets of data points, fitted functions and the fits attached to histograms
can be exported to a HEPData submission and imported back. This page covers:

1. [What a HEPData submission is](#1-the-hepdata-format)
2. [How Datum stores systematic uncertainties](#2-systematic-uncertainties-in-datum)
3. [Import: tables to Datum objects](#3-import)
4. [Export: Datum objects to tables](#4-export)
5. [Functions and fits](#5-functions-and-fits)
6. [Lossless and strict files](#6-lossless-and-strict-files)
7. [Publication and collaboration](#7-publication-and-collaboration)
8. [In DatumGui: File > Import / Export > HEPData YAML](#8-in-datumgui)
9. [In a workflow: the HEPDataExporter task](#9-in-a-workflow-the-hepdataexporter-task)
10. [In C++](#10-in-c)
11. [Building, tests, limitations](#11-building-tests-limitations)

The code is in `src/core/HEPData` (library `HEPData`, built with `-DDATUM_WITH_YAML=ON`, the default) and
`src/gui/HEPDataDialog.*`. YAML is read and written with [yaml-cpp](https://github.com/jbeder/yaml-cpp) (MIT
licence), distributed with Datum in `external/yaml-cpp`; a yaml-cpp installed on the system is used instead when CMake
finds one.

## 1. The HEPData format

A submission is a directory (or a `.tar.gz` / `.zip` archive of one) holding:

- **`submission.yaml`**: several YAML documents separated by `---`. The first, optional, applies to the whole
  submission (`comment`, `additional_resources`); each of the others describes one table: `name` (at most 64
  characters), `description`, `location` (e.g. "Figure 3a"), `keywords` (`observables`, `reactions`, `cmenergies`,
  `phrases`), `data_file`, and optionally `data_license` and `additional_resources`.
- **one data file per table** (`data1.yaml`, ...), with `independent_variables` (the x: values, or bins `low`/`high`)
  and `dependent_variables` (the y), each with a `header` (`name`, `units`), `qualifiers` for the y (e.g.
  `SQRT(S) = 13000 GEV`), and one row per point. Each y value has its uncertainties: `symerror` (symmetric) or
  `asymerror` (`plus`, `minus`), each with a `label` (`stat`, `sys`, `sys,lumi`, ...), possibly in percent of the value.
  A value that is not a number (`'-'`) marks an empty bin.

The single-YAML-file format, where the variables are in the documents of `submission.yaml`, and single data files
(one table, no metadata) are read as well. See the
[HEPData submission documentation](https://hepdata-submission.readthedocs.io).

## 2. Systematic uncertainties in Datum

HEPData keeps statistical and systematic uncertainties apart, often asymmetric, plus uncertainties common to all the
rows (a normalization, a luminosity). Datum holds the same:

| Where | What | API |
|---|---|---|
| `DataPoint` (and `DataPoint2D`, `3D`, `ND`) | statistical uncertainty, and an **asymmetric systematic** uncertainty per point | `setSyst(plus, minus)`, `systPlus()`, `systMinus()`; `syst()` is the larger side |
| `DataPoints` (base of points and histograms) | a **common systematic** uncertainty, relative to the value, upper and lower | `setCommonSyst(relative)` or `setCommonSyst(plus, minus)`, `getCommonSyst()`, `getCommonSystPlus()`, `getCommonSystMinus()`, `hasCommonSyst()` |
| `HistogramSyst` (a `Histogram`) | an **asymmetric systematic** uncertainty per bin (float or double, as the bin values) | `setBinSystAt(i, plus, minus)`, `binSystPlusAt(i)`, `binSystMinusAt(i)`, `binSystAt(i)`; global-index forms `...AtGlobal(k)` |
| `Histogram` | no systematic per bin: the same functions throw, `hasSystUncertainties()` is false | |

Systematic uncertainties are propagated through the arithmetic of `HistogramSyst` (to first order, each side
separately) and saved in Datum files (record `DTMY`). Histograms can also have **bins of different widths**
(`Histogram(name, title, edges)`, `HistogramSyst(name, title, edges)`, `Partition::isRegular()`, `binEdges()`), which
most published tables need.

## 3. Import

`HEPDataConverter::toObjects()` makes **one object per dependent variable** of a table.

- **Histograms.** When the independent variables are bins that do not overlap (up to three variables; gaps between
  rows become empty bins), the table gives a `Histogram`, or a `HistogramSyst` if it has systematic uncertainties.
  The axes are regular when the bins have equal widths, variable otherwise.
  A histogram keeps only the bins: when the table also gives positions inside the bins (e.g. the mean pT), they are
  dropped with a warning, unless `ImportOptions::keepPositions` asks for data points instead.
- **Points.** Otherwise (values, overlapping bins, more variables) the table gives a `DataPointsVector` of
  `DataPoint2D`, `DataPoint3D` or `DataPointND`. The coordinate of a row is its value if given, else the middle of its
  bin; its uncertainty is half the width of the bin.
- **Uncertainties.**
  - Labelled statistical (`stat`, or the only, unlabelled error): the statistical uncertainty. Several are added in
    quadrature; an asymmetric one gives its larger side (with a warning).
  - Labelled as common (`ImportOptions::commonLabels`, by default `norm` and `sys,common`): the **common systematic**
    uncertainty of the object, relative to the value and averaged over the rows. A warning is given if it varies by
    more than `commonTolerance` (5 %).
  - All the others (`sys`, `sys,lumi`, `sys,model`, ...): **added in quadrature**, each side separately, into the
    systematic uncertainty of the point or bin. Several sources therefore become one.
  - Percentages are converted to absolute uncertainties.
- **Names and titles.** The name is the stem of the data file (`data3`), with `_<k>` for the k-th dependent variable
  when there are several. The title is `<name> (<units>)` of the dependent variable, and axis titles are made the same
  way. Files written by Datum in lossless mode (§6) give back the original names, titles, axes and entries.
- **Tables of functions and fits** written by Datum in lossless mode give back the function (§5).

## 4. Export

`HEPDataConverter::toTable()` writes one object per dependent variable. Several objects with the same binning or
coordinates can share a table.

- **Histograms:** one independent variable per axis, with the edges of the bins (2D and 3D flattened, axis 0 fastest).
  Empty bins are left out (written as `'-'` in lossless mode).
- **Points:** the coordinates as values, with `low` and `high` when their uncertainty is not zero.
- **Uncertainties:** the statistical uncertainty (label `stat`), the systematic one (`sys`; `symerror`, or `asymerror`
  when the two sides differ), and the common systematic uncertainty as a percentage (`norm`). The labels are
  `ExportOptions::statLabel`, `systLabel` and `commonLabel`.
- **Headers:** from the titles, `name (units)` split into name and units.

Export writes **one** systematic uncertainty per row and the common one apart. A file whose rows have several
systematic sources therefore comes back with one source per row (their sum in quadrature). A round trip is exact for
tables with one statistical and one systematic uncertainty (symmetric or not), plus the common one.

## 5. Functions and fits

A function of one variable (`ParameterizedFunction`, e.g. `Exponential`, `Normal`, a polynomial, a composite) is
written as a table of its values, which HEPData and Rivet can use:

- **sampled** at `nPoints` points evenly spaced over a range, ends included, or
- **on bins** with given edges: the **average** of the function over each bin (`integralInRange` / width, the
  quantity a histogram measures) or its value at the center.

The parameters become qualifiers of the dependent variable (`amplitude = 1520`; for a fit, `1520 +- 30`), with
`FUNCTION = Exponential`, and, for a fit, `CHI2/NDF = 12.3/8`.

A **fit** (`FitRecord`, as attached by `Fitter::fitAndAttach()` or the Fit window of DatumGui) is written over its
range, with an **uncertainty band**. The band is propagated from the covariance of the free parameters: a `symerror`
labelled `fit`, sqrt(g^T C g), with g the gradient of the function in the parameters. `fitTables(object)` gives one
table per fit attached to an object, named `<object> fit <k>: <label>`.

In lossless mode the function (its class in `FunctionRegistry`, its parameters with errors, types and bounds, and its
binary record) and the fit (result, chi2, ndf, p-value, covariance) are kept in the datum block. `toFunction(table)`
and `toFit(table)` rebuild them **exactly**. The lossless table of a histogram or points also keeps the fits attached
to it (`ExportOptions::withFits`), and `toObjects()` attaches them again.

## 6. Lossless and strict files

- **Lossless** (the default): each data file has, after the HEPData variables, a top-level `datum:` block with what
  HEPData cannot hold: class, name, title, axis titles, entries per bin, under- and overflow, binning, precision, fits,
  functions. Binary records are written in base64. Datum rebuilds the objects exactly from it. hepdata.net and other
  readers ignore the block.
- **Strict**: plain HEPData files, for a submission to hepdata.net.

Numbers are written in the shortest form that reads back exactly, or with a given number of significant digits.

## 7. Publication and collaboration

hepdata.net takes the paper (title, authors, journal, DOI) and the collaboration from the INSPIRE record attached to
the submission: `submission.yaml` has no field for them. Datum keeps them (`HEPDataPublication`) as comment lines at
the top of `submission.yaml`, which HEPData ignores and Datum reads back:

```yaml
# Collaboration: ALICE
# Title: Neutral pion production in O-O and p-O collisions
# INSPIRE: 1234567
# arXiv: 2511.01234
# DOI: 10.1016/...
# Journal: Phys. Lett. B ...
# Contact: A. Author <a.author@cern.ch>
---
comment: |
  ALICE Collaboration, Neutral pion production in O-O and p-O collisions, arXiv:2511.01234 (INSPIRE 1234567).

  Production cross sections of neutral pions ...
```

The names used by collaborations are also read (`# InspireHEP: 99999`), and other comment lines are kept. The comment
of the submission can begin with a one-sentence summary of the publication (`HEPDataPublication::summary()`).

## 8. In DatumGui

**File > Import > HEPData YAML...** and **File > Export > HEPData YAML...** open a window whose **Preview** button
opens a second window, resizable and scrollable. It shows what will be done as a tree of bullets: actions (what OK
does) in green, warnings in yellow, errors in red. **Expand All**, **Collapse All** and **Copy** (the whole tree as
text, e.g. for a log or a note to a colleague) act on it, and a right-click on a line copies that line or its branch.
**OK** does what the preview shows; **Cancel** closes the preview and does nothing. A change made in the window after
Preview makes the preview stale: OK waits for Preview again. **Help** in the window (or **Help > HEPData Import and
Export**) summarizes this page.

### Import

- **Read from**: a folder holding a submission (`submission.yaml` and its data files); or a folder of such folders,
  of archives (`.tar.gz`, `.tgz`, `.tar`, `.zip`) and of loose YAML data files (each a table); or one archive or file.
  **Folder...** and **File...** browse for them.
- **Data file made**: the name of the new data file in the browser (default: that of the folder).
- **Conversion**: histograms from bins (otherwise data points); points where values are given within the bins; the
  labels of the common systematic uncertainty (separated by `, `); rows shown per table in the preview.

Preview reads the files and converts the tables in memory. For each submission it shows its publication, comment and
tables (description, location, keywords, variables, qualifiers, first rows), each with the objects it makes, e.g.

```
- => makes HistogramSyst "data1" (E d**3sigma/dp**3 (b GEV**-2 c**3)), 1D: 27 variable bins in PT (GEV/c) [1.2, 20];
     stat + syst + common syst 2.6%
- warning: the positions given within the bins are not kept by the histogram (bin centers are used)
```

OK makes a **new data file** in the browser with these objects (a folder per submission when there are several).
Save writes it as a Datum file.

### Export

- **Objects**: the data files open in the browsers (the one selected is checked), and the objects of those checked:
  histograms and points (with their fits: column Fits) and functions of one variable. Click a row to edit its
  **table**: description, location, keywords (observables, reactions, cmenergies, phrases), qualifiers
  (`NAME = value [units]; ...`, e.g. `SQRT(S) = 13.6 [TEV]; CENTRALITY = 0-10 [%]`), names and units of x and y, and,
  for a function, its range and number of points. The table name is in the list.
- **Submission**: the directory written, an archive (none, `.tar.gz`, `.zip`, next to the directory), Lossless or
  Strict, significant digits.
- **Publication and collaboration**: collaboration, title, INSPIRE, arXiv, DOI, journal, contact; whether the comment
  begins with them; the comment.
- **All tables**: the keywords, qualifiers, location and licence of the tables that set none.

Preview shows where the files go, the files, `submission.yaml` with every table (and the object it comes from), what
**hepdata.net would reject** (missing keywords or descriptions, names longer than 64 characters, ...), the objects
that cannot be written, and a warning when a submission or an archive is replaced. OK writes the files.

## 9. In a workflow: the HEPDataExporter task

`Datum::HEPDataExporter` writes a submission from a workflow, as the Export window does. Run it as a Datum job of its
own, after the jobs that make the files:

```
bin/Datum HEPDataExporter <directory of the XML>/ HEPDataExport.xml <histogram directory>/
```

Its template is `DB/Templates/HEPDataExporter.xml` (written by `bin/Datum --xml-templates`, regenerated by
RunDatum.py). An example job is `src/core/HEPData/examples/HEPDataExport.xml`. The keys, below the name of the task
(`NONE` when not set):

| Key | Default | Meaning |
|---|---|---|
| `HISTOGRAM:IMPORT:PATH` | NONE | directory of the Datum files (NONE: the histogram directory of the job) |
| `HISTOGRAM:IMPORT:FILE_NAME` | NONE | the Datum files read, separated by spaces |
| `HEPDATA:EXPORT:PATH` | NONE | the directory written (created if needed) |
| `HEPDATA:EXPORT:ARCHIVE` | NONE | an archive of it (`.tar.gz`, `.tgz`, `.zip`); a name alone goes next to the directory |
| `MODE` | LOSSLESS | LOSSLESS or STRICT (§6) |
| `DIGITS` | 0 | significant digits (0: the shortest exact form) |
| `FITS` | true | also the fits of the histograms and points, a table each |
| `OBJECTS`, `EXCLUDE` | `*`, NONE | when no table is listed: the objects whose path matches a pattern of OBJECTS (`*`, `?`, separated by spaces) and none of EXCLUDE |
| `FUNCTION:LOW`, `HIGH`, `POINTS` | 0, 1, 100 | range and points of the functions (when not set for a table) |
| `TABLE:N` | 0 | number of tables listed, `TABLE_1` ... `TABLE_N` (0: until the first without PATH; none listed: OBJECTS) |
| `TABLE_<k>:PATH` | NONE | path of the object in its file, e.g. `ParticleSingleAnalyzer/ALL_PiP/n1_pt` |
| `TABLE_<k>:FILE` | NONE | the file holding it, when several are read (NONE: the first that has it) |
| `TABLE_<k>:NAME`, `DESCRIPTION`, `LOCATION` | NONE | name of the table (default: the name of the object), caption, place in the paper |
| `TABLE_<k>:OBSERVABLES`, `REACTIONS`, `CMENERGIES`, `PHRASES`, `QUALIFIERS` | NONE | as in `TABLES:` below, for this table |
| `TABLE_<k>:X_NAME`, `X_UNITS`, `Y_NAME`, `Y_UNITS` | NONE | headers (default: from the titles) |
| `TABLE_<k>:LOW`, `HIGH`, `POINTS` | NONE | range and points of a function |
| `TABLES:OBSERVABLES`, `REACTIONS`, `CMENERGIES`, `PHRASES` | NONE | keywords of the tables that set none (values separated by commas) |
| `TABLES:QUALIFIERS` | NONE | `NAME = value [units]; ...` |
| `TABLES:LOCATION`, `LICENSE`, `LICENSE_URL` | NONE | location and data licence of the tables that set none |
| `PUBLICATION:COLLABORATION`, `TITLE`, `INSPIRE`, `ARXIV`, `DOI`, `JOURNAL`, `CONTACT` | NONE | §7 |
| `PUBLICATION:IN_COMMENT` | true | the comment begins with the publication |
| `COMMENT` | NONE | comment of the submission |
| `PREVIEW_ONLY` | false | print the preview, write nothing |
| `PREVIEW_ROWS` | 3 | rows of each table in the preview |
| `FAIL_ON_PROBLEMS` | false | stop the job if hepdata.net would reject the submission or an object cannot be written (otherwise: warnings) |

Paths: `$VARIABLES` are expanded, and a relative path is in the histogram directory of the job
(`$DATUM_HISTOS_EXPORT_PATH`, set by bin/Datum), as for the input and output files of jobs. Set the job's
`JOB:OUTPUT_FILE` to NONE: the task writes no Datum file. The log shows the preview of §8, as text.

```xml
<TASK name="HEPDataExporter" class="Datum::HEPDataExporter" severity="INFO">
  <GROUP name="HISTOGRAM"><GROUP name="IMPORT">
    <PARAMETER name="FILE_NAME" type="string">ParticleSingle.datum</PARAMETER>
  </GROUP></GROUP>
  <GROUP name="HEPDATA"><GROUP name="EXPORT">
    <PARAMETER name="PATH" type="string">HEPData</PARAMETER>
    <PARAMETER name="ARCHIVE" type="string">HEPData.tar.gz</PARAMETER>
  </GROUP></GROUP>
  <PARAMETER name="MODE" type="string">STRICT</PARAMETER>
  <GROUP name="TABLES">
    <PARAMETER name="OBSERVABLES" type="string">DN/DPT</PARAMETER>
    <PARAMETER name="CMENERGIES" type="string">13700</PARAMETER>
  </GROUP>
  <GROUP name="TABLE"><PARAMETER name="N" type="int">1</PARAMETER></GROUP>
  <GROUP name="TABLE_1">
    <PARAMETER name="PATH" type="string">ParticleSingleAnalyzer/ALL_PiP/n1_pt</PARAMETER>
    <PARAMETER name="NAME" type="string">Figure 1a</PARAMETER>
    <PARAMETER name="DESCRIPTION" type="string">Transverse momentum spectrum of positive pions.</PARAMETER>
    <PARAMETER name="REACTIONS" type="string">P P --> PI+ X</PARAMETER>
  </GROUP>
  <GROUP name="JOB"><PARAMETER name="OUTPUT_FILE" type="string">NONE</PARAMETER></GROUP>
</TASK>
```

The task dialog of RunDatum shows the keys of the template, which lists `TABLE_1`. Add `TABLE_2`, `TABLE_3`, ... in
the XML, and set `TABLE:N` accordingly.

## 10. In C++

Read a submission, convert, write it back:

```cpp
#include "HEPDataSubmission.hpp"
#include "HEPDataConverter.hpp"
using namespace Datum;

HEPDataSubmission s = HEPDataSubmission::read("HEPData-ins1234567-v1-yaml.tar.gz");   // directory, file or archive
for (const HEPDataConverter::Imported & i : HEPDataConverter::toObjects(s))
  std::cout << i.table << ": " << i.object->name() << std::endl;   // Histogram, HistogramSyst or DataPointsVector

MATH::HistogramSyst h("pt", "dN/dpT (1/GEV)", {0.0, 1.0, 2.0, 4.0, 8.0});   // variable bins
h.setBinAt(1, 1, 120.0, 11.0);       // value 120 +- 11 (stat)
h.setBinSystAt(1, 6.0, 4.0);         // +6 -4 (syst)
h.setCommonSyst(0.03);               // 3 % normalization
HEPDataTable t = HEPDataConverter::toTable(h);
t.name = "Figure 1";
t.description = "Transverse momentum spectrum.";
t.setKeyword("observables", {"DN/DPT"});
HEPDataSubmission out;
out.publication.collaboration = "ALICE";
out.addTable(t);
out.write("HEPData", "HEPData.tar.gz");   // lossless; write(dir, archive, digits, false) for strict files
```

A fit and a function:

```cpp
MATH::FitRecord fit = MATH::Fitter::fitAndAttach(function, h);
HEPDataTable ft = HEPDataConverter::fitTable(fit);                 // range of the fit, band, parameters
HEPDataConverter::FunctionOptions o;  o.edges = {0, 1, 2, 4, 8};   // bin averages
HEPDataTable fn = HEPDataConverter::functionTable(function, 0.0, 8.0, o);
std::shared_ptr<MATH::ParameterizedFunction> back = HEPDataConverter::toFunction(fn);   // lossless: exact
```

What the windows of DatumGui and the task do is in `HEPDataPlan.hpp`, usable by programs:

- `HEPDataImportPlan`: `scan()`, `prepare(path)`, `preview()`, `addTo(folder)`.
- `HEPDataExportPlan`: `itemsOf(folder, label)`, then the items, publication and defaults; `build()`, `preview()`,
  `write()`.
- `HEPDataPreviewNode::toText()`: the preview as text.

| Class (src/core/HEPData) | Role |
|---|---|
| `HEPDataTable`, `HEPDataVariable`, `HEPDataValue`, `HEPDataError`, `HEPDataQualifier`, `HEPDataKeyword`, `HEPDataResource` | the tables, as in the files; `HEPDataTable::problems()` lists what hepdata.net would reject |
| `HEPDataSubmission`, `HEPDataPublication` | submission.yaml and the tables; read, write, archives |
| `HEPDataConverter` | tables to Datum objects and back; functions and fits |
| `HEPDataPlan` (`HEPDataImportPlan`, `HEPDataExportPlan`, `HEPDataPreview`, `HEPDataPreviewNode`) | what an import or export will do, and its preview |
| `HEPDataExporter` | the task (§9) |
| `HEPDataDialog` (src/gui) | the windows of DatumGui (§8) |

## 11. Building, tests, limitations

- **Building:** `-DDATUM_WITH_YAML=ON` (default) builds the `HEPData` library, links it into `bin/Datum` (the task)
  and into DatumGui (the windows).
- **Tests** (ctest): `YamlCpp`, `HEPData` (format, the ALICE O-O and p-O π0 record in `tests/data`),
  `HEPDataConverter`, `HEPDataFunctions`, `HEPDataPlan`, `HEPDataExporter`, and `HEPDataDialog` (the windows of
  DatumGui drawn by Dear ImGui without a screen, built with the GUI). Also `HistogramSyst`, `DataPointSyst` and
  `VariableBins` in Math.
- **Limitations:**
  - Several systematic sources are summed in quadrature on import; export writes one.
  - Histograms keep bin centers only (`keepPositions` gives points instead).
  - Only functions of one variable are written as tables.
  - DatumGui does not yet draw the systematic uncertainties of a `HistogramSyst` apart from the statistical ones.
  - `bin/DatumRootConvert` reads regular binnings only.
