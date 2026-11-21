# Elero radio remote manuals

Use the model name on the remote to find its manual. Open the PDF for button symbols, diagrams, and tables. Search the English text across all models from the root of this repository.

```sh
rg -in 'programming mode|teach-in|intermediate position' docs/manuals/en -g '*.md'
```

For status light colours and basic pairing, see Elero’s two-page guide. Pairing sets a receiver to accept commands from a transmitter. [PDF](en/transmitter-programming.pdf) · [Searchable text](en/transmitter-programming.md) · [Elero original](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bcontroller%5D=Download&tx_avelero_downloads%5Bdownload%5D=1298&cHash=72809734d88e40d8f3fda759204ee1f3)

For a comparison of remote actions and supported radio commands, see the [command coverage review](../research/remote-command-coverage.md).

## Model index

The project has recorded radio packets from MonoTel, VarioTel, and TempoTel. The other entries cover models for which Elero documents bidirectional radio operation. In this mode, the receiver can send a reply. A manual alone does not confirm operation with this component. Match the radio band on your device to the band on your hub. Some manuals also describe regional versions or older modes. [Project radio observations](../../.pi/skills/elero-protocol/SKILL.md#observed-p--learn-in-rf-envelopes)

The page column gives the English pages in the original Elero PDF. For a multilingual manual, page 1 of the local copy is the first page in that range.

### Handheld remotes

| Model | Local manual | Original PDF pages | Source |
|---|---|---|---|
| MonoTel 2 (Progreso1) | [PDF](en/monotel-2.pdf) · [Text](en/monotel-2.md) | 1–12 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=320&cHash=2b5c961f89e27370f8023983cceef0c1) |
| LumeroTel 2 (Progreso1M) | [PDF](en/lumerotel-2.pdf) · [Text](en/lumerotel-2.md) | 1–11 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=321&cHash=f3f61a28fc8d84b5abf03820b9c35d74) |
| VarioTel 2 (Progreso5M) | [PDF](en/variotel-2.pdf) · [Text](en/variotel-2.md) | 1–11 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=325&cHash=af753ee20fbd7a77ee897b17bf8f1830) |
| TempoTel 2 | [PDF](en/tempotel-2.pdf) · [Text](en/tempotel-2.md) | 1–38 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=324&cHash=19416ea19b003d4510544ed8c8638c8f) |
| MultiTel 2 | [PDF](en/multitel-2.pdf) · [Text](en/multitel-2.md) | 1–23 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=323&cHash=9387c3dfd69f6d52121bbe25ae05a2b4) |
| SoloTel 2 | [PDF](en/solotel-2.pdf) · [Text](en/solotel-2.md) | 1–11 | [Elero](https://www.elero.com/en/downloads-service/downloads/?cHash=14a107bc4d00d9345cb7cd114e1d30c0&tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=480) |
| MonoCom, MonoCom Slide, LumeroCom, VarioCom, VarioCom Slide | [PDF](en/com-series.pdf) · [Text](en/com-series.md) | 5–8 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=1771&cHash=7744feb4df8f2ee4645f841f18133bc5) |

### Son handheld and wall remotes

| Model | Local manual | Original PDF pages | Source |
|---|---|---|---|
| MonoSon-868 Mini, MonoSon W-868 | [PDF](en/monoson-868.pdf) · [Text](en/monoson-868.md) | 11–18 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=2458&cHash=8cbb93dda98be5eb23646d222a3f0989) |
| LumeroSon-868, LumeroSon-868 Slide, LumeroSon-868 Mini, LumeroSon W-868 | [PDF](en/lumeroson-868.pdf) · [Text](en/lumeroson-868.md) | 10–16 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=2456&cHash=ac8125d81fcf1b2cb8ecbf28d1d6b29c) |
| VarioSon-868 Slide, VarioSon W-868 | [PDF](en/varioson-868.pdf) · [Text](en/varioson-868.md) | 11–18 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bcontroller%5D=Download&tx_avelero_downloads%5Bdownload%5D=2688&cHash=a7c162fc47ee60c51c3549cbeb0b7b69) |
| TempoSon-868 Slide | [PDF](en/temposon-868-slide.pdf) · [Text](en/temposon-868-slide.md) | 19–34 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bcontroller%5D=Download&tx_avelero_downloads%5Bdownload%5D=2457&cHash=6def89025364dbe046ade5e2eb6bf9e6) |

### Wall remotes and switch inputs

| Model | Local manual | Original PDF pages | Source |
|---|---|---|---|
| MonoTec-868 | [PDF](en/monotec-868.pdf) · [Text](en/monotec-868.md) | 3–4 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=1650&cHash=5f8a4176b354fab2ff6ef8d327ab3961) |
| QuinTec-868 | [PDF](en/quintec-868.pdf) · [Text](en/quintec-868.md) | 3–4 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=1857&cHash=4a7f80b2bc7e655e8efbfb3a47bb1f67) |
| UniTec-868 | [PDF](en/unitec-868.pdf) · [Text](en/unitec-868.md) | 1–2 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=329&cHash=fbe4942546010484544e44837b407a74) |
| AstroTec-868 bidi (28 320.0006) | [PDF](en/astrotec-868.pdf) · [Text](en/astrotec-868.md) | 1–48 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bcontroller%5D=Download&tx_avelero_downloads%5Bdownload%5D=326&cHash=04a4034e690d9e0405063319d1ba9883) |
| MultiTec Touch-868 (wall touchscreen) | [PDF](en/multitec-touch-868.pdf) · [Text](en/multitec-touch-868.md) | 1–27 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bcontroller%5D=Download&tx_avelero_downloads%5Bdownload%5D=751&cHash=7ae21a1c7fe371d71087a23032aae5fc) |
| Invio-868 (transmitter behind a wall switch) | [PDF](en/invio-868.pdf) · [Text](en/invio-868.md) | 1–13 | [Elero](https://www.elero.com/en/downloads-service/downloads?tx_avelero_downloads%5Baction%5D=download&tx_avelero_downloads%5Bdownload%5D=327&cHash=315c33e07f97ffdeabb6c928db5c9f43) |

## Pair a Tel 2 remote

For the first pairing, open the local PDF at the programming procedure for your model. These links use PDF page numbers. The printed page number can differ.

| Remote | Programming procedure |
|---|---|
| MonoTel 2 | [Page 6](en/monotel-2.pdf#page=6) |
| VarioTel 2 | [Page 6](en/variotel-2.pdf#page=6) |
| TempoTel 2 | [Page 26](en/tempotel-2.pdf#page=26) |
| LumeroTel 2 | [Page 6](en/lumerotel-2.pdf#page=6) |
| MultiTel 2 | [Page 17](en/multitel-2.pdf#page=17) |
| SoloTel 2 | [Page 5](en/solotel-2.pdf#page=5) |

## Sources and search text

The sources are Elero’s English operating instructions. The index covers physical transmitters found in the product catalogue and the download archive on 2026-11-21. Some products share one manual. The SoloTel 2 manual lists the Progreso names in its declaration of conformity. [Elero download archive](https://www.elero.com/en/downloads-service/downloads)

The files in `en/` contain the original English PDF pages and searchable text. Firecrawl Parse and Firecrawl’s local PDF Inspector extracted the text. Use the PDF to verify steps that depend on a symbol, a table column, or the order of buttons.

Earlier German reference copies are also available. [MonoTel 2](monotel2.pdf) · [VarioTel 2](variotel2.pdf) · [TempoTel 2](tempotel2.pdf)
