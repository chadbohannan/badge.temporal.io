#pragma once
// Scripted play: a text file of commands drives the game against a fixed
// 16ms clock and writes PNG snapshots. Exit codes: 0 script finished, 1 an
// assert or until failed, 2 the script itself is malformed.
//
//   new                      start a new game (implied before the first tick)
//   load k=v ...             start from a save: room hp silver arts weapon
//                            won loot ex ey
//   hold <dirs|none>         set the stick: any of u d l r, e.g. "hold ur"
//   press <a|b|x|y>          one edge-triggered press on the next tick
//   wait <ms>                advance the clock
//   until <expr> <ms>        tick until a status expression holds, or fail
//   snap [name]              write <outdir>/<name>-oled.png and -matrix.png
//   status                   print the status line
//   assert <expr> ...        fail unless every expression holds
//
// Expressions are k=v, k>=v or k<=v over the status fields (see status.h).
// `#` starts a comment.
#include <istream>
#include <string>

namespace script {

int run(std::istream& in, const std::string& outDir);

}  // namespace script
