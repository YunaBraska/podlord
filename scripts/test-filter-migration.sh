#!/bin/sh
set -eu
umask 077
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${1:?Usage: test-filter-migration.sh NATIVE_BUILD_DIRECTORY FRESH_EVIDENCE_DIRECTORY}
OUT=${2:?A fresh evidence directory is required}
DOTNET=${PODLORD_DOTNET:-dotnet}
BIN="$BUILD/field-filter-ui-test"
[ -x "$BIN" ] || { printf '%s\n' 'Build field-filter-ui-test first.' >&2; exit 2; }
command -v "$DOTNET" >/dev/null 2>&1 || { printf '%s\n' 'The reference export requires the .NET 10 SDK; set PODLORD_DOTNET to its executable.' >&2; exit 2; }
[ ! -e "$OUT" ] || { printf '%s\n' 'Evidence output already exists; refusing to replace it.' >&2; exit 2; }
mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
RUN=$(mktemp -d "${TMPDIR:-/tmp}/podlord-filter-migration.XXXXXX")
cleanup() { find "$RUN" -depth -delete; }
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir "$RUN/tmp" "$RUN/profile"
TMPDIR="$RUN/tmp"
PODLORD_CONFIG_HOME="$RUN/profile"
DOTNET_CLI_HOME="$RUN/dotnet-home"
export TMPDIR PODLORD_CONFIG_HOME DOTNET_CLI_HOME
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_GENERATE_ASPNET_CERTIFICATE=false DOTNET_NOLOGO=1
cat > "$RUN/export.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="Program.cs" />
    <Compile Include="$(ReferenceSource)" Link="FilterPresetStore.cs" />
  </ItemGroup>
</Project>
EOF
cat > "$RUN/NuGet.Config" <<'EOF'
<configuration><packageSources><clear /></packageSources></configuration>
EOF
cat > "$RUN/Program.cs" <<'EOF'
using Podlord.App;

var defaults = FilterPresetStore.DefaultFilter;
FilterPresetStore.Save([
    defaults,
    defaults with {
        Name = "Pod alpha", Search = "Pod", Id = "not-an-existing-resource-id",
        NameFilter = "alpha", Namespace = "team-a", Kind = "Pod", Cluster = "local", Status = "Running",
        Age = ">=1m <2m", Node = "node-a", Image = "busybox:1", Ready = "1/1", Restarts = "=0", Owner = "owner-a", Limit = "7"
    },
    defaults with { Name = "Problem pods", ProblemsOnly = true, Kind = "Pod", Issue = "CrashLoopBackOff" },
    defaults with { Name = "Recently active", ActivityOnly = true, Kind = "Pod" },
    defaults with { Name = "Measured workloads", Cpu = ">0", Memory = ">0", Storage = ">0" }
]);
EOF
"$DOTNET" build "$RUN/export.csproj" -p:ReferenceSource="$ROOT/src/Podlord.App/FilterPresetStore.cs" \
    --configfile "$RUN/NuGet.Config" --nologo > "$OUT/reference-build.log" 2>&1 || { cat "$OUT/reference-build.log" >&2; exit 1; }
"$DOTNET" "$RUN/bin/Debug/net10.0/export.dll" > "$OUT/reference-export.log" 2>&1
SOURCE="$RUN/profile/filter-presets.json"
cp "$SOURCE" "$OUT/reference-filter-presets.json"
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
for style in Basic Fusion; do
    for scenario in fields problems activity missing_metrics repeat conflict restart; do
        name="reference_presets_$scenario"
        QT_QUICK_CONTROLS_STYLE="$style" PODLORD_FIELD_FILTER_SCREENSHOT="$OUT/$style-$scenario.png" \
            "$BIN" "$name" "$SOURCE" > "$OUT/$style-$scenario.log" 2>&1 || { cat "$OUT/$style-$scenario.log" >&2; exit 1; }
        printf '%s\n' "$style/$name passed"
    done
done
cmp "$SOURCE" "$OUT/reference-filter-presets.json"
printf '%s\n' 'The real reference export was retained byte-for-byte. Screenshots are native headless evidence, not desktop parity.'
