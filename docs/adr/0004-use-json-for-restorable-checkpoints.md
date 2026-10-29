# Use JSON for restorable checkpoints

Replace binary persistence checkpoints with JSON checkpoints, without retaining a
loader for old binary formats. Preserve independent restoration and identical future
behavior, including RNG, suspended effects and stale-handle rejection; migrate the
existing persistence tests to verify those guarantees through JSON.

The user selected JSON in place of the current binary encoding. This changes the
binary checkpoint choice in the simulator design and architecture; it does not
change ADR 0003's in-memory memcpy cloning contract. Existing comparison snapshots
omit simulator continuation and identity metadata and cannot meet this guarantee
unchanged. The module boundary and JSON format are still under discussion.
