# C Code-Quality Tool Evaluation

Assessment date: 2026-11-12. This project does not require a particular MISRA edition or formal compliance claim. The goal is practical style and correctness feedback using repository conventions, compiler diagnostics, and suitable tooling. The evaluation below is a target-aware pilot, not a complete repository-wide quality baseline.

## Environment and build metadata

- Reused the running `EF800_Devcontainer`; west topdir is `/workspace`, manifest is `application/manifest-repo/west.yml`, and Zephyr is `v4.4.2`.
- Installed Cppcheck `2.17.1` and clang-tidy `22.1.8` in the devcontainer. No repository source or tracked build configuration was changed for the trial.
- Generated six isolated Zephyr configurations under ignored `build/misra-pilot/fresh-*` directories. The EF800Application configuration was also fully built. The compile databases contain the actual GNU Arm GCC `14.3.0` C17 options, Zephyr Kconfig/generated include paths, and target flags.
- EF800-owned application/shared C translation units identified by the six compile databases: EF800Application 54, BackupApplication 13, L496 bootloader 11, keyboard application 7, keyboard bootloader 4, keyboard updater 4. Generated files and Zephyr/west module sources were excluded by path ownership.
- `west twister -T application/tests -p native_sim --inline-logs --no-clean -x CMAKE_EXPORT_COMPILE_COMMANDS=ON` passed all 26 configurations and 175 test cases. Compile databases were generated for those configurations.
- After extracting keyboard matrix debounce and application I2C service, the
  repeatable `application/scripts/run-c-quality.py` runner passed against all
  six configured image databases (101 EF800-owned source entries), all 26
  currently discovered native-test compile databases, and the plugin-host
  database. Every invocation exited successfully with zero warnings/errors.

## Tool observations

### Cppcheck

- Cppcheck `2.17.1` with general warning/style/performance/portability checks could import the Zephyr compile database but failed preprocessing at `zephyr/toolchain.h` with `Invalid/unknown toolchain configuration`.
- Supplying guessed compiler/architecture definitions only exposed additional Zephyr preprocessor errors. Those runs are not valid target-aware results and are not used as evidence.
- Cppcheck ran on the standalone plugin host compile database, but produced low-signal suggestions (including callback constness incompatible with the plugin callback ABI, branch-limit notes, and an assignment-in-assert test warning). Do not use that output as a blanket gate without narrowing and reviewing checks.

### clang-tidy

- clang-tidy can analyze the six Zephyr databases after dropping a small explicit set of GCC-only options it does not accept and adding `--target=arm-none-eabi`. The trial retained the target CPU/Thumb/ABI flags, C17 mode, defines, generated Zephyr configuration, and include paths. This adaptation is a tool-compatibility workaround, not the compiler invocation and not proof that every compiler semantic is preserved; keep the removed options documented and revalidate when toolchains change.
- A focused check set (`bugprone-sizeof-expression`, `bugprone-suspicious-memset-usage`, `readability-misleading-indentation`) completed successfully over the EF800-owned sources in all six image compile databases: 101 source entries after the keyboard-family extractions and no findings. It also completed over the standalone plugin host test sources and the native test configurations.
- The native test pilot applied the same focused checks to EF800-owned test/shared inputs for all 26 configurations. A separate core-analyzer trial reported one null-dereference warning inside a Zephyr byte-order header while analyzing the virtual-USB test. This is external code and was not treated as an EF800 finding.
- Adding `clang-analyzer-core.uninitialized.Assign` / `clang-analyzer-core.NullDereference` reported three EF800Application warnings at ADC/GPIO API call sites. The analyzer does not model Zephyr's successful syscall output writes in these paths; keep these checks supplemental until each warning can be reviewed, and do not gate on them as-is.
- Host `clang-tidy` initially could not use the container compile database because its `/workspace/...` working directories were unavailable on the host. Running clang-tidy `22.1.8` in the devcontainer against the adjusted database resolved that environment mismatch.
- No `.clang-format` configuration exists. Do not apply default clang-format output blindly; first agree on formatting conventions and add a project configuration if automated formatting is wanted.

### CScout complexity-metric CI gate

- CScout documents per-function CC1 (control statements), CC2 (also Boolean
  AND/OR and conditional operators), and CC3 (also individual `switch` cases),
  plus preprocessor-aware call graphs. These metrics may help locate and
  compare high-branching functions during the domain refactor.
