# Brain Dump — Pebble

> Fork of [adrienthiery/pebble-watchface-agent-skill](https://github.com/adrienthiery/pebble-watchface-agent-skill)

**Brain Dump** is a Pebble app that turns a quick voice note into the right action.
Press SELECT, speak, and an on-device NLP classifier routes what you said — no extra
cloud round-trip for routing. Destinations include **Google Tasks, Todoist, Nextcloud
Tasks, Notion, Nextcloud Notes, a custom webhook, an AI agent, and on-watch reminders**.
Due dates and priorities are parsed from natural language (EN/DE/FR/ES).

## What this fork adds

- **Nextcloud Tasks** as a first-class task destination, via CalDAV/`VTODO` (due date +
  priority mapping, dedicated config — separate from the existing Nextcloud Notes).
- **Webhook responses** are shown on the watch: if your endpoint returns a body, it is
  displayed under the status line (the webhook becomes two-way).

## Build & install

```bash
# the ARM toolchain ships with the SDK but isn't on PATH by default
export PATH="$HOME/.local/share/pebble-sdk/SDKs/current/toolchain/arm-none-eabi/bin:$PATH"

pebble build
pebble install --phone <PHONE_IP>   # IP shown in the Pebble app → Devices → Dev Connect
pebble logs --phone <PHONE_IP>      # live routing + destination errors
```

Configure each destination from the Pebble mobile app → Brain Dump → Settings.
