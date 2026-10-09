#!/bin/sh
set -eu
umask 077
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:?Usage: export-native-locales.sh OUTPUT_JSON_OR_--check}
DOTNET=${PODLORD_DOTNET:-dotnet}
command -v "$DOTNET" >/dev/null 2>&1 || { printf '%s\n' 'The catalog export requires the .NET 10 SDK.' >&2; exit 2; }
RUN=$(mktemp -d "${TMPDIR:-/tmp}/podlord-locales.XXXXXX")
trap 'find "$RUN" -depth -delete' 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
export DOTNET_CLI_HOME="$RUN/home" DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_GENERATE_ASPNET_CERTIFICATE=false DOTNET_NOLOGO=1
cat > "$RUN/export.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <JsonSerializerIsReflectionEnabledByDefault>false</JsonSerializerIsReflectionEnabledByDefault>
  </PropertyGroup>
  <ItemGroup><Compile Include="Program.cs"/><Compile Include="$(ReferenceSource)" Link="PodlordLocalizer.cs"/></ItemGroup>
</Project>
EOF
printf '%s\n' '<configuration><packageSources><clear/></packageSources></configuration>' > "$RUN/NuGet.Config"
cat > "$RUN/Program.cs" <<'EOF'
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using Podlord.App;

var source = File.ReadAllText(args[0]);
var native = JsonSerializer.Deserialize(File.ReadAllText(args[2]), CatalogJson.Default.Catalog)
    ?? throw new InvalidDataException("The native language catalog is empty.");
var retired = new HashSet<string>(["settings.themeIntensity", "settings.themeIntensityHelp"], StringComparer.Ordinal);
var keys = Regex.Matches(source, "\"(?<key>[a-z]+(?:\\.[a-zA-Z][a-zA-Z0-9]*)+)\"")
    .Select(match => match.Groups["key"].Value).Where(key => !retired.Contains(key)).Distinct().Order(StringComparer.Ordinal).ToArray();
// Reference refreshes own shared strings, not native-only controls.
var english = new SortedDictionary<string, string>(native.English, StringComparer.Ordinal);
foreach (var key in retired) english.Remove(key);
foreach (var key in keys) english[key] = PodlordLocalizer.Text(key, "en");
var languages = PodlordLocalizer.SupportedLocales.Select(option => {
    var retained = native.Languages.SingleOrDefault(language => language.Code == option.Code);
    var text = retained is null ? new SortedDictionary<string, string>(StringComparer.Ordinal)
        : new SortedDictionary<string, string>(retained.Text, StringComparer.Ordinal);
    foreach (var key in retired) text.Remove(key);
    if (option.Code != "system")
        foreach (var key in keys) {
            var translated = PodlordLocalizer.Text(key, option.Code);
            if (translated != english[key]) text[key] = translated;
            else text.Remove(key);
        }
    return new Language(option.Code, option.NativeName, text);
}).ToArray();
var catalog = new Catalog(1, english, languages);
File.WriteAllText(args[1], JsonSerializer.Serialize(catalog, CatalogJson.Default.Catalog) + "\n");

internal sealed record Language(string Code, string Name, SortedDictionary<string, string> Text);
internal sealed record Catalog(int Version, SortedDictionary<string, string> English, Language[] Languages);
[JsonSourceGenerationOptions(PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase, WriteIndented = true)]
[JsonSerializable(typeof(Catalog))]
internal partial class CatalogJson : JsonSerializerContext;
EOF
"$DOTNET" build "$RUN/export.csproj" -p:ReferenceSource="$ROOT/src/Podlord.App/PodlordLocalizer.cs" --configfile "$RUN/NuGet.Config" --nologo > "$RUN/build.log" 2>&1 || { cat "$RUN/build.log" >&2; exit 1; }
"$DOTNET" "$RUN/bin/Debug/net10.0/export.dll" "$ROOT/src/Podlord.App/PodlordLocalizer.cs" "$RUN/locales.json" "$ROOT/native/ui/locales.json"
if [ "$OUT" = '--check' ]; then
    cmp "$RUN/locales.json" "$ROOT/native/ui/locales.json"
else
    cp "$RUN/locales.json" "$OUT"
fi