- CScout was built from source in the reusable devcontainer and invoked with a
  workspace containing only one explicit EF800-owned translation unit. This
  correctly scopes analysis targets; it does not require treating Zephyr source
  files as project inputs.
- The target pilot still fails before producing function metrics because
  CScout's parser rejects GCC/Zephyr preprocessing dependencies (including
  compiler-specific builtins and preprocessor constructs). CScout is required
  as an automated CI metric gate by the current implementation request; it is
  not classified as advisory. Do not report a pass or mark the new gate task
  complete until the explicit-source pipeline emits usable CC1/CC2/CC3 metrics.
- The proposed gate compares a reviewed per-function baseline, fails on
  unreviewed increases, and checks new functions against adopted limits. No
  limits or baseline have been accepted yet. Do not treat metric reduction as
  proof of a good refactor.
- References: <https://www.spinellis.gr/cscout/doc/metrics.html> and
  <https://www.spinellis.gr/cscout/>.

### Compiler diagnostics and plugin sources

- Existing Zephyr builds already use GNU Arm GCC warning options including `-Wall`, `-Wformat`, `-Wformat-security`, `-Wdouble-promotion`, `-Wpointer-arith`, and `-Werror=implicit-int`. These build checks remain the compiler-diagnostic baseline; this pilot did not broaden warnings.
- The measurement-plugin host build passed all 5 CTest cases. Focused clang-tidy checks completed without findings on its 10 EF800-owned host source units.
- The optional plugin `PM_BUILD_NATIVE_SIM=ON` build generated and verified both ELTA3 and FTS3XX packages. Its CTest run passed 7/8; `measurement_plugins_native_sim_runtime` aborted at `tests/test_native_package.c:69` when the fake worker syscall received an operation outside its handled port read/write cases. This existing test failure is recorded for follow-up, not hidden as an analyzer result.
- The standalone plugin package images are built through custom CMake commands and are not represented by CMake's compile database. A direct clang-tidy trial on the freestanding package sources completed with zero focused-check findings, using the package's `-m32`, C11, freestanding, include, and plugin definitions.

## Current profile and gate work

- Existing gate: GNU Arm GCC's target warnings plus clang-tidy `22.1.8` with the three focused checks listed above, using real target compile databases and a documented adaptation for GCC-only flags. Keep clang-analyzer findings supplemental/manual-review only until Zephyr syscall modeling noise is resolved.
- Cppcheck is selected for an EF800-source gate. The repeatable runner filters configured compile databases to owned translation units, captures macros with the actual target compiler, and is wired into CI. A pilot found and helped fix an output-pointer issue in the updater session API. The complete configured run now passes across all six firmware images, all 26 native-test compile databases, and plugin-host sources with zero owned findings. Narrow reviewed suppressions preserve fixed plugin ABI callbacks, Kconfig-controlled log expansions, and intentional test fault-reset flows.
- CScout CC1/CC2/CC3 is required as an automated metric gate. Explicit EF800 file selection was tested, but target metrics are not yet emitted because CScout fails on toolchain/header constructs. The integration remains open; do not describe CScout as advisory or count the pilot as passing.
- Scope analysis to EF800-owned C translation units from the source manifests. Exclude Zephyr/vendor/west modules and generated build outputs explicitly. Keep host plugin and native test configurations separate from ARM image results.
- Focused-check baseline in this pilot is zero findings. No broad or source-wide suppressions were introduced. Any future suppression should identify the check, exact code scope, rationale, and reviewer.
- The compile-database adaptation, ownership filtering, target compiler macro capture, and CI invocation are automated and the complete Cppcheck/clang-tidy runner passes. The CScout metrics pipeline remains blocked on parsing and prevents completion of the combined gate. The previous optional plugin native-sim package-runtime test failure remains a separate recorded gap; the CI-host CTest suite is distinct evidence.

## Limitations and next work

- This pilot does not constitute MISRA assessment or conformance, and no standard edition is part of the project's requirement.
- The six image compile databases are freshly configured; only EF800Application was fully built in the isolated pilot directory. Native tests and plugin tests were built and executed as described above.
- Report exact analyzer/compiler versions, removed flags, inputs, and output with any future quality run. Do not claim checks for an image/test/package configuration that was not analyzed.
