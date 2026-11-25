# Plots in batch and DatumShell

## Figures and documents

A canvas is drawn by the same code on the screen and in files, so what is seen is what is saved. Datum writes:

- **PDF** and **SVG** files (no external library), and PDF **books** of one canvas per page;
- **PNG** images;
- **PowerPoint** presentations, one canvas per slide, drawn as native shapes that can be edited;
- **LaTeX** documents: a Beamer presentation with one frame per canvas, or an article with one section per window and
  one figure per canvas, compiled if asked.

Labels written in LaTeX (or the TLatex notation of ROOT) are typeset by `latex` and `dvisvgm` when present; otherwise
they are drawn with plain fonts.

## PlotHistos

`bin/PlotHistos` draws a [plot file](datumgui.md#plot-files) without a window, and makes plot files from data:

```
PlotHistos --all plots.xml [data.datum ...]               # named figures, PDF book, PowerPoint, LaTeX
PlotHistos --template plots.xml --select "*R2*" workflow.xml   # a plot file of the histograms of a workflow
```

The Datum files given after a plot file replace its inputs, so that one plot file serves the outputs of several jobs.
Without an output option, `PlotHistos` opens DatumGui on the plot file. `PlotHistos --help` lists the options.

## In C++

```cpp
Datum::GRAPHICS::Canvas canvas("spectra", "Transverse momentum spectra", 800, 600);
canvas.divide(2, 1);                                                   // two graphs side by side
Datum::GRAPHICS::Graph & left = canvas.graph(0);
left.show(Datum::GRAPHICS::GraphItem(&ptPions, "$\\pi^{+}$"));
left.show(Datum::GRAPHICS::GraphItem(&ptKaons, "$K^{+}$"));            // overlaid
left.setLogY(true);
Datum::GRAPHICS::PdfPainter pdf(800, 600);
canvas.draw(pdf);
pdf.write("spectra.pdf");
```

## DatumShell

`bin/DatumShell` runs a command language for exploring results and making figures without writing and compiling a
program. It has variables, numbers, texts, lists, ranges `a..b`, `if`/`else`, `while` and `for` loops, and functions
(`fun`); objects are made by calling their class (every class of the reflection registry: histograms, data points,
random generators, canvases, graphs) and used through their member functions and properties.

```
g = MersenneTwister(12345)
h = Histogram("h", "Gaussian numbers", 40, -4, 4)
for k in 1..10000 { h.fill(g.gauss(0, 1)) }
save("gauss.pdf", h)
pt = read("run.datum", "Single/MB_PiP/n1_pt")
c = Canvas("spectra", "Spectra", 800, 600)
c.divide(1, 1)
c.graph(0).add(pt)
c.graph(0).logY = true
c.save("pt.pdf")
```

`help` gives help on the language, on any function and on any registered class; `ls`, `read` and `write` list, read
and write the objects of Datum files.
