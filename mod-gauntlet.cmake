# mod-gauntlet - AzerothCore module registration
# Sources under src/ are collected automatically by AzerothCore's module system.
#
# What is not automatic is the unit tests. AzerothCore builds them into its own
# unit_tests binary, and a module joins that binary by appending to two global
# properties that $CORE/src/test/CMakeLists.txt reads (lines 34-47): the test
# sources and the include directories they need. Plan section 5.1 asks for
# exactly this, and no module in this tree uses the mechanism yet, so the
# reasoning is written out rather than left to be rediscovered.
#
# Why it works. This file is include()d from $CORE/modules/CMakeLists.txt:314,
# inside the loop over the module list, so the appends below run while the
# modules subdirectory is being configured. The root CMakeLists.txt adds
# modules at line 143 and src/test at line 167, so both properties are already
# populated when src/test reads them. src/test itself is only added when
# BUILD_TESTING AND BUILD_APPLICATION_WORLDSERVER (line 148); with testing off
# the properties are simply never read, which costs nothing.
#
# unit_tests links the modules library ($CORE/src/test/CMakeLists.txt:49-52),
# so the Gauntlet:: symbols the tests call resolve from there. That is also the
# reason every source registered below must stay free of Player.h: unit_tests
# has no game objects, and a test translation unit that pulled one in would
# fail to compile long before it failed to link.
#
# Only the module's own src/ tree is collected into the modules library --
# GetPathToModuleSource ($CORE/src/cmake/macros/ConfigureModules.cmake:19-22)
# appends "/src" to the module path -- so tests/ is not compiled into
# worldserver, and the sources below are not built twice. The same recursion
# is why src/mechanics/attrition/*.cpp needs no cmake of its own.

# The glob is non-recursive on purpose: tests/tools/dump_legacy_rolls.cpp is a
# standalone program with its own main() and must never join a gtest binary.
# Like every glob in this build system it is evaluated at configure time, so a
# newly added test file needs cmake to run again -- which a fresh core build
# does anyway.
file(GLOB GAUNTLET_TEST_SOURCES "${CMAKE_CURRENT_LIST_DIR}/tests/*.cpp")

# The one case where registering would break the build rather than help it.
# unit_tests resolves the module's symbols out of the modules library, and a
# module is only in that library when its linkage is static -- a dynamic module
# becomes its own shared object and a disabled one is not built at all, and
# either way the test sources would be compiled with nothing to link them
# against. This is a loud skip, not a silent one: the message names the reason
# and appears in the same configure output as the registration it replaces.
ModuleNameToVariable(${SOURCE_MODULE} GAUNTLET_LINKAGE_VARIABLE)

if("${${GAUNTLET_LINKAGE_VARIABLE}}" STREQUAL "static")
  set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_SOURCES ${GAUNTLET_TEST_SOURCES})
  set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_INCLUDES "${CMAKE_CURRENT_LIST_DIR}/src")
  list(LENGTH GAUNTLET_TEST_SOURCES GAUNTLET_TEST_COUNT)
  message(STATUS "mod-gauntlet: registered ${GAUNTLET_TEST_COUNT} test source(s) with unit_tests")
else()
  message(STATUS
    "mod-gauntlet: NOT registering unit tests -- module linkage is "
    "'${${GAUNTLET_LINKAGE_VARIABLE}}', and unit_tests can only resolve the module's symbols "
    "from the static modules library. Build with -DMODULES=static to run them.")
endif()
