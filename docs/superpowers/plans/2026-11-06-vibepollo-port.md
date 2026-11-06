# Port of the Apollo fork features to Vibepollo

Date: 2026-11-06. Base: `Nonary/Vibepollo` release tag `2.0.6`, commit `5e6ed8594`. Branch: `feature/apollo-port`. Requirements: issue #19 in `craigmdennis/Apollo`.

## Goal

The host build is Vibepollo with every feature of the `craigmdennis/Apollo` fork. The build installs on the Windows streaming PC. Selene (the Apple TV client) and Calliope (the microphone app) connect to it without a client change. Sleep, Steam game activity, and the integration API work.

## Constraints

- Selene ships through App Store review, so the host keeps every route and response shape that Selene uses: `/moonlight/steam`, `/moonlight/steam/art/{appId}/{kind}`, `/moonlight/wallpaper`, `POST /moonlight/sleep`, and the `activeclient` element in HTTPS `serverinfo`.
- Calliope keeps its protocol: `/api/mic/*` on the config server, the mic token as a Bearer credential, and UDP on base port + 13.
- Vibepollo installs into `Program Files\Apollo` and replaces `ApolloService`, so the existing paired clients and `mic_state.json` carry over.
- A feature that Vibepollo already has replaces the fork's version. The fork adds only what is missing.

## Reconciliation

| Fork feature | Vibepollo | Decision |
|---|---|---|
| Calliope remote microphone: mic module, UDP receiver, WASAPI writer, pairing, tray notices, config routes, web UI | Missing | Port. Calliope routes register directly on the server, because Vibepollo's `authenticate()` treats every Bearer header as an API token. Web UI routes use `register_api_route`, so CSRF checks, session checks, and token scopes apply. |
| Steam activity routes and `activeclient` | Missing. Vibepollo's Steam code imports games as apps. | Port the self-contained `steam_activity` module. `activeclient` reads `stream::get_all_session_info()`. |
| `share_activity` permission and `share_game_activity` setting | Missing | Port. The bit `0x00200000` is free in Vibepollo's `PERM`. |
| `POST /moonlight/sleep` and the `sleep` permission | Vibepollo has `/power-actions/v1` with a per-client `allow_sleep` opt-in | Keep Vibepollo's service and opt-in. Add `/moonlight/sleep` as an adapter that answers in the fork's format. Drop the `sleep` bit. On load, a client stored with the old bit `0x00400000` and no `allow_sleep` key gets `allow_sleep = true`. |
| Read-only API key (`/api/token`) for Home Assistant | Vibepollo has scoped API tokens | Drop the fork's key. Register `GET /api/mic/list` so that a scoped token can reach it. |
| Connected API for home automation (issue #19, phase 1) | Missing. `/api/session/status` and `/api/rtsp/sessions` carry no client identity or address. | Add `GET /api/sessions` with the issue's contract, and `streaming` and `session_count` in `GET /api/metadata`. |
| Request body cap on the GameStream servers | Missing on nvhttp | Port. |
| Log redaction, password setup origin check, cookie hardening, client list platform, AMF profile fix, empty server command packet | Present | Skip. |
| Update channel | Checks `Nonary/vibepollo` | Point it at `craigmdennis/Vibepollo`, so an upstream release never offers to replace this build. |
| Windows build and install scripts | Vibepollo builds an MSI with WiX in CI | Port `build-windows.ps1` and `install-local.ps1`, adapted to the MSI. |

## Upstream Apollo commits missing from Vibepollo

Vibepollo `2.0.6` lacks 22 commits of `ClassicOldSong/Apollo`. The security backport (GHSA-ph75-mgxh-mv57, `9fc623ddf`) removes the acceptance of `X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY`; Vibepollo removed it on 2026-05-22. The credentials corruption fix (`7ec7566ad`) is superseded by `statefile::write_json_atomic`. The rest change documentation, Linux encoders, and capture timestamps.

## Tasks

1. Steam activity: module, tests, routes, `activeclient`, permission, setting.
2. Sleep adapter route and the `allow_sleep` migration.
3. Remote microphone: module, platform writer, config routes, startup, tray notices, tests.
4. Web UI: permission checkbox, setting toggle, microphone pairing and list, strings.
5. Request body cap on nvhttp.
6. Update channel.
7. Build and install scripts.
8. Documentation: README section, configuration, API reference, microphone guide.
9. Verification: standalone tests on macOS, web UI build, Windows CI build, then the Windows checklist below.

## Windows checklist

Run on the streaming PC after installing the build.

1. Selene lists the PC as paired, shows the last Steam game, its art, and the wallpaper.
2. While Selene streams, a second client sees `activeclient` with Selene's device name.
3. Sleep from Selene's hold menu suspends the PC. With an app left running and no stream, sleep still works.
4. Calliope connects, and Steam voice chat hears it.
5. A token scoped to `GET /api/sessions` returns `streaming: true` with Selene's name while Selene streams, `false` within one poll after the stream ends, and 403 for `/api/apps`.
