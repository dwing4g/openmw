OpenMW EldenCombat component

combat.cpp/combat.hpp adapt the public MIT-licensed state-machine design in
ELDEN-RING-Combat-Rewrite/src/sim/{player,data,dummy}.rs into standalone C++17.
Keep LICENSE-MIT.txt with distributions containing this component. OpenMW's
overall GPL licensing continues to apply to the combined application.

The source recreation requires generated src/sim/extracted.rs, animations and
other assets produced from a local Elden Ring installation. Those generated
files are not committed with the recreation. Until the generated timing table
is imported, this component provides documented fallback action timings and
weapon profiles rather than claiming that its full action table, animation or
weapon geometry matches Elden Ring. scripts/import_eldenring_timings.py imports
the local generated table into imported_timings.hpp; CMake enables that table
when the generated header exists.
The importer also keeps each action's baked clip identifier and each weapon's
one-/two-handed stance categories. animations.cpp reads the local ERAN bank
from the recreation's tools/bake_anims.py, validates its layout and samples
the original 30-fps model-space joint positions and oriented joint axes.
The engine shares this immutable bank and retargets poses onto actor skeletons.
Roll/backstep costs, known roll distances/i-frame lengths, the normalized
stamina estimates, guard raise delay and counter window come from the supplied
code/tests. Light/heavy motion values and the default light chain are retained
where the supplied behavior tests specify them. Other timings and the root
displacement interpolation are OpenMW fallback estimates.

Implemented mechanisms:
  - Input windows, one-slot newest-input buffering and action cancel windows.
  - Light chains, charge/release heavy attacks and alternating heavy chains.
  - Running, roll, backstep, crouch, airborne and guard-counter openers.
  - Imported jump attacks transition to full/short landing recovery on ground
    contact, retaining prepaid stamina and preventing repeated airborne hits.
  - Left-hand and paired chains with independent multi-hit windows and costs.
  - Right- and left-hand two-handed grips, including the shield attack profile.
  - Release-to-roll/hold-to-sprint, load-dependent dodge displacement and i-frames.
  - Delayed frontal guarding, two-handed guarding, guard breaks and counters.
  - Per-hit stamina payments, out-of-combat free sprinting, exhaustion/rearming,
    reduced guard regeneration and hit-stop.
  - Ground hit reactions and the dummy's accumulated stance damage/recovery.

Critical vulnerability after stance/guard breaks and critical attacks are
OpenMW integration extensions. Parrying and weapon skills are absent from the
source recreation and are not implemented here. Physics, target selection,
actual weapon contact, health, equipment and animation are owned by OpenMW;
the component receives engine decisions instead of embedding a second world.

Adapter contract:
  1. Configure equipment/load/guard eligibility and setVitals(current,max).
     Costs and regeneration scale by max/100 so the native fatigue bar is used.
  2. Call update(input,seconds) once per simulation update and copy stamina()
     back to native fatigue. Clear Input edges after the update. Native fatigue
     regeneration/drain must be suppressed when this controller owns fatigue.
  3. Use Update action flags and definition() to drive animation.
     An imported definition's mAnimation names the ERAN clip. Sample it at
     frame()*mAnimationSpeed: imported weapon timings have been divided by
     weapon speed, and this factor restores the original authored frame.
     Jump-attack landing changes the definition and resets its frame without
     starting a new attack sequence. Sample the current definition each update
     so the airborne clip gives way to the full or short landing recovery.
     stanceAnimation(weapon,twoHanded,id) gives the extracted idle/guard/movement
     category. With no local imported table it selects the base category.
     mAdvancedSeconds is the portion of the update outside hit-stop; advance
     animations by this time and scale native velocity by its fraction of the
     world update. mFrozen means the entire update was paused. mJumpStarted
     retains an accepted jump even if its action ends during a long update;
     use it to launch native physics, without charging jump stamina again.
     mDisplacementX/Y are actor-local horizontal displacement in metres, to
     convert through the engine's world scale and collision-controlled motion.
     Imported mRootMotion samples are cumulative [left,up,forward] metres at
     30 fps. rootMotionAt() interpolates at frame*mRootMotionSpeed; horizontal
     actor coordinates negate left and retain forward. Canonical Front roll
     motion rotates toward the captured dodge direction. Backstep retains its
     authored signed motion. Vertical movement and airborne motion remain
     owned by OpenMW physics. Fallback definitions use the estimated curve.
  4. Query activeHit() for contact. A window crossed by a slow frame is retained
     until the next update, even if the action ended. markHit() consumes exactly
     one window; loop to handle multiple windows crossed by the same update.
     A missed live window can be queried again next update until it expires.
     attack() reports the current action's first-hit metadata for animation use;
     activeHit() supplies the actual hit's sequence, index and damage metadata.
  5. receiveHit() reports evade/block/guard break/reaction before engine health
     damage. Incoming stamina/stance damage is normalized; damage itself stays
     in native health units and is never applied by this component. The adapter
     checks target reach and sets mCriticalAvailable before requesting a critical.
     consumeCritical() reserves the target's vulnerability exactly once.
     Paired fallback hits alternate hands; imported hits may set mLeftHand.
     For a left-handed two-hand grip, configure mWeaponClass/mSpeedMultiplier
     from the selected left item and set both mTwoHanded and mTwoHandedLeft.
     Primary attacks and guard counters then report left-hand hit ownership.
  6. applyHitStop(seconds) freezes action/regen/movement after valid contact;
     setDead()/reset() synchronize death, resurrection and loaded-game changes.
     cancelInputActions() releases menus' input ownership: buffered requests,
     pending hit windows and current attacks are discarded without refunding
     stamina, restoring stance or consuming critical vulnerability. Ongoing
     defensive reactions and dodge/jump movement remain paused until resumed.

Native Morrowind armor shields and lights have no weapon damage ranges. Their
two-handed/left-hand attacks use native hand-to-hand health damage with the
imported shield/torch timing, stamina and motion-value profile. Native attack
values and contact geometry remain OpenMW adaptations. The baked animation
bank contains no replacement character mesh; the renderer uses the actor's
existing skeleton and equipment. Without usable bank/skeleton data, visual
playback falls back to the native clips.

All timing uses animation frames at 30 fps. update subdivides positive time
into at most 1/60-second slices and caps one update at one second to avoid huge
catch-up after pauses. Timers, costs and crossed-hit events use those slices.

Standalone checks (no OpenMW dependencies):
  c++ -std=c++17 -O2 -Wall -Wextra -Werror combat.cpp tests.cpp -o /tmp/eldencombat-tests
  /tmp/eldencombat-tests
  c++ -std=c++17 -O2 -Wall -Wextra -Werror animations.cpp animationtests.cpp -o /tmp/eldenanimation-tests
  /tmp/eldenanimation-tests [path/to/local/player_anims.bin]

The CMake eldencombat-imported-tests executable also accepts a local animation
bank path to check that every imported action and weapon idle/guard stance
references a clip present in that bank.
