import argparse
import random
import sys


def args():
    p = argparse.ArgumentParser()
    p.add_argument("--bytes", type=int, required=True)
    p.add_argument("--seed", type=int, default=1)
    return p.parse_args()


def emit(chunks, limit: int) -> None:
    out = sys.stdout.buffer
    total = 0
    for chunk in chunks:
        out.write(chunk)
        total += len(chunk)
        if total >= limit:
            break
    out.flush()


def rng(seed: int) -> random.Random:
    return random.Random(seed)
