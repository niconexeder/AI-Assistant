import json
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = os.path.dirname(os.path.abspath(__file__))
RUN = os.path.join(ROOT, "build", "run.exe")
CATALOG = os.path.join(ROOT, "data", "catalog.json")


def call(label, messages):
    process = subprocess.run(
        [RUN, "--catalog", CATALOG, "--no-llm"],
        input=json.dumps({"messages": messages}, ensure_ascii=False).encode("utf-8"),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    data = json.loads(process.stdout.decode("utf-8"))
    print("==", label, "exit", process.returncode, "status", data["status"])
    if data["status"] == "ok":
        print([o["listing_ids"] for o in data["options"]])
    elif data["status"] == "clarify":
        print([q["field"] for q in data["questions"]])
    else:
        print(data["message"])
    return data


call("replacement across turns", [
    {"role": "user", "text": "Нужен стол. Таганрог. До 15000 рублей"},
    {"role": "assistant", "text": "Вот варианты."},
    {"role": "user", "text": "Вместо стола кресло"},
])

call("three-turn city then budget", [
    {"role": "user", "text": "Нужны стол и кресло"},
    {"role": "assistant", "text": "В каком городе искать?"},
    {"role": "user", "text": "Таганрог"},
    {"role": "assistant", "text": "Какой у вас бюджет на весь комплект?"},
    {"role": "user", "text": "15 тысяч"},
])

call("unsupported plus supported", [{"role": "user", "text":
    "Нужен стол и диван. Таганрог. До 15000 рублей"}])

call("city correction", [
    {"role": "user", "text": "Нужен стол в Таганроге до 15000"},
    {"role": "assistant", "text": "Понял."},
    {"role": "user", "text": "Не в Таганроге, а в Азове"},
])

call("add category", [
    {"role": "user", "text": "Нужен стол и кресло. Таганрог. До 15000"},
    {"role": "assistant", "text": "Вот варианты."},
    {"role": "user", "text": "Ещё лампу"},
])

call("attribute reset", [
    {"role": "user", "text": "Стол не шире 80 см. Таганрог. До 15000"},
    {"role": "assistant", "text": "Понял."},
    {"role": "user", "text": "Ширина стола не важна"},
])

call("chair true constraint", [{"role": "user", "text":
    "Кресло с регулировкой высоты. Таганрог. До 5000"}])

call("chair false constraint", [{"role": "user", "text":
    "Кресло без регулировки высоты. Таганрог. До 5000"}])
