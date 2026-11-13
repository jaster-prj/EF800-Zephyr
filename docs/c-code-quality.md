# EF800 C Code-Quality Profile

## Scope and intent

This is a practical maintainability profile for EF800-owned production and test
C. It does not target a specific MISRA edition and is not a formal compliance
claim. Follow the surrounding EF800 style, keep responsibilities and mutable
state cohesive, and prefer clear control/data flow over mechanical rule-driven
rewrites.

The scope includes EF800 C compiled into the six firmware images, EF800 shared
modules, EF800-authored native tests, and the measurement-plugin host/package
sources. Zephyr, CMSIS, HAL, west modules, other third-party code, generated
build outputs, and generated headers are not EF800-owned findings. Include
directories and macros from those dependencies remain part of the target parse.
Use the source ownership inventory and actual target manifests to distinguish
these classes; do not treat a repository-wide `.c` scan as the compiled scope.

## Initial checks

1. Keep the existing GNU Arm GCC diagnostics enabled by each Zephyr target.
   Current target commands include `-Wall`, `-Wformat`, `-Wformat-security`,
   `-Wdouble-promotion`, `-Wpointer-arith`, and `-Werror=implicit-int`. Do not
   broaden warning flags across all images without checking build impact.
2. Use clang-tidy 22.1.8 with the following focused checks as the initial
   cross-target style/quality set:
   - `bugprone-sizeof-expression`
   - `bugprone-suspicious-memset-usage`
   - `readability-misleading-indentation`
3. Keep `clang-analyzer-core.*` checks supplemental/manual-review only for now.
   The pilot produced reports around Zephyr syscall output parameters that the
   analyzer did not model. Cppcheck 2.17.1 is selected for an EF800-source CI
   gate. Its compile-database invocation must filter analysis inputs to
   EF800-owned translation units and capture compiler predefined macros from
   the actual target compiler; diagnostics located only in Zephyr/vendor files
   are not EF800 findings. Parser/preprocessor failures in an EF800 translation
    unit remain failures, not suppressions. The repeatable runner is wired into
    CI. The complete configured clang-tidy and Cppcheck run now passes across
    all six firmware images, 26 native-test configurations, and plugin-host
    sources with zero EF800-owned findings. Reviewed suppressions are limited to
    fixed plugin ABI callbacks, Kconfig-controlled logging expansions, and
    intentional test fault-reset flows.
4. Do not run default clang-format as a gate until a project `.clang-format`
   has been reviewed against the existing source style.
5. CScout CC1, CC2, and CC3 function metrics are required CI checks, not an
   advisory-only report. CC1 counts control statements, CC2 also counts
   Boolean AND/OR and conditional operators, and CC3 additionally counts
   individual `switch` cases. CI must pass only explicit EF800-owned
   translation units from configured targets, compare results against a
   reviewed per-function baseline, and fail on unreviewed regressions or new
   functions exceeding the adopted limits. Do not analyze Zephyr/vendor source
   files as owned inputs. The CScout integration and baseline are still in
   progress; current CScout parsing has not yet produced usable target metrics.
   Metrics are approximations and multi-configuration analyses can be
   resource-intensive.

The initial focused-check baseline is zero findings across the pilot's six
image databases, native test configurations, and plugin host sources. This is a
baseline for only those checks and inputs; it is not a claim that the code has
no other quality issues.

## Target-aware invocation

Configure Zephyr with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, then run clang-tidy in
the devcontainer against the source paths owned by the target. Preserve the
target's C standard, definitions, generated configuration, include paths,
`-mcpu`/Thumb/ABI options, and source selection. clang-tidy does not accept a few
GNU Arm GCC-specific options in the current databases; for this tool invocation
only, remove exactly these tokens from an analysis copy of the database:

```text
-fno-printf-return-value
-fno-reorder-functions
-mfp16-format=ieee
-mtp=soft
--param=min-pagesize=0
-specs=picolibc.specs
```

Add `--extra-arg-before=--target=arm-none-eabi`. Keep the original compile
database untouched. This is a documented clang frontend adaptation, not the
GCC command and not proof that every compiler-specific semantic is modeled.
Report the adaptation with each analysis run and revalidate it when Zephyr,
GCC, or clang-tidy changes.

Example for one target after generating its database and an ownership-filtered
source list:

```sh
clang-tidy-22 \
  -p build/quality/ef800-main-clang-db \
  --extra-arg-before=--target=arm-none-eabi \
  --checks=-*,bugprone-sizeof-expression,bugprone-suspicious-memset-usage,readability-misleading-indentation \
  $(cat build/quality/ef800-main-owned-sources.txt)
```

Native-sim tests use their own compile databases and host target configuration;
do not pass ARM target flags to them. The freestanding plugin package sources
use their package CMake command configuration (C11, `-m32`, freestanding
options, plugin definitions and SDK includes), rather than an unrelated Zephyr
image database. Keep host tests, native packages, and ARM image results as
separate report groups.

The exact database adaptation, source ownership filtering, and repetition
across all configured builds is automated by `application/scripts/run-c-quality.py`.
The GitHub build workflow exports the per-image and native-test compile
databases, builds all six images, and runs this script on every push and pull
request. Do not analyze generated C files under a build directory as EF800-owned
input.

## Findings, baselines, and suppressions

- Fix clear, actionable findings when they are in scope and behavior can be
  preserved. Review warnings against the API contract and target behavior;
  static-analysis output is not automatically a defect.
- Existing findings that are not fixed must have a recorded disposition and
  rationale. New findings must not silently expand the baseline.
- A suppression must identify the tool/check, exact source scope, rationale,
  and reviewer. Prefer a local suppression over file-wide or project-wide
  disables. No suppressions were added during the pilot.
- Preserve the tool version, target/configuration, source list, flags removed
  for clang compatibility, report, and summary so the result can be repeated.
- This profile supports code review and regression prevention only. It does
  not establish MISRA compliance, hardware qualification, or correctness of
  unanalyzed configurations.

## Pilot evidence

See `openspec/changes/refactor-ef800-firmware-misra/evidence/analyzer-evaluation.md`
for versions, target/source counts, test outcomes, observed analyzer noise,
Cppcheck/CScout pilot limitations, and the outstanding native plugin runtime-test
failure.

## Continuous security checks

- `.github/workflows/build.yaml` runs GitHub CodeQL's `security-extended` C/C++
  query suite with the six firmware builds and native test compilation captured
  by the build tracer.
- The same workflow runs `pip-audit` against the repository's pinned
  Virtual-EF800 requirements and Zephyr's Python requirements.
- `.github/workflows/dependency-review.yaml` rejects pull requests introducing
  dependencies with known high/critical severity where GitHub's dependency
  review can identify the ecosystem.
- `.github/dependabot.yml` checks GitHub Actions, Python requirements, and the
  two Docker build contexts weekly.
- The west manifest pins Zephyr and additional modules to tags/commit hashes;
  Dependabot and dependency-review do not currently evaluate those west entries
  as a supported lockfile ecosystem. CodeQL scans compiled C/C++ for code-level
  weaknesses, but does not replace checking upstream advisories and reviewing
  pinned west revisions. This limitation must remain visible until west-module
  vulnerability monitoring is automated.
