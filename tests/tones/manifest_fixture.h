#pragma once

namespace tonetest {
// Synthetic Python json.dumps(indent=2, ensure_ascii=False); demo nesting and relevant key order.
inline constexpr char kManifest[] = R"json({
  "Entries": {
    "0123456789ABCDEF0123456789ABCDEF": {
      "Attributes": {
        "ArrangementName": "Lead",
        "ArrangementProperties": {
          "pathLead": 1
        },
        "SongName": "Synthetic étude",
        "Tone_A": "alpha",
        "Tone_B": "bravo",
        "Tone_Base": "alpha",
        "Tone_C": "charlie",
        "Tone_D": "delta",
        "Tone_Multiplayer": "",
        "Tones": [
          {
            "GearList": [],
            "ToneDescriptors": [],
            "NameSeparator": " ",
            "IsCustom": true,
            "Volume": 0.0,
            "Key": "alpha",
            "Name": "alpha"
          },
          {
            "GearList": [],
            "ToneDescriptors": [],
            "NameSeparator": " ",
            "IsCustom": true,
            "Volume": 0.0,
            "Key": "bravo",
            "Name": "bravo"
          },
          {
            "GearList": [],
            "ToneDescriptors": [],
            "NameSeparator": " ",
            "IsCustom": true,
            "Volume": 0.0,
            "Key": "charlie",
            "Name": "charlie"
          },
          {
            "GearList": [],
            "ToneDescriptors": [],
            "NameSeparator": " ",
            "IsCustom": true,
            "Volume": 0.0,
            "Key": "delta",
            "Name": "delta"
          },
          {
            "GearList": [],
            "ToneDescriptors": [],
            "NameSeparator": " ",
            "IsCustom": true,
            "Volume": 0.0,
            "Key": "echo",
            "Name": "echo"
          },
          {
            "GearList": [],
            "ToneDescriptors": [],
            "NameSeparator": " ",
            "IsCustom": true,
            "Volume": 0.0,
            "Key": "foxtrot",
            "Name": "foxtrot"
          }
        ],
        "ExtendedRange": {
          "Version": 1
        },
        "Tuning": {
          "string0": -2,
          "string1": -2,
          "string2": -2,
          "string3": -2,
          "string4": -2,
          "string5": -2,
          "string6": -2
        },
        "Tone_E": "echo",
        "Tone_F": "foxtrot",
        "Tone_G": "",
        "Tone_H": "",
        "Tone_I": "",
        "Tone_J": "",
        "Tone_K": "",
        "Tone_L": "",
        "Tone_M": "",
        "Tone_N": "",
        "Tone_O": "",
        "Tone_P": "",
        "Tone_Q": "",
        "Tone_R": "",
        "Tone_S": "",
        "Tone_T": "",
        "Tone_U": "",
        "Tone_V": "",
        "Tone_W": "",
        "Tone_X": "",
        "Tone_Y": "",
        "Tone_Z": ""
      }
    }
  },
  "ModelName": "Synthetic",
  "IterationVersion": 2,
  "InsertRoot": "Static.Songs.Entries"
})json";
}  // namespace tonetest
