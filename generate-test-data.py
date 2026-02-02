"""
Generate test data for file indexing.

Creates a small, heterogeneous directory tree containing:
- Small text files
- Medium binary files
- One large binary file
- One restricted file (permission denied where supported)

"""

from pathlib import Path
import os
import random

BASE_DIR = Path("test_data")

def write_file(path: Path, size_kb: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(os.urandom(size_kb * 1024))


def main() -> None:
    #small files (1–4 KB)
    write_file(BASE_DIR / "small" / "doc1.txt", 1)
    write_file(BASE_DIR / "small" / "doc2.txt", 2)
    write_file(BASE_DIR / "small" / "notes.md", 1)

    #medium files (100–500 KB)
    write_file(BASE_DIR / "medium" / "image.jpg", 200)
    write_file(BASE_DIR / "medium" / "report.pdf", 500)

    #large file (~10 MB)
    write_file(BASE_DIR / "large" / "bigfile.bin", 10_240)

    #restricted file (may not work on Windows)
    restricted = BASE_DIR / "restricted" / "no_access.txt"
    write_file(restricted, 1)
    try:
        restricted.chmod(0o000)
    except PermissionError:
        pass

    print(f"Test data created under: {BASE_DIR.resolve()}")


if __name__ == "__main__":
    main()