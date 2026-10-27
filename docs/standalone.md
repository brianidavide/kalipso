# Standalone editor

Run `kalipso` after building or installing the native application. Use `kalipso --demo --prompter` to open a sample script without microphone or network access.

The editor can open and save script files and show a separate reading window. Choose Moonshine or Vosk and configure its extracted model and compatible native library in Settings. On macOS/Windows, [system recognition](system-speech.md) is also available. Listening starts explicitly; Linux audio capture uses PipeWire. Settings retain speech engine and model paths. The reader shows listening status and a pause control. Hiding the reader stops listening; showing it again does not restart the microphone. Interface text size also scales reader controls, while script size is set independently.

Conversation, meeting transcription and LLM assistance are deferred. Their experimental implementation is not exposed in the first-release interface.

An optional KWin script in `packaging/kwin/` positions the reading window and requests capture exclusion when the compositor exposes that property. This does not guarantee invisibility in screen sharing. Verify the remote participant's view.
