# Event generators

The generators and readers produce events for the analyses; they meet only through the events and tasks of the
Particles library, so that every generator works with every analysis. Example workflows of each are in `projects`.

## BasicEventGen

A simple generator of particles with uniform azimuth and rapidity distributions and exponential $p_T$ spectra, for
tests and quick studies.

## Therminator3

A thermal-model generator derived from Therminator 2: particles are emitted from a freeze-out hypersurface with
thermal distributions, then the unstable hadrons decay according to the particle database (`DB/ParticleData`). Several
freeze-out models are provided; the inputs are in `DB/Therminator`.

## PYTHIA 8

The interface to PYTHIA 8 (optional, `-DDATUM_WITH_PYTHIA8=ON`) runs Pythia with the settings of the workflow.
Productions of hard processes are made in **bins of $\hat{p}_T$**, run as separate jobs and combined by the
sub-sample tools as differential cross sections in mb, or per event of the range covered.

## Herwig 7

The interface to Herwig 7 (optional) runs Herwig in the job, configured as on its command line: an input file
(`.in`) of repository commands is turned into a run file that holds the event generator.

## Event readers

Readers of the events of EPOS and PHSD; with ROOT, readers of ROOT event trees.

## Glauber model

A Glauber model of the collision geometry: nuclear density profiles, nucleon&ndash;nucleon collisions, numbers of
participants and binary collisions, and the related event classes.

## Particle database

`DB/ParticleData` is an XML file of the particles and their decay modes, used by the generators and the filters, and
edited with `ParticleDbEditor.py`.

## Classes

The generator task of each library under `src/models` ([API reference](../api.md)).
