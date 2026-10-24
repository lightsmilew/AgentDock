import json
import os
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path
from unittest.mock import patch

from agentdock.adapters import Adapters
from agentdock.core import Store, metadata, tree_hash
from agentdock.models import Models, protect, validate_endpoint
from agentdock.service import DesktopService
from agentdock.instance import NativeInstance


CONTENT = '---\nname: test-skill\ndescription: "测试中文搜索与代码审查的可复用工作流程"\n---\n\n# Test Skill\n\n1. Read the supplied input.\n2. Verify and report the result.\n'


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.env_patch = patch.dict(os.environ, {"CODEX_HOME": str(self.root / "home/.codex"), "CLAUDE_CONFIG_DIR": str(self.root / "home/.claude"), "DSH_HOME": str(self.root / "home/.dsh")})
        self.env_patch.start()
        self.store = Store(self.root / "data")
        self.models = Models(self.store)
        self.adapters = Adapters(self.store, self.models, home=self.root / "home")

    def tearDown(self):
        self.env_patch.stop()
        self.tmp.cleanup()

    def skill(self):
        return self.store.save_skill({"content": CONTENT})

    def provider(self, protocol="responses"):
        return self.models.save({"name": "Test", "base_url": "https://example.com/v1", "protocol": protocol, "model": "test-model", "key_env": "TEST_API_KEY"})


class SkillsTests(Fixture):
    def test_seed_search_and_content_dedup(self):
        self.assertEqual(self.store.list_skills()["total"], 4)
        self.skill()
        self.skill()
        self.assertEqual(self.store.list_skills(q="中文搜索")["total"], 2)
        self.assertEqual(self.store.list_skills(q="中文")["total"], 2)
        self.assertEqual(self.store.list_skills(q='" OR x')["total"], 0)
        self.assertGreater(self.store.stats()["saved_bytes"], 0)

    def test_metadata_validation_and_block_scalar(self):
        for text in ["# no frontmatter", CONTENT.replace("test-skill", "../escape"), CONTENT.replace('"测试中文搜索与代码审查的可复用工作流程"', "''")]:
            with self.assertRaises(ValueError):
                metadata(text)
        text = CONTENT.replace('"测试中文搜索与代码审查的可复用工作流程"', ">\n  A reusable workflow\n  for analysis.")
        self.assertEqual(metadata(text)["description"], "A reusable workflow for analysis.")

    def test_revisions_preserved_and_fts_updated(self):
        s = self.skill()
        newer = CONTENT.replace("代码审查", "报表分析")
        self.store.save_skill({"id": s["id"], "content": newer})
        self.assertEqual(len(self.store.skill(s["id"])["revisions"]), 2)
        self.assertEqual(self.store.list_skills(q="报表分析")["total"], 1)

    def test_incremental_scan_and_external_readonly(self):
        external = self.root / "external" / "sample"
        external.mkdir(parents=True)
        (external / "SKILL.md").write_text(CONTENT, encoding="utf-8")
        r = self.store.scan([("Test Agent", external.parent)])
        self.assertEqual(r["changed"], 1)
        self.assertEqual(self.store.scan([("Test Agent", external.parent)])["changed"], 0)
        s = self.store.list_skills(scope="external")["items"][0]
        with self.assertRaises(ValueError):
            self.store.save_skill({"id": s["id"], "content": CONTENT})
        self.assertEqual((external / "SKILL.md").read_text(encoding="utf-8"), CONTENT)

    def test_import_resources_deploy_toggle_and_conflict(self):
        external = self.root / "external"
        (external / "references").mkdir(parents=True)
        (external / "SKILL.md").write_text(CONTENT, encoding="utf-8")
        (external / "references/spec.txt").write_text("Keep this resource")
        s = self.store.import_skill(str(external))
        target = self.root / "target"
        deployed = Path(self.store.deploy(s["id"], "test", target)["path"])
        self.assertEqual((deployed / "references/spec.txt").read_text(), "Keep this resource")
        self.store.toggle(s["id"], "enabled", False)
        self.assertFalse(deployed.exists())
        self.store.toggle(s["id"], "enabled", True)
        self.assertTrue(deployed.exists())
        (deployed / "SKILL.md").write_text("modified", encoding="utf-8")
        with self.assertRaises(ValueError):
            self.store.deploy(s["id"], "test", target)
        with self.assertRaises(ValueError):
            self.store.toggle(s["id"], "enabled", False)
        self.assertEqual((deployed / "SKILL.md").read_text(), "modified")

    def test_never_overwrite_unmanaged_target(self):
        s = self.skill()
        target = self.root / "target" / s["name"]
        target.mkdir(parents=True)
        (target / "SKILL.md").write_text("original")
        with self.assertRaises(ValueError):
            self.store.deploy(s["id"], "test", target.parent)
        self.assertEqual((target / "SKILL.md").read_text(), "original")


