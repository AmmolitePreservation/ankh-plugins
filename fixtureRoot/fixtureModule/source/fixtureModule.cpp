// A DLL the fixture game loads and frees in its module mode, for the tests of watchModuleLoads.

// A value no other function returns: the linker folds identical functions, and the 64-bit runtime has a stub that
// returns 1, which the test's hook would change too.
extern "C" __declspec(dllexport) int __cdecl fixtureModuleValue() {
  return 7319;
}
