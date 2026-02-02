"""
Python File Indexer

Variant A: Thread-pool
Variant B: True Multicore using multiprocessing

"""

from __future__ import annotations

import json
from pathlib import Path
from collections import deque
from dataclasses import dataclass
from typing import Deque, Dict, Any, List, Optional
import hashlib
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed #for variant A
from concurrent.futures import ProcessPoolExecutor #for variant B 

def hash_file(path: Path, algo: str = "sha256") -> str:
    h = hashlib.new(algo)
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()

# scanning files for multiprocessing method
def scan_one_path_mp(args):
    path, hash_algo= args
    record = scan_one_path(path, hash_algo)
    return record

# scanning files for thread-pool method
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

# VARIANT A — Thread Pool
def run_threadpool_indexer(
    root: Path,
    output_jsonl: Path,
    hash_algo: str,
    workers: int
) -> None:
    files = [p for p in root.rglob("*") if p.is_file()]

    with output_jsonl.open("w", encoding="utf-8") as f, \
         ThreadPoolExecutor(max_workers=workers) as pool:

        futures = {
            pool.submit(
                scan_one_path,
                p,
                hash_algo
            ): p
            for p in files
        }

        for future in as_completed(futures):
            record = future.result()
            record["variant"] = "thread_pool"
            f.write(json.dumps(record) + "\n")

# VARIANT B — Multiprocessing
def run_multiprocess_indexer(
    root: Path,
    output: Path,
    hash_algo: str,
    workers: int
) -> None:
    files = [p for p in root.rglob("*") if p.is_file()]

    with output.open("w", encoding="utf-8") as f, \
         ProcessPoolExecutor(max_workers=workers) as pool:

        for record in pool.map(
            scan_one_path_mp,
            [(p, hash_algo) for p in files]
        ):
            record["variant"] = "multiprocessing"
            f.write(json.dumps(record) + "\n")


# for the CLI query: reads a previously generated index file and lists all files larger than a given size
def query_find(index_file: Path, min_mb: int) -> None:
    threshold = min_mb * 1024 * 1024
    for line in index_file.open():
        rec = json.loads(line)
        if rec.get("size", 0) > threshold:
            print(rec["path"], rec["size"])

# for the CLI query: searches a previously generated index file and prints the hash of the specified filename if it exists
def query_checksum(index_file: Path, filename: str) -> None:
    for line in index_file.open():
        rec = json.loads(line)
        if rec.get("filename") == filename:
            print(rec.get("hash"))
            return
    print("File not found")

# main method with CLI implementation
if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--index", type=Path, help="Path to index file (used by find and checksum queries)")
    parser.add_argument("mode", choices=["index", "find", "checksum"])
    parser.add_argument("--root", type=Path)
    parser.add_argument("--variant", choices=["thread", "process"])
    parser.add_argument("--hash", default="sha256")
    parser.add_argument("--workers", type=int, default=4)
    # greater-than size in MB for 'find' mode
    parser.add_argument("--gt", type=int)
    parser.add_argument("--file")

    args = parser.parse_args()

    if args.mode == "index":

        base_output = Path("file-indexer-output")

        if args.variant == "thread":
            SCHEDULER = "FCFS"
            output_file = Path("file-indexer-output-thread.jsonl")
            run_threadpool_indexer(
                args.root, output_file, args.hash, args.workers, SCHEDULER
            )
        elif args.variant == "process":
            SCHEDULER = "RR"
            output_file = Path("file-indexer-output-multiprocess.jsonl")
            run_multiprocess_indexer(
                args.root, output_file, args.hash, args.workers, SCHEDULER
            )

    elif args.mode == "find":
        query_find(args.index, args.gt)

    elif args.mode == "checksum":
        query_checksum(args.index, args.file)

