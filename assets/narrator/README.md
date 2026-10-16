# Narrator source assets

`voice/manifest.json` binds eight retained mono WAVs to their fal request IDs, original text, source hashes and
four-bit PCM hashes. These are voice prototypes generated with `xai/tts/v1`, built-in voice `rex`. No API credential
is stored in the assets or cartridge. Paid generation is separate in `tools/narrator/fal_generate.py`.

Reproduce the cartridge banks offline:

    .venv/bin/python tools/narrator/bake_assets.py
    .venv/bin/python tools/narrator/bake_assets.py --check

The bake removes DC, filters and trims speech, resamples to 8,192 Hz, quantizes to four-bit PCM and pads to 32-sample
wave-channel blocks. Eight clips total 96,432 ROM bytes. A clip fits a single 16 KiB bank. The native timer copies
sixteen bytes per 256 Hz interrupt, restoring the ROM bank before returning. Music advances while speech uses the
wave channel, then its bass waveform is restored. Sequencing follows the GBDK `wav_sample` example; GBDK remains a
separate dependency under its own license.

The shared narrator has 24 event topics and 3,072 host-generated text variants. Only the eight manifest clips are
spoken in the current native build. The larger event/history and voice catalogue remain on the completion ledger.
