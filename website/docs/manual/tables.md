# Tables of data

Measurements copied from a publication, the output of another program or a spreadsheet are tables of numbers.
Datum reads, plots, analyses and writes them as `Table2D` objects: rows of numbers in named columns.

## Files read

Text files `.txt`, `.dat`, `.csv`, `.tsv`, `.tab`, `.ssv`, binary tables `.dtb`, and tables stored in Datum files:

```
# optional comment lines
energy  count  error
1.5     10     3.2
2.5     20     4.5
```

- The values are separated by blanks, tabs, commas or semicolons; the separator is found from the first lines.
- The first row holds the names of the columns when one of its fields is not a number; otherwise the columns are
  named `c0`, `c1`, ...
- Empty fields and `NA` are missing values; short rows are completed; quoted CSV fields, comments (`#`) and the line
  endings of every system are handled.
- A file with entries other than numbers after the first row is **not** read: a dialog says where the first one is.

In DatumGui, a table opens in a **table pane** when it is clicked in the browser, or with **File &gt; Open**.

## Columns and plots

Each column has a role in the plots, set from its context menu (right-click on its name) or from the **Stat** menu:

| Item | |
|---|---|
| Use c*k* as X | the positions of the points (none: the index of the rows) |
| Use c*k* as Y | a quantity plotted against X; several columns can be Y |
| Use c*k* as error on x | horizontal error bars |
| Use c*k* as error on y | vertical error bars |
| Use c*k* as covariance between x and y | the covariance of the errors of each point |

Columns are named by their number *k*, with their name when they have one (`c2 (energy)`); the headers show the roles.
Then:

| Item | |
|---|---|
| Plot Y vs X (no errors) | the points |
| Plot Y vs X (with errors) | the points with their error bars (the covariance is kept with them) |
| Bar Chart of Y vs X | one bar per row, at X; several columns side by side |
| Bar Chart of Y | one bar per row, at the index of the row |
| Distribution of Y | a histogram of the values of each column |

The plots are drawn in a canvas beside the table, where they are fitted, compared and saved as any other object
(*Overlay Plots* adds them to the graph instead of replacing its content).

## Statistics

- **Summary Statistics**: number of values, missing values, mean, standard deviation, minimum, median and maximum of
  each column, as a new table.
- **Correlations**: the matrix of the correlation coefficients of Pearson, Spearman (of the ranks) or Kendall
  ($\tau_b$) of the selected columns, each with its uncertainty and $p$-value, as a table and as a map.
- **Copy Table** (to the clipboard, tab separated) and **Save Table As** (CSV, tab or blank separated text, Datum
  binary, Excel `.xlsx`).

The **Stat** menu holds the same items as the context menu, and is active when a table pane is shown and is the pane
used last.

## In programs and workflows

`Datum::TableReader`, `Datum::TableWriter`, `Datum::MATH::TableStatistics` and `Datum::TablePlots` do the same in C++;
the task `Datum::SpreadsheetAnalyzer` reads a table, selects columns, processes them, and writes its summary,
correlations and plots to a Datum file (template `DB/Templates/SpreadsheetAnalyzer.xml`).

```cpp
Datum::MATH::Table2D table;
Datum::TableReader::Report report = Datum::TableReader().read("measurements.csv", table);
std::cout << report.summary() << std::endl;     // e.g. "1200 rows x 6 columns (comma), header"
auto * points = Datum::TablePlots::xy(table, table.columnIndex("x"), table.columnIndex("y"),
                                      table.columnIndex("ey"), table.columnIndex("ex"));
```
