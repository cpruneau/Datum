# Credits

## Authors

| | |
|---|---|
| **Claude Pruneau** | Department of Physics and Astronomy, Wayne State University, Detroit, USA |
| **Victor Gonzalez** | Department of Physics and Astronomy, Wayne State University, Detroit, USA |
| **Oveis Sheibani** | Department of Physics and Astronomy, Wayne State University, Detroit, USA |

Parts of the code and of this documentation of Datum 1.0 were written with the assistance of **Claude**, an AI model
made by [Anthropic](https://www.anthropic.com).

## Acknowledgments

This work was supported in part by the **U.S. Department of Energy**, Office of Science, under Grant
**No. DE-FG02-92ER40713**.

## How to cite

If you use Datum in your work, please cite the paper describing it:

> C. Pruneau, V. Gonzalez and O. Sheibani, *Datum 1.0: a C++ framework for simulating and analysing correlation and
> fluctuation observables in high-energy collisions*, in preparation (2026).

```bibtex
@unpublished{Datum:2026,
  author = {Pruneau, Claude and Gonzalez, Victor and Sheibani, Oveis},
  title  = {{Datum 1.0: a C++ framework for simulating and analysing correlation and fluctuation observables
             in high-energy collisions}},
  note   = {In preparation},
  year   = {2026},
  url    = {https://github.com/cpruneau/Datum}
}
```

This reference will be updated when the paper is published.

## License

Datum is free software: it can be redistributed and modified under the terms of the
[GNU Lesser General Public License](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html), version 2.1 or (at
your option) any later version, as published by the Free Software Foundation. It is distributed in the hope that it
will be useful, but without any warranty. See the file `LICENSE` of the distribution.

## Software used by Datum

Distributed with Datum, under their own licenses:

| Software | Used for | License |
|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | the widgets of DatumGui | MIT |
| [GLFW](https://www.glfw.org) | the windows of DatumGui | zlib/libpng |
| [NanoSVG](https://github.com/memononen/nanosvg) | rasterising typeset labels | zlib |
| [yaml-cpp](https://github.com/jbeder/yaml-cpp) | HEPData YAML files | MIT |

Used when installed (optional):

- [PYTHIA 8](https://pythia.org), [Herwig 7](https://herwig.hepforge.org): event generators;
- [FastJet](https://fastjet.fr): jet finding;
- [ROOT](https://root.cern): conversion of `.root` files;
- LaTeX and `dvisvgm`: typesetting of the labels of figures.

The thermal-model generator Therminator3 is derived from Therminator 2 (M. Chojnacki, A. Kisiel, W. Florkowski and
W. Broniowski). Datum also reads events produced by EPOS and PHSD.
