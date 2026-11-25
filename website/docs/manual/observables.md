# Observables

Datum computes observables selected at run time, each by a family of analysis tasks. **Basic** observables are
filled in a single pass through the events; **derived** observables are computed afterwards from the basic histograms
(the second and third stages of a workflow, [Running analyses](workflows.md#stages)). All observables, integral or
differential, are kept as histograms, one folder per combination of event and particle filters.

| Observable | Library | |
|---|---|---|
| Single-particle spectra | ParticleSingle | densities in $p_T$, $y$ or $\eta$, and $\varphi$ |
| Pair densities and cumulants | ParticlePair | $\rho_2$, $C_2$, normalised $R_2$, as functions of $(p_{T,1},p_{T,2})$, $(y_1,y_2)$, or $(\Delta\eta,\Delta\varphi)$, $(\Delta y,\Delta\varphi)$ |
| Three-dimensional pair analysis | ParticlePair3D | pair observables in three variables |
| Transverse-momentum correlators | PtPt | $\langle\Delta p_T\Delta p_T\rangle$, its normalised form $P_2$, and $G_2$ |
| Balance functions | ParticlePair | of charge and of other conserved quantities |
| Fluctuations | NuDyn | $\nu_{\rm dyn}$ and higher moments of multiplicity distributions |
| Anisotropic flow | ParticleFlow | flow coefficients from multi-particle correlators, with gaps and sub-events |
| Jets | Jets (FastJet) | jet spectra, groomed jets, fluctuation moments, underlying event |
| Global observables | Global | multiplicity, total energy, net charge, strangeness and baryon number in a chosen range |
| Event shapes | Spherocity | spherocity, flattenicity |
| Performance | Performance | efficiencies and resolutions from generated and reconstructed particles |

## Definitions

With $\rho_1$ and $\rho_2$ the single- and pair-particle densities, the pair cumulant and normalised cumulant are

$$
C_2(\mathbf{p}_1,\mathbf{p}_2) = \rho_2(\mathbf{p}_1,\mathbf{p}_2) - \rho_1(\mathbf{p}_1)\,\rho_1(\mathbf{p}_2),
\qquad
R_2 = \frac{\rho_2}{\rho_1\rho_1} - 1 ,
$$

computed for each pair of particle filters $\alpha$, $\beta$ (like-sign, unlike-sign, species pairs). The
transverse-momentum correlator averages the products of the deviations of the $p_T$ of the two particles from their
means, and is normalised by them:

$$
P_2 = \frac{\langle\Delta p_{T,1}\,\Delta p_{T,2}\rangle}{\langle p_T\rangle_\alpha\,\langle p_T\rangle_\beta},
$$

while $G_2$ compares the $p_T$-weighted pair density with the product of the $p_T$-weighted single densities. The
balance functions measure the density of particles $\bar\beta$ associated with a particle $\alpha$, less that of
particles $\beta$, for charge and other conserved quantities.

## Acceptance and normalisation

Each analysis takes its kinematic acceptance from the binning of its histograms and the filters of its particles,
so that predictions are computed under the conditions of a measurement. Histograms are normalised per event, and,
for productions of PYTHIA in $\hat{p}_T$ bins, as differential cross sections ([Event generators](generators.md#pythia-8)).

## Classes

The analyzer and derived-histogram calculator of each library (for example the single-particle analyzer and its
histogram folders), and the base classes of `Particles` ([API reference](../api.md)).
