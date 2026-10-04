"""Strict, version-independent protocol for publishing canonical C cases."""

from dataclasses import dataclass
import json
import re


@dataclass(frozen=True)
class Case:
    suite: str
    name: str


def parse_case_list(output: str) -> list[Case]:
    try:
        payload = json.loads(output)
    except (ValueError, TypeError) as error:
        raise ValueError("C harness did not publish valid JSON") from error
    if not isinstance(payload, list) or not payload:
        raise ValueError("C harness must publish at least one case")
    cases = []
    for entry in payload:
        if not isinstance(entry, dict) or set(entry) != {"suite", "name"}:
            raise ValueError("C case must contain exactly suite and name")
        if any(not isinstance(value, str) or not re.fullmatch(r"[a-z][a-z0-9_]*", value)
               for value in entry.values()):
            raise ValueError("C case identifiers must be safe literal names")
        case = Case(**entry)
        if case in cases:
            raise ValueError(f"duplicate C case: {case}")
        cases.append(case)
    return cases
