import json
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = os.path.dirname(os.path.abspath(__file__))
RUN = os.path.join(ROOT, "build", "run.exe")
CATALOG = os.path.join(ROOT, "data", "catalog.json")

CASES = {
    "replacement": [
        {"role": "user", "text": "Нужен стол. Таганрог. До 15000 рублей"},
        {"role": "assistant", "text": "Вот варианты."},
        {"role": "user", "text": "Вместо стола кресло"},
    ],
    "city-correction": [
        {"role": "user", "text": "Нужен стол в Таганроге до 15000"},
        {"role": "assistant", "text": "Понял."},
        {"role": "user", "text": "Не в Таганроге, а в Азове"},
    ],
    "chair-false": [
        {"role": "user", "text": "Кресло без регулировки высоты. Таганрог. До 5000"},
    ],
    "chair-fixed": [
        {"role": "user", "text": "Кресло с фиксированной высотой. Таганрог. До 5000"},
    ],
}


def dump(label, messages):
    process = subprocess.run(
        [RUN, "--catalog", CATALOG, "--no-llm", "--dump-requirements"],
        input=json.dumps({"messages": messages}, ensure_ascii=False).encode("utf-8"),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    print("==", label)
    for line in process.stderr.decode("utf-8").splitlines():
        if "requirements:" in line:
            print(line)
    print("status:", json.loads(process.stdout.decode("utf-8"))["status"])


for name, messages in CASES.items():
    dump(name, messages)
