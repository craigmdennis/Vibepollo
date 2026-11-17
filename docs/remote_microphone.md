# Remote microphone

Vibepollo can receive a microphone from an iPhone or a Mac running Calliope. The voice
arrives on the PC as the Steam Streaming Microphone, which Steam voice chat, games, and
Discord read as an ordinary microphone. Moonlight is not involved and needs no change.

## Requirements

- Vibepollo on Windows.
- Steam installed on the PC. Steam supplies the Steam Streaming Microphone driver.
- Calliope on an iPhone or a Mac on the same network as the PC.

## Pair a microphone

1. Open Calliope and choose the PC. Calliope shows a 4-digit PIN.
2. On the PC, open the Vibepollo web UI and go to the Clients page. The tray notice
   "Incoming microphone pairing request" opens it directly. In the V2 web UI, the same
   card is on the Devices page.
3. In the Microphones card, enter the PIN and, optionally, a name for the device.
4. Select Pair microphone. The device appears in the list under the form.

A PIN is valid for 120 seconds. After three wrong PINs, Vibepollo cancels the request.
Tap Get a new PIN in Calliope to start again.

## Use the microphone

Tap Connect in Calliope. While connected:

- Vibepollo makes the Steam Streaming Microphone the default microphone on the PC.
- The tray shows "Microphone connected" with the device name.
- The Microphones list shows Connected beside the device.

When Calliope disconnects, Vibepollo restores the previous default microphone. Vibepollo also
restores it on exit, and at the next start after a crash.

One microphone is connected at a time. When a second paired device connects, it
replaces the first, and the first shows a message.

## Remove a microphone

In the Microphones card, select Remove beside the device. A
connected device disconnects immediately.

## Network

Calliope sends audio to UDP port 48002 when Vibepollo uses the default base port. The port
is the base port plus 13. The installer's firewall rule allows every port for `sunshine.exe`,
so it covers this one.

## Home automation

`GET /api/mic/list` lists paired microphones and marks the connected one. An API token
scoped to `GET /api/mic/list` can read it. Create the token on the API tokens page.

## Troubleshooting

| Message | Action |
|---|---|
| Apollo cannot find the Steam Streaming Microphone | Install Steam on the PC, then connect again |
| Another program is blocking the Steam Streaming Microphone | Close Steam Remote Play or the program using the device, then connect again |
| This host cannot receive a microphone | The PC runs Vibepollo on Linux or macOS. The microphone needs Vibepollo on Windows |
| Apollo is still opening the microphone | The first connection installs the Steam driver, which takes several seconds. Connect again. The web UI does not respond during that time |
| The PC lists the device, and Calliope still waits for the PIN | The pairing answer did not reach Calliope. Remove the device under Microphones, then pair again |
| Calliope shows Mic live and the PC hears nothing | Choose Copy diagnostics in Calliope. The Host counters line states whether the PC received and played the audio. A game started before the connection may need a restart to pick the Steam Streaming Microphone |
