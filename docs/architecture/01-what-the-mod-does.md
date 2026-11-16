# 1. What the mod does

Part of the [rs7string architecture](../ARCHITECTURE.md).


rs7string is a `dinput8.dll` proxy that patches the running game to support seven- and eight-string
guitars and five- and six-string basses. Ordinary instruments keep their normal behaviour, although
they also run through shared patched code.

## From chart to gameplay

The chart declares the extended instrument; the mod uses that information to build the instrument
and calculate the expected notes.

```mermaid
flowchart LR
    chart["Extended chart<br/>tuning and RS7X"] --> model["Model builds<br/>the instrument"]
    model --> play["Highway, tuner<br/>and detection"]
```

For a seven-string guitar, the SNG tuning has seven entries. String index 6 is the low B; its
expected MIDI note is `35 + tuning offset + fret`. See [String-count model](03-the-string-count-model.md)
and [Detection and scoring](06-detection-and-scoring.md) for the implementation and evidence.
