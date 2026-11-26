# Data and files

## Objects

Datum defines its own objects, none of which depends on ROOT:

- **Histograms** of any number of dimensions, profiles, and histograms with asymmetric systematic uncertainties;
- **Data points** with statistical and systematic uncertainties (and, in one variable, an $x$ uncertainty and the
  covariance of $x$ and $y$);
- **Tables** (`Table2D`): rows of numbers in named columns ([Tables of data](tables.md));
- **Parameterised functions**: a registry of classes (polynomials, Gaussians, exponentials, orthogonal series, Bessel
  functions, Fourier decompositions, probability distributions, two-dimensional functions, B&eacute;zier curves),
  and **composites** made of them: sums, products and ratios with coefficients, or $f(x)\,g(y)$;
- **Fits**: $\chi^2$ and likelihood fits over ranges and regions, with global searches and Bayesian fits; a fit is
  kept with the object it was made to.

Objects are organised in **folders**, as in a file system.

## Datum files

Datum files (`.datum`) hold folders of objects in a versioned binary format: each object is a record tagged by its
type (for example `DTMH` for a histogram), so that later versions of Datum still read the files of earlier ones. Files
are read lazily: an object is read when it is used. The files written by CAP (`.cap`) are still read.

Files are compact:

- empty bins of histograms are not written; a histogram of counts (unit weights) is written as its numbers of entries
  only, about one to two bytes per filled bin;
- records of objects of 4 kB or more are compressed (zlib) when that saves space: a 3-D density of 45,000 filled bins
  takes 58 kB instead of 465 kB, a ratio of histograms 60% less;
- data points write only the columns they use (x and y only: 8 bytes per point), and tables each column in the
  precision it needs; both are read back exactly.

In memory, a histogram bin takes 12 bytes in single precision and 20 in double (at most 2³¹ − 1 entries per bin).

```cpp
Datum::DatumFile file("run.datum", Datum::File::kReadOnly);
file.read();
const auto * h = file.get<Datum::MATH::Histogram>("Single/MB_PiP/n1_pt");
```

## Configuration in XML

Tasks are configured by properties read from XML: the templates of the task classes (`DB/Templates`), and the
workflow files that `RunDatum.py` writes ([Running analyses](workflows.md)).

## HEPData

Datum reads and writes [HEPData](https://www.hepdata.net) submissions: histograms (with asymmetric systematic
uncertainties), data points, fits and functions, to and from the YAML tables of hepdata.net.

- In DatumGui: **File &gt; Import &gt; HEPData YAML** and **File &gt; Export &gt; HEPData YAML**, with a preview.
- In a workflow: the task `Datum::HEPDataExporter` (template `DB/Templates/HEPDataExporter.xml`).
- In C++: `HEPDataSubmission`, `HEPDataConverter`, `HEPDataPlan` (`src/core/HEPData`).

## ROOT files

When Datum is built with ROOT (`-DDATUM_WITH_ROOT=ON`), `bin/DatumRootConvert` converts histograms between ROOT and
Datum files, DatumGui opens `.root` files, and a folder or a file of the browser can be exported to ROOT.

## Classes

`Datum::DatumFile`, `Datum::Folder`, `Datum::Compression`, `Datum::MATH::Histogram`, `Datum::MATH::DataPointColumns`,
`Datum::MATH::DataPointsVector`, `Datum::MATH::TableConversion`,
`Datum::MATH::Table2D`, `Datum::MATH::ParameterizedFunction`, `Datum::MATH::FunctionRegistry`
([API reference](../api.md)).
