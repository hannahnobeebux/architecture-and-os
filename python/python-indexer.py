"""
Directory Indexer - Scheduler Prototype

Variant A: FCFS (baseline)
Variant B: Round Robin (pre-emptive)

What this script does:
- Walks a directory and creates "jobs" (each job = one file path to index)
- Indexes each job: metadata + basic permissions (POSIX-style)
- Writes results to JSON Lines (.jsonl)
"""

from __future__ import annotations

import os
import json
import stat
import mimetypes
from pathlib import Path
from collections import deque
from dataclasses import dataclass
import time
from typing import Deque, Dict, Any, List, Optional
import hashlib

SCHEDULER = "FCFS"  # "FCFS" (Variant A) or "RR" (Variant B)
TIME_QUANTUM = 1    # Used by Round Robin only

# -----------------------------
# Job model
# -----------------------------
@dataclass
class Job:
    path: Path
    arrival: int            # "arrival time" in ticks (just a counter in this simulation)
    est_cost: int           # estimated cost (used for SJF/MLFQ ideas)
    remaining: int = 1      # for RR/MLFQ: remaining "work units" (simple abstraction)
    queue_level: int = 0    # for MLQ/MLFQ: which queue the job is in


# -----------------------------
# Indexing work (CPU burst)
# -----------------------------



def hash_file(path: Path, algo: str = "sha256") -> str:
    h = hashlib.new(algo)
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def scan_one_path(path: Path, hash_algo: str) -> Dict[str, Any]:
    rec: Dict[str, Any] = {
        "filename": path.name,
        "path": str(path),
        "hash_algo": hash_algo,
    }

    try:
        st = path.stat()
        rec["size"] = st.st_size
        rec["mtime"] = st.st_mtime
        rec["owner"] = getattr(st, "st_uid", None)

        if path.is_file():
            rec["hash"] = hash_file(path, hash_algo)

    except Exception as e:
        rec["error"] = str(e)

    return rec

# -----------------------------
# Job creation
# -----------------------------
def build_jobs(root: Path, repeat: int = 1) -> List[Job]:
    jobs: List[Job] = []
    tick = 0

    for _ in range(repeat):
        for dirpath, _, filenames in os.walk(root):
            for fn in filenames:
                p = Path(dirpath) / fn
                tick += 1

            # Simple estimate: larger files => higher cost (bucketed)
            try:
                size = p.lstat().st_size
            except OSError:
                size = 0

            if size < 100_000:         # < 100 KB
                est = 1
            elif size < 10_000_000:    # < 10 MB
                est = 2
            else:
                est = 3

            jobs.append(Job(path=p, arrival=tick, est_cost=est, remaining=est))

    return jobs


# -----------------------------
# Scheduler logic 
# -----------------------------
def choose_next_job(ready: Deque[Job], tick: int) -> Optional[Job]:
    if not ready:
        return None
    
    # VARIANT A: First-Come-First-Served (FCFS)
    if SCHEDULER == "FCFS":
        return ready.popleft()
    
    # VARIANT B: Round Robin (RR)
    if SCHEDULER == "RR":
        job = ready.popleft()
        job.remaining -= TIME_QUANTUM

        if job.remaining > 0:
            # Not finished: re-queue
            ready.append(job)
        else:
            # Finished: return job
            return job
        
        return job  # Return the job that just ran (even if not finished)

    raise ValueError("Unknown scheduler")


def on_job_feedback(job: Job, record: Dict[str, Any]) -> None:
    """
    Feedback-driven scheduling adjustments (MLFQ-style)

    Rules:
    - Demote jobs that cause errors (e.g. permission issues)
    - Demote jobs that are large (likely CPU / I/O heavy)
    - Keep fast jobs at higher priority
    """

    # Scheduling feedback (priority adjustment only)
    if "error" in record:
        job.queue_level = min(job.queue_level + 1, 3)
    elif record.get("size_bytes", 0) > 10_000_000:
        job.queue_level = min(job.queue_level + 1, 3)
    else:
        job.queue_level = max(job.queue_level - 1, 0)

    # IMPORTANT: indexing a file completes the job
    return True


# -----------------------------
# Simulation loop (runs "scheduler")
# -----------------------------
# def run_indexer(root: Path, output_jsonl: Path, scheduler_name: str, run_id: int) -> Dict[str, Any]:
#     # Create jobs and load into a ready queue
#     jobs = build_jobs(root, repeat=10)

#     # In this simple model, all jobs are "ready" immediately.
#     # Students can extend this by using arrival times more realistically.
#     ready: Deque[Job] = deque(sorted(jobs, key=lambda j: j.arrival))

#     tick = 0
#     completed = 0
#     latencies: List[int] = []

#     start_time = time.perf_counter()

#     with output_jsonl.open("w", encoding="utf-8") as f:
#         while ready:
#             tick += 1

