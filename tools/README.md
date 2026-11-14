# tools

## outage_schedule.py

Графік погодинних відключень (ГПВ) з публічного Telegram-каналу (веб-версія `t.me/s/<канал>`): усі черги 1.1–6.2 на сьогодні й завтра. Для кожної дати береться найсвіжіший пост («станом на …»).

З'єднання відкривається лише по IPv4. Якщо в мережі немає маршруту IPv6, а `t.me` першою віддає IPv6-адресу, запит з Home Assistant висить до тайм-ауту — звідси помилки `Timeout while fetching data`.

Друкує JSON:

```json
{"state": "14.11 15:01", "today": "14.11.2026", "tomorrow": "", "updated": "15:01",
 "t": {"1.1": "18-20", "1.2": "18-20", "4.1": "00-00:30,16-18"}, "n": {}}
```

Використання в Home Assistant (command_line):

```yaml
command_line:
  - sensor:
      name: "Графік відключень"
      command: "python3 /config/scripts/outage_schedule.py"
      command_timeout: 40
      scan_interval: 900
      value_template: "{{ value_json.state }}"
      json_attributes: [today, tomorrow, updated, t, "n"]
```

Канал задано в `URL` на початку скрипта.
