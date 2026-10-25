# Gates

Each phase ends with a gate. A gate passes only when the commands in the plan's Definition of Done
exit zero and the transcript is committed as `gates/phase-NN.md`. Thresholds live in
`thresholds.toml` and are fixed before the phase that uses them; changing a value requires an ADR
and a `threshold-change` label.
