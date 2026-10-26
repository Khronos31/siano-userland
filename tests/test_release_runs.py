import json
from pathlib import Path
import sys
import tempfile
import unittest


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
        release_runs.verify_run(self.run_metadata(candidate_id), candidate_id, SHA)
        release_runs.verify_run(self.run_metadata(reproduction_id), reproduction_id, SHA)

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

    def test_cli_requires_exact_metadata_fields(self):
        metadata = self.run_metadata(12345)
        metadata["extra"] = True
        path = self.write_metadata("run.json", metadata)
        with self.assertRaises(release_runs.RunError):
            release_runs.parse_run(path)


if __name__ == "__main__":
    unittest.main()
