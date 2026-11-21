import json
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = os.path.dirname(os.path.abspath(__file__))
RUN = os.path.join(ROOT, "build", "run.exe")
CATALOG = os.path.join(ROOT, "data", "catalog.json")


def call(messages):
    process = subprocess.run(
        [RUN, "--catalog", CATALOG, "--no-llm"],
        input=json.dumps({"messages": messages}, ensure_ascii=False).encode("utf-8"),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    print("exit", process.returncode)
    print(process.stdout.decode("utf-8"))
    return json.loads(process.stdout.decode("utf-8"))


print("== zero budget explicit ==")
call([{"role": "user", "text": "Нужен стол. Таганрог. Бюджет 0 рублей"}])

print("== zero budget answer ==")
call([
    {"role": "user", "text": "Нужен стол. Таганрог."},
    {"role": "assistant", "text": "Какой у вас бюджет на весь комплект?"},
    {"role": "user", "text": "0"},
])

print("== unsupported category ==")
call([{"role": "user", "text": "Нужен диван. Таганрог. До 15000 рублей"}])

print("== nothing given ==")
call([{"role": "user", "text": "Помогите подобрать мебель"}])
