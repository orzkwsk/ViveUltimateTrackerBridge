# Fixed local runtime

Run `Build_Runtime.cmd` explicitly from any working directory. It discovers
CMake through `MONAKA_CMAKE`, PATH, or environment-derived installed locations
and builds the existing products into `Build/runtime/Release`. The C++ toolchain,
Python 3 and the tracked fixed protocol dependencies must be available.
`-WithTests` compiles the software test targets; this build command never runs
the products, tests, installer, driver or device controls.

`Start_VIVE_Backend.cmd` starts the prebuilt service. It does not build on
startup. `--check` reads the executable/config prerequisites and reports file
presence; it does not inspect processes, create a journal directory, open HID,
or claim configuration validation or runtime/physical readiness. Prepare the
installation-specific configuration explicitly from the documented example;
the launcher never copies or rewrites it. Direct PowerShell callers retain the
existing explicit configuration, journal, port, diagnostics and build-config
parameters; `-NoBuild` remains accepted for compatibility.
Relative configuration and journal arguments are resolved from the repository
root, regardless of the caller's working directory. Unknown arguments fail
before process inspection or launch. Missing configuration and executable
prerequisites are reported together.

`Build/` is the canonical local output root. Revision tests and CI have separate
subdirectories there; `Build/Distributions` contains release archives/receipts
and is not a normal runtime locator. Explicit external output parameters remain
operator choices. No old task worktree, dated package, sibling SDK or previously
built executable is used as a fallback. Required tracked kits, source assets,
frozen evidence, and old ignored worktree/source directories remain preserved.
