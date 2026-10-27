# Standalone editor

Run `kalipso` after building or installing the native application. Use `kalipso --demo --prompter` to open a sample script without microphone or network access.

The editor can open and save script files and show a separate reading window. Choose Moonshine or Vosk and configure its extracted model and compatible native library in Settings. On macOS/Windows, [system recognition](system-speech.md) is also available. Listening starts explicitly; Linux audio capture uses PipeWire. Settings retain paths and provider configuration; API keys stay in memory.

The standalone application also contains experimental meeting transcription and LLM tools. They are not included in the Plasma/GNOME frontend MVP. Selecting and approving a question sends it and up to 8,000 recent transcript characters to the configured chat-completions endpoint. Approving the returned answer appends it to the editable script. Remote endpoints receive that text and apply their own retention policies. Audio is not uploaded to the LLM endpoint.

An optional KWin script in `packaging/kwin/` positions the reading window and requests capture exclusion when the compositor exposes that property. This does not guarantee invisibility in screen sharing. Verify the remote participant's view.
