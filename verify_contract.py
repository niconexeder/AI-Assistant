import json
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.abspath(__file__))
RUN = os.path.join(ROOT, "build", "run.exe")
CATALOG_PATH = os.path.join(ROOT, "data", "catalog.json")
CATALOG = json.load(open(CATALOG_PATH, encoding="utf-8"))
BY_ID = {item["id"]: item for item in CATALOG}


def call(messages, *extra):
    request = {"messages": messages}
    process = subprocess.run(
        [RUN, "--catalog", CATALOG_PATH, "--no-llm", *extra],
        input=json.dumps(request, ensure_ascii=False).encode("utf-8"),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert process.returncode == 0, (process.returncode, process.stderr.decode("utf-8"))
    return json.loads(process.stdout.decode("utf-8"))


def assert_ok(response, budget=None, city="taganrog", cats=("table", "chair", "lamp"),
              width=None):
    assert response["status"] == "ok", response
    assert 1 <= len(response["options"]) <= 3
    shapes = set()
    for option in response["options"]:
        ids = option["listing_ids"]
        assert len(ids) == len(set(ids)), ids
        shapes.add(tuple(sorted(ids)))
        items = [BY_ID[i] for i in ids]
        assert sorted(x["category"] for x in items) == sorted(cats), items
        assert all(x["city_id"] == city for x in items), items
        if width is not None and "table" in cats:
            table = next(x for x in items if x["category"] == "table")
            assert table["attributes"]["width_cm"] <= width, table
        assert option["total_rub"] == sum(x["price_rub"] for x in items)
        if budget is not None:
            assert option["total_rub"] <= budget
        assert option["explanation"].strip()
    assert len(shapes) == len(response["options"]), shapes
    return response


pub = json.load(open(os.path.join(ROOT, "examples", "01-selection", "request.json"),
                     encoding="utf-8"))
example = assert_ok(call(pub["messages"]), budget=15000, width=100)
print("example 01:", [o["total_rub"] for o in example["options"]])

clarify_request = json.load(open(os.path.join(ROOT, "examples", "02-clarification",
                                             "request.json"), encoding="utf-8"))
clarify = call(clarify_request["messages"])
assert clarify["status"] == "clarify", clarify
assert [q["field"] for q in clarify["questions"]] == ["budget_rub"], clarify
print("example 02:", [q["field"] for q in clarify["questions"]])

continued = call([
    {"role": "user", "text": "Стол не шире метра, кресло и лампа. Таганрог."},
    {"role": "assistant", "text": "Какой у вас бюджет на весь комплект?"},
    {"role": "user", "text": "15 тысяч"},
])
assert_ok(continued, budget=15000, width=100)
print("clarify continuation:", [o["total_rub"] for o in continued["options"]])

excluded = call([{"role": "user", "text":
                  "Стол не нужен, нужна лампа. Таганрог. До 15000 рублей"}])
assert_ok(excluded, budget=15000, cats=("lamp",))
print("exclusion:", [o["listing_ids"] for o in excluded["options"]])

bad_city = call([{"role": "user", "text": "Нужен стол в Москве до 15000 рублей"}])
assert bad_city["status"] == "clarify", bad_city
assert "city_id" in [q["field"] for q in bad_city["questions"]], bad_city
print("unknown city:", [q["field"] for q in bad_city["questions"]])

no_match = call([{"role": "user", "text": "Кресло. Азов. До 100 рублей"}])
assert no_match["status"] == "no_match", no_match
assert no_match["message"].strip()
print("no_match:", no_match["message"])

all_negated = call([{"role": "user", "text":
                     "Стол, кресло и лампа не нужны. Таганрог. До 15000 рублей"}])
assert all_negated["status"] == "clarify", all_negated
assert "categories" in [q["field"] for q in all_negated["questions"]], all_negated
print("all categories negated:", [q["field"] for q in all_negated["questions"]])

print("ALL CONTRACT CHECKS PASSED")
