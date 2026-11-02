# Fixed local runtime

Run `Build_Runtime.cmd` explicitly from any working directory. It discovers
CMake through `MONAKA_CMAKE`, PATH, or environment-derived installed locations
and builds the existing products into `Build/runtime/Release`. The C++ toolchain,
Python 3 and the tracked fixed protocol dependencies must be available.
`-WithTests` compiles the software test targets; this build command never runs
the products, tests, installer, driver or device controls.

The workspace's `launchers/Start_ViveUltimateTrackerBridge.cmd` is the single
normal entry. Its private helpers live outside this repository and resolve this
checkout relative to their own location. It starts the prebuilt console service;
this source tree has no GUI target. No UX checkout or historical GUI is used.
Startup never builds. Optional `--check` reads the executable/config prerequisites and reports file
presence; it does not inspect processes, create a journal directory, open HID,
or claim configuration validation or runtime/physical readiness. Prepare the
installation-specific configuration explicitly from the documented example;
the launcher never copies or rewrites it. The normal entry uses the repository
configuration and the existing per-user revision journal, with Release products
and the default port. Advanced diagnostic invocation remains an explicit native
CLI operation. Unknown or extra entry arguments fail
before process inspection or launch. Missing configuration and executable
prerequisites are reported together.

`Setup_Altra_Trackers.cmd` and its helper remain explicit HIL/bring-up maintenance
tools. They are outside normal startup and must not be launched during an offline
launcher check. The read-only `scripts/vive_launcher_process.ps1` library remains
for process-identity regression tests; it is not an application entry.

`Build/` is the canonical local output root. Revision tests and CI have separate
subdirectories there; `Build/Distributions` contains release archives/receipts
and is not a normal runtime locator. Explicit external output parameters remain
operator choices. No old task worktree, dated package, sibling SDK or previously
built executable is used as a fallback. Required tracked kits, source assets,
frozen evidence, and old ignored worktree/source directories remain preserved.
