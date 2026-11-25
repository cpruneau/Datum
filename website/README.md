# The Datum web site

Sources of the web site of Datum: pages written in Markdown (`docs/`), built with
[MkDocs](https://www.mkdocs.org) and the [Material](https://squidfunk.github.io/mkdocs-material/) theme, and the API
reference made by Doxygen from the sources of Datum.

## Setup (once)

```
brew install doxygen graphviz         # the API reference and its diagrams (macOS)
```

MkDocs needs nothing more: the first `./build_site.sh` makes a Python virtual environment, `website/.venv`, and
installs MkDocs 1.x and the Material theme in it (Homebrew's Python does not allow `pip install` outside of one).
`./build_site.sh --setup` makes it again.

## Build

```
cd website
./build_site.sh --api     # pages + API reference (Doxygen run again: after changes to the sources)
./build_site.sh           # pages only (the API reference made last is copied again)
./build_site.sh --serve   # preview at http://127.0.0.1:8000, refreshed as the pages are edited
```

The site is written to `website/site`; open `site/index.html` to check it. `site/` and `build/` are not kept in git.

## Publish

Copy the content of `website/site` to the folder of the web server, for example

```
rsync -av --delete website/site/ user@server.wayne.edu:public_html/datum/
```

All links are relative, so the site works in any folder of the server.

## Edit

- Pages: `docs/*.md` and `docs/manual/*.md`; the menu is `nav` in `mkdocs.yml`.
- Pictures: `docs/images/`.
- The versions: `docs/download.md`; the reference of the paper: `docs/credits.md` (and the box at the end of
  `docs/index.md`).
- The API reference: `Doxyfile` (the files read, the diagrams).
