# TREEWALK books

TREEWALK reads Choose-Your-Own-Adventure books from a read-only SQLite file.
Books are written in [Twee 3](https://twinery.org/cookbook/terms/terms_twee.html)
(Twine can export it) and compiled by `firmware/scripts/treewalk_build.py`.

```sh
python3 scripts/treewalk_build.py build -o treewalk.db treewalk/books/lantern-keeper.twee
python3 scripts/treewalk_build.py build -o treewalk.db --merge my-book.twee   # add to an existing file
python3 scripts/treewalk_build.py check treewalk.db
```

Run these from `firmware/`. `build` checks its own output; `check` re-checks any
database.

## Writing a book

```
:: StoryTitle
The Lantern Keeper

:: Start
Fog rolls over the cliffs. A dark lighthouse stands at the end of the path.

[[Climb the spiral stairs->Stairs]]
[[Check the cellar->Cellar]]

:: Stairs
...
```

- `StoryTitle` names the book. Without it the file name is used. Renaming a book
  makes it a new book on the badge, because the book ID is a hash of the name.
- The start passage is `Start`, or the `start` key of a `StoryData` passage.
- A passage is one screen of text. Links `[[label->target]]`, `[[label|target]]`,
  `[[target<-label]]` and `[[target]]` become choices, in order: the first is
  the `A` button, the second `B`, the third `Y`. At most three.
- A line that holds only links is removed from the text. A link inside a line
  is replaced by its label.
- A passage with no links is an ending.
- Every passage must be reachable from the start, every link must resolve, and
  links must not loop back (the story is a DAG). A passage may be the target of
  several links.
- Do not rename a passage once a book ships: the passage name is hashed into the
  node ID that a badge saves as its place.

The screen is 128x64, so keep text short. The tool warns above 600 characters
of text or 40 characters in a choice label, and warns on characters the badge
font may not draw. Curly quotes, en/em dashes, ellipses and non-breaking spaces
are converted to plain ASCII.

`books/lantern-keeper.twee` is a small example that also serves as a test book.

## Rebuilding the shipped database

The badge ships `firmware/initial_filesystem/treewalk/treewalk.db`. After you change a book, run from `firmware/`:

```sh
python3 scripts/treewalk_build.py build -o initial_filesystem/treewalk/treewalk.db treewalk/books/*.twee
TEMPORAL_BADGE_REGEN_MANIFEST=1 python3 scripts/generate_startup_files.py
```

A unit test fails if the committed database no longer matches the books.
