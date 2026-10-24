"""Reproducible local index/search benchmark; touches temporary data only."""
import json
import statistics
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from agentdock.core import Store


def run(count=1000):
    with tempfile.TemporaryDirectory(prefix="agentdock-bench-") as temp:
        root = Path(temp)
        external = root / "skills"
        for i in range(count):
            folder = external / ("skill-" + str(i))
            folder.mkdir(parents=True)
            (folder / "SKILL.md").write_text(f'---\nname: skill-{i}\ndescription: "代码审查与数据分析工作流程 {i}"\n---\n\n# Skill {i}\n\n1. Read the input.\n2. Verify the result.\n', encoding="utf-8")
        start = time.perf_counter()
        store = Store(root / "data")
        startup = (time.perf_counter() - start) * 1000
        cold = store.scan([("Benchmark", external)])
        warm = [store.scan([("Benchmark", external)])["ms"] for _ in range(3)]
        queries = []
        for _ in range(20):
            start = time.perf_counter()
            store.list_skills(q="数据分析", limit=24)
            queries.append((time.perf_counter() - start) * 1000)
        return {"skills": count, "store_startup_ms": round(startup, 2), "cold_scan_ms": cold["ms"],
            "warm_scan_median_ms": statistics.median(warm), "fts_search_median_ms": round(statistics.median(queries), 2),
            "data_bytes": store.stats()["data_bytes"], "note": "Local synthetic workload; OS file cache is warm after first scan. Edge UI memory not included."}


if __name__ == "__main__":
    print(json.dumps(run(int(sys.argv[1]) if len(sys.argv) > 1 else 1000), indent=2))
