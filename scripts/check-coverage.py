#!/usr/bin/env python3
"""Merge Cobertura reports and enforce Podlord coverage gates."""

from __future__ import annotations

import fnmatch
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


LINE_GATE = 95.0
# Coverlet branch data for this Avalonia/C# app includes many compiler-generated
# property, switch, async, and TLS setup branches. Keep this gate enforceable while
# the report still prints the worst branch debt for hardening work.
BRANCH_GATE = 80.0
PROJECTS = ("Podlord.App", "Podlord.Core", "Podlord.Kubernetes")
EXCLUDES = (
    "src/Podlord.App/*.axaml",
    "src/Podlord.App/*.axaml.cs",
    "src/Podlord.App/Program.cs",
    "src/Podlord.App/KindGlyph.cs",
    "src/Podlord.App/RadarWaterLayer.cs",
    "src/Podlord.App/YamlSyntaxColorizer.cs",
    "src/Podlord.App/MainWindowViewModel.cs",
    "src/Podlord.App/AlertRuleRowViewModel.cs",
    "src/Podlord.App/AlertSoundPlayer.cs",
    "src/Podlord.App/Controls/*.cs",
    "src/Podlord.App/InspectorSortManager.cs",
    "src/Podlord.App/LogSyntaxColorizer.cs",
    "src/Podlord.App/ResourceReferenceTooltipBuilder.cs",
    "src/Podlord.App/StatusBrushConverter.cs",
    "src/Podlord.App/WorkspaceModels.cs",
)


def main() -> int:
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path.cwd().resolve()
    reports = sorted(root.glob("tests/*/TestResults/*/coverage.cobertura.xml"))
    if not reports:
        print("coverage: no Cobertura reports found under tests/*/TestResults", file=sys.stderr)
        return 2

    files: dict[str, dict[int, int]] = {}
    branches: dict[str, dict[int, tuple[int, int]]] = {}
    for report in reports:
        merge_report(root, report, files, branches)

    line_covered = sum(sum(hit > 0 for hit in lines.values()) for lines in files.values())
    line_total = sum(len(lines) for lines in files.values())
    branch_covered = sum(sum(covered for covered, _ in lines.values()) for lines in branches.values())
    branch_total = sum(sum(total for _, total in lines.values()) for lines in branches.values())
    line_rate = percent(line_covered, line_total)
    branch_rate = percent(branch_covered, branch_total)

    print(f"coverage: reports={len(reports)} files={len(files)}")
    print(f"coverage: line   {line_covered}/{line_total} = {line_rate:.2f}% (gate {LINE_GATE:.2f}%)")
    print(f"coverage: branch {branch_covered}/{branch_total} = {branch_rate:.2f}% (gate {BRANCH_GATE:.2f}%)")

    if line_rate < LINE_GATE or branch_rate < BRANCH_GATE:
        print("coverage: gate failed; worst files:")
        for item in worst_files(files, branches)[:20]:
            print(item)
        return 1

    print("coverage: gate passed")
    return 0


def merge_report(
    root: Path,
    report: Path,
    files: dict[str, dict[int, int]],
    branches: dict[str, dict[int, tuple[int, int]]],
) -> None:
    document = ET.parse(report)
    for cls in document.findall(".//class"):
        rel = normalize_source(root, cls.attrib.get("filename", ""))
        if rel is None or excluded(rel):
            continue

        line_hits = files.setdefault(rel, {})
        branch_hits = branches.setdefault(rel, {})
        for line in cls.findall("./lines/line"):
            number = int(line.attrib["number"])
            line_hits[number] = max(line_hits.get(number, 0), int(line.attrib.get("hits", "0")))
            if line.attrib.get("branch", "").lower() == "true":
                match = re.search(r"\((\d+)/(\d+)\)", line.attrib.get("condition-coverage", ""))
                if match is None:
                    continue

                covered, total = int(match.group(1)), int(match.group(2))
                previous = branch_hits.get(number, (0, 0))
                branch_hits[number] = (max(previous[0], covered), max(previous[1], total))


def normalize_source(root: Path, filename: str) -> str | None:
    path = filename.replace("\\", "/")
    if "/obj/" in path or path.endswith((".g.cs", ".g.i.cs")):
        return None

    if path.startswith("src/"):
        return path

    for project in PROJECTS:
        prefix = f"{project}/"
        if path.startswith(prefix):
            return f"src/{path}"

    basename = Path(path).name
    for project in PROJECTS:
        candidate = root / "src" / project / basename
        if candidate.exists():
            return str(candidate.relative_to(root)).replace("\\", "/")

    return None


def excluded(path: str) -> bool:
    return any(fnmatch.fnmatch(path, pattern) for pattern in EXCLUDES)


def worst_files(
    files: dict[str, dict[int, int]],
    branches: dict[str, dict[int, tuple[int, int]]],
) -> list[str]:
    rows = []
    for path, lines in files.items():
        line_total = len(lines)
        line_covered = sum(hit > 0 for hit in lines.values())
        branch_map = branches.get(path, {})
        branch_total = sum(total for _, total in branch_map.values())
        branch_covered = sum(covered for covered, _ in branch_map.values())
        rows.append((
            percent(line_covered, line_total),
            percent(branch_covered, branch_total),
            line_covered,
            line_total,
            branch_covered,
            branch_total,
            path,
        ))

    rows.sort(key=lambda item: (item[0], item[1], item[6]))
    return [
        f"  {line_rate:6.2f}% L {line_covered:4}/{line_total:<4} | "
        f"{branch_rate:6.2f}% B {branch_covered:4}/{branch_total:<4} | {path}"
        for line_rate, branch_rate, line_covered, line_total, branch_covered, branch_total, path in rows
    ]


def percent(covered: int, total: int) -> float:
    return 100.0 if total == 0 else covered / total * 100.0


if __name__ == "__main__":
    raise SystemExit(main())
