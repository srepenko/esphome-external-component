#!/usr/bin/env python3
"""Графік погодинних відключень з публічного каналу t.me/SvitloOleksandriyskohoRaionu (веб-версія t.me/s/...).

Друкує JSON для command_line-сенсора HA:
  {"state": "14.11 15:01", "today": "14.11.2026", "tomorrow": "15.11.2026" | "",
   "t": {"1.1": "18-20", ...}, "n": {...}, "updated": "15:01"}
Для кожної дати береться найсвіжіший пост («станом на ...»). З'єднання лише по IPv4:
у мережі немає маршруту IPv6, а t.me віддає спершу IPv6-адресу — тоді запит висить до тайм-ауту.
"""
import datetime
import html
import json
import re
import socket
import urllib.request

URL = "https://t.me/s/SvitloOleksandriyskohoRaionu"
QUEUES = [f"{a}.{b}" for a in range(1, 7) for b in (1, 2)]

_orig = socket.getaddrinfo


def _ipv4_only(host, port, family=0, type=0, proto=0, flags=0):
    return _orig(host, port, socket.AF_INET, type, proto, flags)


socket.getaddrinfo = _ipv4_only


def main():
    req = urllib.request.Request(URL, headers={"User-Agent": "Mozilla/5.0 (HomeAssistant outage schedule)"})
    page = urllib.request.urlopen(req, timeout=25).read().decode("utf-8", "replace")
    posts = {}
    for part in page.split("tgme_widget_message_text")[1:]:
        body = part.split("</div>")[0]
        text = html.unescape(re.sub(r"<[^>]+>", "", re.sub(r"<br\s*/?>", "\n", body)))
        if "Черга 1.1:" not in text:
            continue
        m = re.search(r"[Нн]а (\d\d\.\d\d\.\d{4})", text) or re.search(r"(\d\d\.\d\d\.\d{4})", text)
        if not m:
            continue
        date = m.group(1)
        upd = re.search(r"станом на (\d{1,2}:\d\d)", text)
        q = {}
        for name in QUEUES:
            mm = re.search(r"Черга " + re.escape(name) + r":\s*([0-9:\-, ]*)", text)
            q[name] = re.sub(r"\s+", "", mm.group(1)).strip(",") if mm else ""
        # пости йдуть від старих до нових — пізніший пост на ту саму дату замінює попередній
        posts[date] = {"q": q, "upd": upd.group(1) if upd else ""}
    now = datetime.datetime.now()
    today = now.strftime("%d.%m.%Y")
    tomorrow = (now + datetime.timedelta(days=1)).strftime("%d.%m.%Y")
    t = posts.get(today, {}).get("q", {})
    n = posts.get(tomorrow, {}).get("q", {})
    upd = posts.get(tomorrow if n else today, {}).get("upd", "")
    out = {"state": (today[:5] + " " + upd).strip() if t else "немає графіка",
           "today": today if t else "", "tomorrow": tomorrow if n else "", "updated": upd, "t": t, "n": n}
    print(json.dumps(out, ensure_ascii=False))


if __name__ == "__main__":
    try:
        main()
    except Exception as e:  # HA покаже стан «помилка», атрибути лишаться порожніми
        print(json.dumps({"state": "помилка: " + type(e).__name__, "t": {}, "n": {}}, ensure_ascii=False))
