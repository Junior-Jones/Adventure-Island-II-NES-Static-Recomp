# Starter NES active authority inventory

The sole active Starter package is `Starter NES(4).zip` (SHA-256 `a895ce1d21ce41d10ef9393b47bdb2f315db42e1a6af2477f9b79a03a1923703`).
Its internal documentation remains Revision 3 and adds the 2026-08-18 construction-flow update.
The previous active Starter package was removed from the live workspace; V01-V04 sealed backups remain historical evidence and are not rewritten.

The installed package passes `Tools/verify_package_manifest.py`: 66 manifest-controlled files, aggregate SHA-256 `c0153945da4fda6a19e16556c97dd77c07f5d5ddb8bc034fc0cb967dd5b41a46`. Including `PACKAGE-MANIFEST.json`, 67 package files are installed. Every installed package file was fully read/hash-inventoried for this update. The six binary/archive package members were identified separately; both nested MesenCE source archives passed ZIP integrity and were unpacked.

The Linux and Windows NES-only MesenCE 2.2.1 source trees each contain 900 files. Their normalized relative source trees have 900 common paths: 899 are byte-identical and only the platform `README-NES-ORACLE.txt` differs. The Linux oracle self-test passes. These trees remain oracle/reference material and never production execution authority.

The new mandatory chapter is `Docs/ROM TO COLD BOOT AND STATIC CLOSURE.md`. It requires static ROM/mapper/proof-driven construction, a reset-vector-to-frame-zero cold-boot loop, offline mapper-aware fixed-point work queues, and eventual ROM-wide closure. It explicitly separates cold-boot closure, CPU-graph closure, machine closure and post-V10 qualification closure. A title screen or gameplay route cannot prove static completeness.

Adventure Island II remains on the new-core V01-V10 track. The U00-U15 existing-project modernization documents are retained only as architectural reference. The Mega Man 2 frontend/source is reusable host architecture only; none of its ROM identity, MMC1 behavior, game code or routes are authority for this game.
