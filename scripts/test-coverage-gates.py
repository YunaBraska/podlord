#!/usr/bin/env python3
"""Exercise the coverage gate through its command-line entrypoint."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class CoverageGates(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="podlord-coverage-")
        self.addCleanup(self.directory.cleanup)
        self.reports = Path(self.directory.name)

    def run_gate(self, lines=None, branches=None):
        if lines is not None:
            rows = "".join(
                f'<line number="{number}" hits="{int(number <= lines)}"'
                + (f' branch="true" condition-coverage="{branches}% ({branches}/100)"' if number == 1 else "")
                + "/>"
                for number in range(1, 101)
            )
            return self.run_report(
                '<coverage><packages><package><classes><class filename="src/Podlord.Core/CoverageGate.cs">'
                f"<lines>{rows}</lines></class></classes></package></packages></coverage>",
            )
        return self.run_report()

    def run_report(self, document=None):
        if document is not None:
            (self.reports / "coverage.cobertura.xml").write_text(document, encoding="utf-8")
        return subprocess.run(
            [sys.executable, str(ROOT / "scripts/check-coverage.py"), str(ROOT), str(self.reports)],
            capture_output=True, text=True, check=False, timeout=10,
        )

    def test_exact_gates_pass(self):
        result = self.run_gate(95, 90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_below_line_gate_fails(self):
        result = self.run_gate(94, 100)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_below_branch_gate_fails(self):
        result = self.run_gate(100, 89)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_former_branch_gate_is_not_accepted(self):
        result = self.run_gate(100, 80)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_full_coverage_passes(self):
        result = self.run_gate(100, 100)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_reports_fail_explicitly(self):
        result = self.run_gate()
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("no Cobertura reports", result.stderr)

    def test_empty_report_fails_explicitly(self):
        self.assertEqual(self.run_report("<coverage/>").returncode, 2)

    def test_excluded_only_report_is_not_full_coverage(self):
        result = self.run_report('<coverage><class filename="src/Podlord.App/MainWindowViewModel.cs"><lines><line number="1" hits="1"/></lines></class></coverage>')
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_malformed_xml_fails_explicitly(self):
        result = self.run_report("<coverage")
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_malformed_branch_data_fails_explicitly(self):
        result = self.run_report('<coverage><class filename="src/Podlord.Core/Gate.cs"><lines><line number="1" hits="1" branch="true" condition-coverage="unknown"/></lines></class></coverage>')
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_impossible_branch_counts_fail_explicitly(self):
        result = self.run_report('<coverage><class filename="src/Podlord.Core/Gate.cs"><lines><line number="1" hits="1" branch="true" condition-coverage="150% (3/2)"/></lines></class></coverage>')
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_missing_line_number_fails_explicitly(self):
        result = self.run_report('<coverage><class filename="src/Podlord.Core/Gate.cs"><lines><line hits="1"/></lines></class></coverage>')
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_negative_hits_fail_explicitly(self):
        result = self.run_report('<coverage><class filename="src/Podlord.Core/Gate.cs"><lines><line number="1" hits="-1"/></lines></class></coverage>')
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_zero_line_number_fails_explicitly(self):
        result = self.run_report('<coverage><class filename="src/Podlord.Core/Gate.cs"><lines><line number="0" hits="1"/></lines></class></coverage>')
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
