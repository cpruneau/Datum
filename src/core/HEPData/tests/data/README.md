# Test data of the HEPData module

- `ALICE_OO_pO_pi0/`: a HEPData record of ALICE, given by C. Pruneau: neutral-pion production in pp (5.36, 9.62 and
  13.6 TeV), pO (9.62 TeV) and OO (5.36 TeV) collisions, and the nuclear modification factors R_OO, R_pO and
  R_OO/R_pO^2 (eight tables of 27 pT bins, errors stat, sys and norm). The numbers are quoted strings, the pT bins
  have both edges and a value, and some tables repeat a keyword name (reactions, cmenergies).
- `TestHEPSubmission/`: submission.yaml and the data files of the HEPData sample submission (ATLAS ZZ cross sections),
  from https://github.com/HEPData/hepdata-submission (examples/submission, commit
  4a16c9edfdba5666563d74812c093e868baca479; Mozilla Public License 2.0): asymmetric errors, data licences, additional
  resources, tables without independent variables. Only the YAML files are kept.
