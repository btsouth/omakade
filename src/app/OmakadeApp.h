#pragma once

class AppHarness;

// Builds the Omakade window and its services, runs the event loop and returns the exit code.
// The shipped omakade binary passes no harness. omakade-uitest (src/selftest) passes one, which
// adds the fixtures, render paths and self-tests the test suite drives.
int runOmakade(int argc, char* argv[], AppHarness* harness);
