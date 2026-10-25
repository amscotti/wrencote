# Security

Wrencote is pre-release. Do not run it on untrusted input yet.

## Reporting

Report suspected vulnerabilities privately to the maintainer rather than in a public issue. Include a
minimal reproducer and the affected version.

## Trust model (current)

Version 0.1 assumes **trusted scripts**: capabilities bound what a script can touch, not how much CPU
it can consume. A handler that loops forever stalls its loop thread. Multi-tenant isolation of hostile
scripts is a later, process-based feature. This file will be replaced with a full threat model at the
release gate.