class AdapterTests(Fixture):
    def test_all_requested_adapters_and_harness_protocols(self):
        names = {a["id"] for a in self.adapters.list()}
        self.assertTrue({"codex", "claude", "harness", "cursor", "workbuddy"} <= names)
        p = self.provider("chat")
        preview = self.adapters.preview("harness", p["id"])
        self.assertIn("api: openai-completions", preview["content"])
        self.assertIn("apiKeyEnv: TEST_API_KEY", preview["content"])
        self.assertFalse(preview["can_apply"])
        with self.assertRaises(ValueError):
            self.adapters.preview("codex", p["id"])

    def test_codex_preserves_toml_and_restores_exact_original(self):
        p = self.provider()
        path = self.adapters.config_paths(self.adapters.item("codex"))[0]
        original = b'# comment\nmodel = "old"\nmodel_provider = "old-provider"\n\n[features]\nfoo = true\n'
        path.parent.mkdir(parents=True)
        path.write_bytes(original)
        preview = self.adapters.preview("codex", p["id"])
        applied = self.adapters.apply(preview["ticket"])
        import tomllib
        result = tomllib.loads(path.read_text())
        self.assertEqual(result["features"]["foo"], True)
        self.assertEqual(result["model"], "test-model")
        self.adapters.restore(applied["backup"])
        self.assertEqual(path.read_bytes(), original)

    def test_config_optimistic_lock_and_restore_new_file(self):
        p = self.provider()
        preview = self.adapters.preview("codex", p["id"])
        path = Path(preview["path"])
        applied = self.adapters.apply(preview["ticket"])
        self.adapters.restore(applied["backup"])
        self.assertFalse(path.exists())
        preview = self.adapters.preview("codex", p["id"])
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('model = "changed-elsewhere"')
        with self.assertRaises(ValueError):
            self.adapters.apply(preview["ticket"])

    def test_claude_existing_secrets_never_sent_in_preview(self):
        p = self.provider("anthropic")
        path = self.adapters.config_paths(self.adapters.item("claude"))[0]
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps({"env": {"ANTHROPIC_API_KEY": "private-existing-secret"}, "permissions": {"allow": ["Read"]}}))
        preview = self.adapters.preview("claude", p["id"])
        self.assertNotIn("private-existing-secret", json.dumps(preview))
        self.adapters.apply(preview["ticket"])
        self.assertEqual(json.loads(path.read_text())["permissions"]["allow"], ["Read"])


class ModelTests(Fixture):
    def test_protocol_request_and_response_shapes(self):
        for protocol, response, endpoint in [
            ("chat", {"choices": [{"message": {"content": "ok"}}]}, "chat/completions"),
            ("responses", {"output": [{"content": [{"type": "output_text", "text": "ok"}]}]}, "responses"),
            ("anthropic", {"content": [{"type": "text", "text": "ok"}]}, "messages"),
        ]:
            p = self.provider(protocol)
            with patch.object(self.models, "request", return_value=response) as call:
                self.assertEqual(self.models.complete(p["id"], "system", "prompt")["text"], "ok")
                self.assertEqual(call.call_args.args[2], endpoint)
                self.assertEqual(call.call_args.args[3]["model"], "test-model")

    def test_generated_skill_not_automatically_saved_or_executed(self):
        before = self.store.list_skills()["total"]
        with patch.object(self.models, "complete", return_value={"text": '```markdown\n' + CONTENT + '\n```', "usage": {"output_tokens": 100}}):
            result = self.models.generate({"provider_id": "test", "prompt": "Generate a code review skill"})
        self.assertTrue(result["valid"])
        self.assertEqual(before, self.store.list_skills()["total"])

    def test_no_unencrypted_remote_http_or_embedded_credentials(self):
        for url in ["http://example.com/v1", "https://key@example.com", "file:///test", "https://example.com?key=secret"]:
            with self.assertRaises(ValueError):
                validate_endpoint(url)
        self.assertEqual(validate_endpoint("http://127.0.0.1:11434/v1/"), "http://127.0.0.1:11434/v1")

    @unittest.skipUnless(os.name == "nt", "DPAPI requires Windows")
    def test_dpapi_roundtrip_and_api_never_returns_key(self):
        self.assertEqual(protect(protect("private-test-secret"), decrypt=True), "private-test-secret")
        p = self.models.save({"name": "encrypted", "base_url": "https://example.com/v1", "api_key": "private-test-secret"})
        self.assertNotIn("private-test-secret", json.dumps(self.models.list()))
        self.assertNotIn("private-test-secret", self.store.path.read_bytes().decode("latin1"))
        self.assertEqual(self.models.get(p["id"])[1], "private-test-secret")
        self.models.save({**p, "base_url": "https://different.example/v1"})
        self.assertEqual(self.models.get(p["id"])[1], "")


