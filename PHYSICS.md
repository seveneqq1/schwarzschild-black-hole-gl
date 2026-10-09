# Tidal-disruption model

This renderer uses a bounded real-time approximation, not a hydrodynamics solver.
Its behavior is based on the following results from the tidal-disruption literature.

## Deformation and failure

In the local frame of a body falling through a Schwarzschild tidal field, the
leading Newtonian tidal tensor has eigenvalues proportional to `(2, -1, -1)`. The
body therefore stretches radially and compresses along both transverse axes. The
shader uses a volume-conserving ellipsoid with radial scale `L` and transverse
scales `1/sqrt(L)`.

The CPU model estimates the tensile load as

```text
sigma_tidal ~ 2 rho G M_BH R^2 / r^3
```

and compares it with material cohesion plus a self-gravity term. Excess stress
accumulates damage and heat through a damped viscoelastic response instead of
triggering at a fixed distance.

Stone, Sari, and Loeb derive the radial stretching/transverse compression picture,
the rapid dominance of the black-hole tide inside the tidal sphere, and the
approximately ballistic post-disruption motion:

- [Consequences of Strong Compression in Tidal Disruption Events](https://arxiv.org/abs/1210.3374)

## Partial disruption and debris energy

The first failure strips symmetric leading and trailing tails while leaving a
denser, more cohesive core. If that core fails deeper in the potential it is fully
disrupted. Child radii are derived from their mass fractions, so total fragment
mass is conserved.

At failure, fragments receive a symmetric spread in specific orbital energy using

```text
Delta epsilon ~ G M_BH R / r_disruption^2
```

applied as equal-and-opposite velocity changes along the orbital direction. This
preserves total linear momentum while producing both more-bound and less-bound
debris. Stone et al. motivate freezing the energy spread near disruption rather
than scaling it with the later pericenter depth. Guillochon and Ramirez-Ruiz show
with adaptive-mesh hydrodynamics that partial events can retain a surviving core
and produce fallback histories that depend on internal structure:

- [Hydrodynamical Simulations to Determine the Feeding Rate of Black Holes by the Tidal Disruption of Stars](https://arxiv.org/abs/1206.2350)

## Stream and aftermath

Disrupted fragments use nearly ballistic motion with much less artificial drag
than intact interactive bodies. Tidal shear and the frozen-in energy spread form a
narrow radial stream. Fragment collisions are resolved inelastically with
mass-weighted impulses; lost kinetic energy becomes surface heat. This approximates
stream self-intersection shocks without adding an unbounded particle system.

Coughlin, Nixon, Begelman, and Armitage show that disrupted material forms a thin
filament whose transverse structure can remain confined by self-gravity:

- [On the structure of tidally disrupted stellar debris streams](https://academic.oup.com/mnras/article/459/3/3089/2595173)

Captured bound debris produces a short disk/corona brightening with a late-time
`t^(-5/3)` envelope. This is deliberately used only as an asymptotic visual model;
Lodato, King, and Pringle show that early fallback depends on internal structure
and need not follow that slope:

- [Stellar disruption by a supermassive black hole: is the light curve really proportional to t^-5/3?](https://arxiv.org/abs/0810.1288)

For dense small bodies, tidal work is stored as internal heat and exposed cracks
become incandescent. This follows calculations finding that tidal squeezing near a
massive black hole can melt comet- or asteroid-like bodies:

- [Tidal effects on small bodies by massive black holes](https://arxiv.org/abs/0901.3447)

## Performance bounds

- At most 24 bodies/fragments are uploaded through OpenGL 4.1 uniform arrays.
- A body undergoes at most two disruption stages.
- Fragment collision work is capped at `24 * 23 / 2` inexpensive CPU pairs.
- No new per-step GPU loops, SSBOs, compute shaders, or unbounded particles are used.
- Debris heat and strain reuse the existing packed object uniform.
