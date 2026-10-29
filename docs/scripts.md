# Writing scripts

A script is plain text. Kalipso reads it word by word; two kinds of markup help you while you read.

## Cues in square brackets

Anything between `[` and `]` on one line is a **cue**: it is shown in italics but never matched against your voice, and steady scrolling passes over it.

```text
Thank you all for coming. [smile] [look at camera]
Today we launch the new plan. [pause]
```

A `[` without a closing `]` on the same line is ordinary text.

## Sections

A line that starts with `#` (as in Markdown) is a **section heading**. It is shown in bold, it is not matched against your voice, and it appears in the reader's **Sections** menu so you can jump straight to it.

```text
# Opening
Good morning, everyone.

# Results
Revenue grew by twelve percent.
```

Jumping to a section places the reading position just before its first word.

## Importing

The standalone app imports:

| File | What you get |
| --- | --- |
| `.txt`, `.md` | The text as is (UTF-8). |
| `.pptx` (PowerPoint) | The speaker notes of each slide, under a `# Slide N: Title` heading. Slides without notes are skipped. |
| `.odp` (LibreOffice Impress) | The same, from Impress speaker notes. |

Slide text itself is not imported, only the notes you wrote for yourself. **Export…** saves the current script as `.txt` or `.md`.

## Where scripts are stored

- **Standalone app:** one text file per script in the application data folder (`~/.local/share/brianidavide/kalipso/scripts` on Linux), plus `library.json` with titles and reading positions.
- **Plasma widget:** in the widget's own Plasma configuration.
- **GNOME extension:** in `~/.local/share/kalipso/gnome-extension.json`.

Nothing is uploaded anywhere.
