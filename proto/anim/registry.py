"""All animated type modules. heirs.py (built separately) is included only if the file exists."""
from __future__ import annotations

import importlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODULES = ["talkers", "ghosts", "mobs", "elites", "bosses", "billboards"]
OPTIONAL = ["heirs"]


def load():
    """returns (modules, errors): errors maps an optional module name to its import error"""
    mods, errors = [], {}
    for name in MODULES:
        mods.append(importlib.import_module(name))
    for name in OPTIONAL:
        if (HERE / f"{name}.py").exists():
            try:
                mods.append(importlib.import_module(name))
            except Exception as e:  # noqa: BLE001 (reported by the harness as a failure)
                errors[name] = f"{type(e).__name__}: {e}"
    return mods, errors
