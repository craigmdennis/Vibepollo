# Home Assistant: which client is streaming

Vibepollo already reports the active streams at `GET /api/history/sessions/active` on the web UI port (47990, HTTPS). Home Assistant polls it with a scoped API token. The route reads Vibepollo's session history, so **Session history** must stay on (`session_history_enabled`, on by default).

## Create a token

In the web UI, open **API Tokens** (`https://<pc>:47990/v2/api-tokens`) and create a token with one scope: path `/api/history/sessions/active`, method `GET`.

The token can read that route and nothing else.

## Request and response

```bash
curl -k -H "Authorization: Bearer <token>" https://<pc>:47990/api/history/sessions/active
```

`-k` accepts the host's self-signed certificate. With no stream, `sessions` is empty. Each active stream has these fields, among others:

| Field | Meaning |
|---|---|
| `client_name`, `device_name` | The streaming client and its device |
| `app_name` | The app being streamed |
| `width`, `height`, `target_fps`, `hdr`, `codec` | The stream's video settings |
| `uptime_seconds` | Time since the stream started |
| `actual_fps`, `actual_bitrate_kbps`, `encode_latency_ms` | Live statistics |
| `samples`, `events` | Statistics history for the dashboard graphs |

## configuration.yaml

```yaml
rest:
  - resource: https://<pc>:47990/api/history/sessions/active
    headers:
      Authorization: !secret vibepollo_bearer   # "Bearer <token>" in secrets.yaml
    verify_ssl: false
    scan_interval: 15
    binary_sensor:
      - name: Vibepollo streaming
        value_template: "{{ value_json.sessions | length > 0 }}"
    sensor:
      - name: Vibepollo stream device
        value_template: >-
          {{ value_json.sessions[0].device_name if value_json.sessions else 'none' }}
        json_attributes_path: "$.sessions[0]"
        json_attributes:
          - client_name
          - app_name
          - width
          - height
          - target_fps
          - hdr
          - uptime_seconds
```

`binary_sensor.vibepollo_streaming` is on while any client streams. `sensor.vibepollo_stream_device` names the device, with the app and resolution as attributes. While the PC sleeps, both sensors are unavailable; an automation can treat that as not streaming.
