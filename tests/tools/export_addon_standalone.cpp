#include "Gauntlet.h"
#include "GauntletRegistry.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
// Mirrors src/GauntletAddon.h:39 exactly: `static constexpr uint16 Version = GeneratorVersion;`
namespace Gauntlet { struct Addon { static constexpr uint16 Version = GeneratorVersion; }; }
namespace Gauntlet {
namespace {
#include "export_block.inc"   // see README-export.md for how this is extracted
}
}
int main() { std::string s = Gauntlet::BuildAddonData(); fwrite(s.data(), 1, s.size(), stdout); }
