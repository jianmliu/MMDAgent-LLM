# MMDAgent-EX

[![en](https://img.shields.io/badge/lang-en-red.svg)](README.md)
[![ja](https://img.shields.io/badge/lang-ja-blue.svg)](README.ja.md)
[![DOI](https://zenodo.org/badge/691418774.svg)](https://zenodo.org/doi/10.5281/zenodo.10427368)
---
**A short demo (tap image to open video)**

[![Demo movie](http://img.youtube.com/vi/iu2gU2uHAcc/0.jpg)](https://www.youtube.com/watch?v=iu2gU2uHAcc)
---

## What is this?

MMDAgent-EX is an open-source platform for voice dialogue systems and avatar communication. It is a standalone system that combines the display and control of CG characters with speech and language processing, allowing you to create various voice dialogue systems. By adding various modules, you can build diverse multimodal dialogue systems. Additionally, it supports features such as lip-synced speech playback and motion playback/control from external software, making it possible to operate as a human-agent frontend for a dialogue system.

MMDAgent was developed at Nagoya Institute of Technology since 2011 as an open-source toolkit for research and development in voice interaction.  Besides the its original version, it has been internally developed continuously for research purposes: support various formats, network capabilities, UI improvements, external control functions, and more.  Since December 2020, its research and development have been progressing in the "Avatar Symbiosis Society" JST moonshot program, as the platform for CG avatar communication. Here, we release some of the research and development results as open source.

MMDAgent-EX has its own OpenGL-based rendering engine compatible with MMD (MikuMikuDance), and can create interactive characters using 3D models and motion data in MMD format. It also includes speech recognition and synthesis engines developed at Nagoya Institute of Technology, such as Julius and Open JTalk, as well as an embedded FST-based state-transition primitive dialogue scripting feature, allowing you to build voice dialogue systems independently. Aside from the internal engines, it can be easily connected to other scripts like Python, so you can add another modules such as cloud-based speech recognition, speech synthesis engines, or large language models like ChatGPT with this system. Furthermore, it supports external control via socket connections, making it suitable for use as the input/output frontend for existing dialogue systems.

All documents are available at the [official site](https://mmdagent-ex.dev/).

## Running Environments

Windows / macOS / Linux / WSL

## PMX models (this fork)

`.pmx` models (PMX 2.0 / 2.1) load directly, without converting them to PMD + a PMXEditor CSV:
`MODEL_ADD|alias|model.pmx`, or drop the file on the window. The loader
(`Library_MMDFiles/src/lib/PMDModel_parsePMX.cpp`) fills the same structures as the PMD + CSV
path, so a PMX behaves like its converted version: 4-bone and SDEF skinning, bone inheritance and
process layers, IK (links limited to bending one way about X act as knees), vertex / bone / UV /
material / group morphs, material flags and edge colors, full texture paths, own and shared toon
textures, rigid bodies and joints.

Not supported: additional UVs, QDEF (loaded as BDEF4), fixed and local bone axes, external
parents, flip and impulse morphs, soft bodies; PMX 2.1 joint types other than spring 6DOF are
loaded as spring 6DOF.

## Material colors at run time (this fork)

`MODEL_MATERIALCOLOR|(model alias)|(material name, or * for all)|r,g,b` multiplies a material's
color from now on (until the model is reloaded); `1,1,1` removes it. MMDAgent-EX answers with
`MODEL_EVENT_MATERIALCOLOR|(model alias)|(material name)`, or logs a warning when no material has
that name. In toon shading the tint is the color shown: a material lit brighter than white
(diffuse / 2 + ambient above 1) is brought down to white first. Like a multiply material morph,
it only recolors light textures; dark texels stay dark.

## Freezing physics per rigid body (this fork)

`MODEL_FREEZEPHYSICS|(model alias)|(rigid body name pattern, * = any)|ON` holds the matching
simulated rigid bodies kinematic: they follow their bones (and the bones follow their parents
and motions) instead of being simulated, while still colliding with the rest. `OFF` simulates
them again. MMDAgent-EX answers `MODEL_EVENT_FREEZEPHYSICS|(alias)|(pattern)|(count)`.
Useful for dense cloth nets (skirts of hundreds of jointed bodies) that never settle in
MMDAgent-EX's Bullet setup and shake. Rigid body names now come from the model (PMD and PMX).

## Remote control over WebSocket (this fork)

- MMDAgent-EX keeps running when the WebSocket server goes away and reconnects to it (as many
  times as `Plugin_Remote_RetryCount` allows per outage). Before, losing the connection ended the
  process on macOS. Models, motions and the avatar's audio thread stay as they were; avatar
  control is switched off until the server sends `__AV_START` again.
- On every connection, including reconnections, the server receives
  `REMOTE_EVENT_CONNECTED|(host)`. After a reconnection there is no `PLUGIN_EVENT_ENABLE|Remote`,
  and the models added before are still loaded, so a server should take this event as its start
  and replace its models (`MODEL_CHANGE`) rather than add them again.
- Commands that open other content, change settings, write or upload files, load scripts,
  switch plugins, press keys or add buttons are not accepted from the other end by default: `OPEN_CONTENT`, `KEYVALUE_SET`,
  `LOG_START`, `LOG_UPLOAD`, `AVATAR_LOGSAVE_START`, `RECOG_RECORD_START`, `RECOG_LOGLOC`,
  `MOTIONCAPTURE_START`, `NETWORK_GET`, `FST_LOAD`, `SUBFST_START`, `SUBFST_START_IF`,
  `PLUGIN_ENABLE`, `PLUGIN_DISABLE`, `REMOTEKEY_CHAR`, `REMOTEKEY_DOWN`, `REMOTEKEY_UP`,
  `HOME_SET`, `HOME_CLEAR`, `BUTTON_ADD`. Set your own comma-separated list with
  `Plugin_Remote_DenyCommands=...` in the .mdf, or `Plugin_Remote_DenyCommands=NONE` to accept
  everything as before. Lines of 2048 bytes or more are ignored.

## Companion mode (this fork)

When an app drives MMDAgent-EX (a desktop companion, a kiosk), set `companion_mode=true` in the
content .mdf. MMDAgent-EX then leaves the content-browser UI out and refuses what would leave or
change the content from its window:

- no tab bar (home / readme / bookmark / history / menu) and no red "PMX" mark;
- Esc does not quit (close the window to quit), Del does not delete a model, dropped files are
  ignored, and the keys that reload (R), browse (O), show history (H), update (!) or open other
  programs (e, E, D, Ctrl+V) do nothing;
- the menu (`/`) keeps the window, view and camera items; browsing content, Home, reload, system
  update and deleting bookmarks or caches are greyed out, and the bookmark page is gone.

Other keys (f fullscreen, t transparent window, s top indicator, camera keys...) work as usual.
`show_fps=false` hides the top-left indicator. Without `companion_mode` nothing changes.

## Full screen on a chosen display (this fork, macOS)

`full_screen=true` goes full screen on the display the window opens on, normally the main one.
To use another display (a small round HDMI screen, say), add `full_screen_display` to the .mdf:

- `full_screen_display=720x720` picks the first display of that size, in points or in pixels;
- `full_screen_display=1` picks that display by number (0 = the one with the menu bar).

Before going full screen, at start or with the f key / [Window] menu, MMDAgent-EX moves its
window onto that display. If no display matches it logs a warning and goes full screen where the
window is. Other platforms ignore the key.

## Plugins on macOS (this fork)

On macOS the plugins no longer carry their own copy of the engine libraries (MMDAgent, MMDFiles,
GLFW, Bullet, libpng, FreeType): the executable links them whole and the plugins use its copy
when it loads them. Before, each plugin had a private copy, with GLFW state that was never
initialized and Objective-C classes registered twice ("Class GLFWThread is implemented in both
... may cause spurious casting failures and mysterious crashes"); each plugin was also about
2.8 MB larger. Other platforms are unchanged.

## Tutorial

There is a [tutorial doc](https://mmdagent-ex.dev/docs/build/) that guides you how to build and run MMDAgent-EX.

### Windows

There are win32 pre-built binaries for [each release](https://github.com/mmdagent-ex/MMDAgent-EX/releases), uploaded as `MMDAgent-EX-win32-vx.x.x.zip`.  You can download that of the latest version, and start from ["Running Examples" page](https://mmdagent-ex.dev/docs/run/), skipping the first build phase.

### Other OS

Please build MMDAgent-EX by your own.  The overall procedure is fully described in [the document's tutorial page](https://mmdagent-ex.dev/docs/build/)

## License

MMDAgent-EX is distributed under the Apache License, Version 2.0.

MMDAgent-EX depends on various open-source softwares and libraries. Some of them are also included in this repository.  For licensing issues, please consult each license of the software:

- MMDAgent
- Bullet Physics
- FreeType
- GLFW
- GLEW
- libjpeg
- libpng
- librdkafka
- libsndfile
- libsamplerate
- UTF8-CPP
- google/re2
- zlib
- PortAudio
- Open JTalk
- Julius
- POCO C++ Libraries
- RabbitMQ
- cpp-httplib

When using materials in this system, i.e. MMD 3D model, music, motions etc., please take care of their licenses.

## ELSI Guidelines

These guidelines outline the ethical, legal and social issues to be observed when using MMDAgent-EX.

Disallowed usage of our software:

- Illegal activity
- Child Sexual Abuse Material or any content that exploits or harms children
- Promotion of hateful, harassing, or violent content
- Activity that has high risk of physical harm, including:
  - Weapons development
  - Military and warfare
  - Management or operation of critical infrastructure in energy, transportation, and water
  - Content that promotes, encourages, or depicts acts of self-harm, such as suicide, cutting, and eating disorders
- Fraudulent or deceptive activity, including:
  - Scams
  - Coordinated inauthentic behavior
  - Plagiarism
  - Disinformation
  - Impersonation or misrepresentation of an actual person, group, or organization for the purpose of deceiving others
- Actions harmful to the MikuMikuDance related community and its fun society:
  - Creation and disribution of contents that violates the rights of copyright holders of models, motions, music, etc.
  - Ignoring guidelines set by the copyright holders.
  - Unauthorized redistribution, plagiarism, and impersonation of the original creators.

## Links

- [Official site](https://mmdagent-ex.dev/)
- Related repositories:
  - [Example content](https://github.com/mmdagent-ex/example)
  - [CG Avatar Gene](https://github.com/mmdagent-ex/gene)
  - [CG Avatar Uka](https://github.com/mmdagent-ex/uka)

## Contacts

- Twitter/X [@MMDAgentEX](https://twitter.com/MMDAgentEX)（for announcements）
- mmdagent-ex-official@lee-lab.org (for inquiries about research, development, and licensing)
-  [Lee Laboratory, Nagoya Institute of Technology](https://www.slp.nitech.ac.jp/en/)

## Citation

**APA**

```text
Lee, A. (2023). MMDAgent-EX (Version 1.0.0) [Computer software]. https://doi.org/10.5281/zenodo.10427369
```

**BibTeX**

```text
@software{Lee_MMDAgent-EX_2023,
  author = {Lee, Akinobu},
  doi = {10.5281/zenodo.10427369},
  license = {Apache-2.0},
  month = dec,
  title = {{MMDAgent-EX}},
  url = {https://github.com/mmdagent-ex/MMDAgent-EX},
  version = {1.0.0},
  year = {2023}
}
```

## Acknowledgments

MMDAgent-EX:

- Akinobu Lee (Nagoya Institute of Technology, Japan)

MMDAgent:

- Keiichi Tokuda (Nagoya Institute of Technology, Japan)
- Akinobu Lee (Nagoya Institute of Technology, Japan)
- Keiichiro Oura (Nagoya Institute of Technology, Japan)
- Daisuke Yamamoto (Nagoya Institute of Technology, Japan)
