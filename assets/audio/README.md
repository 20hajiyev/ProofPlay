# Procedural audio draft 001

Original deterministic NumPy synthesis. No paid service, recordings, sampled songs, or external source assets.

Run `tools/generate_audio.py` with Python + NumPy to reproduce. 48 kHz / 24-bit WAV. Engines and effects are mono; four music stems are stereo, 128 BPM, 64 bars, 120 seconds, synchronized at frame zero.

Engine loops use a stylized combustion pulse oscillator with load-dependent harmonics and noise. They are not accurate recordings of real engines. Metadata root RPM is nominal synthesis input. Music is an original procedural draft requiring listening, mixing and artistic refinement.

Technical checks decode written PCM and validate format, sample peak, DC and loop endpoint discontinuity. They do not establish audio quality, true peak, loudness compliance or in-engine functionality. Begin music mixing with each stem at -12 dB; never sum normalized stems at unity gain.

See manifest_fragment.json for IDs, hashes, loop sample markers, provenance and preview cue sheet. See validation_report.json for actual decoded measurements.
