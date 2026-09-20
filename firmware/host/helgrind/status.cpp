#include "status.h"

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "HelgrindGame.h"

namespace status {
namespace {

struct Field { const char* name; double value; };

std::vector<Field> fields() {
  helgrind::GameStatus s = helgrind::gameStatus();
  return {{"room", double(s.room)},     {"x", double(s.x)},
          {"y", double(s.y)},           {"hp", double(s.hp)},
          {"silver", double(s.silver)}, {"arts", double(s.arts)},
          {"weapon", double(s.weapon)}, {"won", s.won ? 1.0 : 0.0},
          {"mode", double(s.mode)},     {"enemies", double(s.enemies)}};
}

}  // namespace

bool matches(const std::string& expr, bool& known) {
  known = false;
  size_t op = expr.find_first_of("<>=");
  if (op == std::string::npos) return false;
  std::string key = expr.substr(0, op);
  char c = expr[op];
  size_t vpos = op + (expr[op + 1] == '=' ? 2 : 1);
  double v = std::atof(expr.c_str() + vpos);
  for (const Field& f : fields()) {
    if (key != f.name) continue;
    known = true;
    if (c == '>') return f.value >= v;
    if (c == '<') return f.value <= v;
    return std::fabs(f.value - v) < 0.5;  // equality within half a unit, so
  }                                        // fractional positions compare
  return false;
}

std::string line() {
  std::ostringstream o;
  for (const Field& f : fields()) o << f.name << "=" << f.value << " ";
  return o.str();
}

}  // namespace status
