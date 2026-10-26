from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import release_runs


SHA = "a" * 40


class ReleaseRunTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.document = self.root / "1.0.0.md"
        self.document.write_text(
            "candidate_run_id: 12345\nreproduction_run_id: 67890\n", encoding="utf-8")

    def tearDown(self):
        self.temporary.cleanup()

    def run_metadata(self, run_id):
        return {"databaseId": run_id, "workflowName": "Release candidate",
                "event": "workflow_dispatch", "status": "completed",
                "conclusion": "success", "headSha": SHA}

    def write_metadata(self, name, metadata):
        path = self.root / name
        path.write_text(json.dumps(metadata), encoding="utf-8")
        return path

    def test_two_recorded_distinct_successful_runs_pass(self):
        candidate_id, reproduction_id = release_runs.read_run_ids(self.document)
        self.assertEqual((candidate_id, reproduction_id), (12345, 67890))
        candidate_sha = release_runs.verify_pair(
            self.run_metadata(candidate_id), self.run_metadata(reproduction_id),
            candidate_id, reproduction_id)
        self.assertEqual(candidate_sha, SHA)

    def test_unset_missing_duplicate_or_invalid_ids_fail(self):
        for content in (
                "candidate_run_id: 未実施\nreproduction_run_id: 67890\n",
                "candidate_run_id: 12345\n",
                "candidate_run_id: 12345\ncandidate_run_id: 22345\nreproduction_run_id: 67890\n",
                "candidate_run_id: 012345\nreproduction_run_id: 67890\n",
                "candidate_run_id: 12345\nreproduction_run_id: 12345\n"):
            self.document.write_text(content, encoding="utf-8")
            with self.subTest(content=content), self.assertRaises(release_runs.RunError):
                release_runs.read_run_ids(self.document)

    def test_failed_workflow_or_wrong_sha_fails(self):
        metadata = self.run_metadata(12345)
        metadata["conclusion"] = "failure"
        with self.assertRaises(release_runs.RunError):
            release_runs.verify_run(metadata, 12345, SHA)
        metadata = self.run_metadata(12345)
        metadata["headSha"] = "b" * 40
        with self.assertRaises(release_runs.RunError):
            release_runs.verify_run(metadata, 12345, SHA)

    def test_wrong_run_id_event_and_workflow_fail(self):
        for key, value in (("databaseId", 999), ("event", "push"),
                           ("workflowName", "other"), ("status", "in_progress")):
            metadata = self.run_metadata(12345)
            metadata[key] = value
            with self.subTest(key=key), self.assertRaises(release_runs.RunError):
                release_runs.verify_run(metadata, 12345, SHA)

    def test_independent_runs_must_name_same_candidate_sha(self):
        reproduction = self.run_metadata(67890)
        reproduction["headSha"] = "b" * 40
        with self.assertRaises(release_runs.RunError):
            release_runs.verify_pair(self.run_metadata(12345), reproduction, 12345, 67890)

    def test_pair_returns_sha_from_verified_run_metadata(self):
        self.assertEqual(release_runs.verify_pair(
            self.run_metadata(12345), self.run_metadata(67890), 12345, 67890), SHA)

    def test_verify_cli_outputs_recorded_ids_and_derived_candidate_sha(self):
        candidate = self.write_metadata("candidate.json", self.run_metadata(12345))
        reproduction = self.write_metadata("reproduction.json", self.run_metadata(67890))
        output = io.StringIO()
        with patch("sys.argv", ["release_runs.py", "verify", "--document", str(self.document),
                                "--candidate-run", str(candidate),
                                "--reproduction-run", str(reproduction)]), \
                redirect_stdout(output):
            self.assertEqual(release_runs.main(), 0)
        self.assertEqual(output.getvalue(), f"12345 67890 {SHA}\n")

    def test_cli_requires_exact_metadata_fields(self):
        metadata = self.run_metadata(12345)
        metadata["extra"] = True
        path = self.write_metadata("run.json", metadata)
        with self.assertRaises(release_runs.RunError):
            release_runs.parse_run(path)


if __name__ == "__main__":
    unittest.main()