class DesktopTests(Fixture):
    def test_direct_service_snapshot_and_export(self):
        service = DesktopService(self.root / "desktop", home=self.root / "home")
        self.assertEqual(len(service.snapshot()["agents"]), 19)
        self.assertEqual(len(service.snapshot()["presets"]), 31)
        s = service.store.list_skills()["items"][0]
        data, filename = service.export_bytes(s["id"])
        self.assertTrue(data.startswith(b"PK"))
        self.assertTrue(filename.endswith(".zip"))
        output = self.root / filename
        service.export(s["id"], output)
        self.assertEqual(output.read_bytes(), data)

    def test_project_directory_and_serial_model_requests(self):
        service = DesktopService(self.root / "desktop", home=self.root / "home")
        service.project(str(self.root))
        self.assertEqual(service.snapshot()["project"], str(self.root.resolve()))
        with self.assertRaises(ValueError):
            service.project(str(self.root / "not-a-directory"))
        service.model_lock.acquire()
        try:
            with self.assertRaises(ValueError):
                service.model_request("test", "any")
        finally:
            service.model_lock.release()

    @unittest.skipUnless(os.name == "nt", "Windows native instance event")
    def test_native_instance_reuses_window_without_network(self):
        first = NativeInstance(self.root)
        second = NativeInstance(self.root)
        try:
            self.assertTrue(first.primary)
            self.assertFalse(second.primary)
            self.assertTrue(first.requested_show())
            self.assertFalse(first.requested_show())
        finally:
            second.close()
            first.close()

    def test_real_local_http_roundtrip_for_three_llm_protocols(self):
        from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
        requests = []

        class FakeModel(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_GET(self):
                self.reply({"data": [{"id": "local-test-model"}]})

            def do_POST(self):
                data = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                requests.append((self.path, data))
                if self.path.endswith("/responses"):
                    self.reply({"output": [{"content": [{"type": "output_text", "text": CONTENT}]}]})
                elif self.path.endswith("/messages"):
                    self.reply({"content": [{"type": "text", "text": CONTENT}]})
                else:
                    self.reply({"choices": [{"message": {"content": CONTENT}}]})

            def reply(self, obj):
                body = json.dumps(obj).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        model_server = ThreadingHTTPServer(("127.0.0.1", 0), FakeModel)
        model_thread = threading.Thread(target=model_server.serve_forever, daemon=True)
        model_thread.start()
        try:
            for protocol in ("chat", "responses", "anthropic"):
                p = self.models.save({"name": protocol, "base_url": f"http://127.0.0.1:{model_server.server_port}/v1", "protocol": protocol, "model": "local-test-model"})
                self.assertEqual(self.models.discover(p["id"])["models"], ["local-test-model"])
                result = self.models.generate({"provider_id": p["id"], "prompt": "Generate a code review skill"})
                self.assertTrue(result["valid"])
            self.assertEqual([r[0] for r in requests], ["/v1/chat/completions", "/v1/responses", "/v1/messages"])
        finally:
            model_server.shutdown()
            model_server.server_close()
            model_thread.join()


if __name__ == "__main__":
    unittest.main()