#             job = choose_next_job(ready, tick)
#             if job is None:
#                 continue

#             # "Run" the job: do one index operation
#             record = scan_one_path(job.path)

#             # Scheduling metadata
#             record["scheduler"] = scheduler_name
#             record["run_id"] = run_id
#             record["arrival"] = job.arrival
#             record["est_cost"] = job.est_cost
#             record["queue_level"] = job.queue_level
#             record["tick_ran"] = tick
#             record["remaining"] = job.remaining

#             # Feedback hook (MLFQ-style adaptations)
#             finished = on_job_feedback(job, record)

#             if finished:
#                 completed += 1
#                 latencies.append(max(0, tick - job.arrival))
#             else: 
#                 # Re-queue the job if not finished (for RR/MLFQ)
#                 ready.append(job)

#             # Write one record per line (easy to parse and analyse)
#             f.write(json.dumps(record) + "\n")
    
#     end_time = time.perf_counter()

#     return {
#         "scheduler": scheduler_name,
#         "run_id": run_id,
#         "jobs": completed,
#         "ticks": tick,
#         "wall_time_sec": round(end_time - start_time, 4),
#         "avg_latency_ticks": round(
#             sum(latencies) / len(latencies), 2
#         ) if latencies else 0,
#     }

# VARIANT A
from concurrent.futures import ThreadPoolExecutor, as_completed

def run_threadpool_indexer(
    root: Path,
    output_jsonl: Path,
    hash_algo: str,
    workers: int = 4,
) -> None:
    files = [p for p in root.rglob("*") if p.is_file()]

    with output_jsonl.open("w", encoding="utf-8") as f, \
         ThreadPoolExecutor(max_workers=workers) as pool:

        futures = {
            pool.submit(scan_one_path, p, hash_algo): p
            for p in files
        }

        for future in as_completed(futures):
            record = future.result()
            record["variant"] = "thread_pool"
            f.write(json.dumps(record) + "\n")

# VARIANT B
from concurrent.futures import ProcessPoolExecutor


def run_multiprocess_indexer(
    root: Path,
    output_jsonl: Path,
    hash_algo: str,
    workers: int = os.cpu_count(),
) -> None:
    files = [p for p in root.rglob("*") if p.is_file()]

    with output_jsonl.open("w", encoding="utf-8") as f, \
         ProcessPoolExecutor(max_workers=workers) as pool:

        for record in pool.map(
            lambda p: scan_one_path(p, hash_algo), files
        ):
            record["variant"] = "multiprocessing"
            f.write(json.dumps(record) + "\n")


import argparse


def query_find(index_file: Path, min_mb: int) -> None:
    threshold = min_mb * 1024 * 1024
    for line in index_file.open():
        rec = json.loads(line)
        if rec.get("size", 0) > threshold:
            print(rec["path"], rec["size"])


def query_checksum(index_file: Path, filename: str) -> None:
    for line in index_file.open():
        rec = json.loads(line)
        if rec.get("filename") == filename:
            print(rec.get("hash"))
            return
    print("File not found")

# if __name__ == "__main__":
#     root_folder = Path("/Users/nobeebuxh/architecture-and-os/test_data") 

#     results: List[Dict[str, Any]] = []

#     # ===== Variant A: FCFS =====
#     SCHEDULER = "FCFS"
#     for run in range(1, 4):  # 3 runs (assignment requirement)
#         out_file = Path(f"index_results_FCFS_run{run}.jsonl")
#         summary = run_indexer(
#             root=root_folder,
#             output_jsonl=out_file,
#             scheduler_name="FCFS",
#             run_id=run,
#         )
#         results.append(summary)

#     # ===== Variant B: Round Robin / MLFQ =====
#     SCHEDULER = "RR"
#     for run in range(1, 4):
#         out_file = Path(f"index_results_RR_run{run}.jsonl")
#         summary = run_indexer(
#             root=root_folder,
#             output_jsonl=out_file,
#             scheduler_name="RR",
#             run_id=run,
#         )
#         results.append(summary)

#     # Print summary table (easy copy into report)
#     print("\n=== Experiment Summary ===")
#     for r in results:
#         print(r)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["index", "find", "checksum"])
    parser.add_argument("--root", type=Path)
    parser.add_argument("--index", type=Path, default=Path("index.jsonl"))
    parser.add_argument("--variant", choices=["thread", "process"])
    parser.add_argument("--hash", default="sha256")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--gt", type=int)
    parser.add_argument("--file")

    args = parser.parse_args()

    if args.mode == "index":
        if args.variant == "thread":
            run_threadpool_indexer(
                args.root, args.index, args.hash, args.workers
            )
        else:
            run_multiprocess_indexer(
                args.root, args.index, args.hash, args.workers
            )

    elif args.mode == "find":
        query_find(args.index, args.gt)

    elif args.mode == "checksum":
        query_checksum(args.index, args.file)