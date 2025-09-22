# Designer Workflow Guide - TLD Cinematic System

## Overview

The TLD Cinematic System eliminates complex Blueprint setups and asset management for cutscenes. Instead of building trigger logic from scratch, designers can focus on timing, pacing, and player experience.

## Quick Start Workflow

### 1. Setup (One-Time Per Project)
1. **Create Cinematic Config Asset:**
   - In Content Browser: Right-click → Miscellaneous → Data Asset
   - Choose `TLDCinematicConfig` as parent class
   - Name it `CinematicConfig` or similar

2. **Register in Project Settings:**
   - Edit → Project Settings → Game → TLD
   - Set "Cinematic Config Asset" to your config asset
   - Save project settings

3. **Add Sequences to Config:**
   - Open your config asset
   - Add entries to "Cinematic Entries" array
   - Set Name (what you'll reference) and Sequence (the actual Level Sequence asset)

### 2. Level Setup (Per Cutscene)

**Basic Trigger Setup:**
1. **Place Trigger:**
   - Drag `TLDCinematicTrigger` from Place Actors panel into level
   - Position where you want the cutscene to activate

2. **Configure Trigger Volume:**
   - Select the trigger actor
   - In Details panel, expand the Box Component
   - Set Box Extent to desired trigger size
   - Use viewport manipulation to position/scale

3. **Fill Cinematic Package:**
   ```
   Cinematic Package:
   ├── Sequence: [Your Level Sequence asset]
   ├── Pause Game: ✓ (for dramatic cutscenes)
   ├── Skippable: ✓ (player convenience)
   ├── Pre Delay: 0.5s (brief pause before starting)
   └── Post Delay: 1.0s (moment to breathe after)
   ```

**Advanced Configuration:**
- **Trigger Once:** Usually ✓ for story moments
- **Player Pawns Only:** ✓ to ignore NPCs/objects
- **Required Actor Tag:** Leave empty unless filtering specific characters

### 3. Voice-Over Integration

For cutscenes with dialogue:

```
VO Settings:
├── VO On Start: [Sound Cue for opening line]
├── VO On End: [Sound Cue for closing line]
├── Duck dB: -10 (how much to lower music)
├── Volume: 1.0
└── Pitch: 1.0
```

**Timing Tip:** Use Pre Delay to let opening VO finish before sequence starts.

## Common Workflow Patterns

### Story Beat Cutscenes
```
Configuration:
- Pause Game: ✓
- Skippable: ✓
- Trigger Once: ✓
- Pre Delay: 1.0s (let player settle)
- Post Delay: 0.5s (brief pause before gameplay)
```

### Environmental Reveals
```
Configuration:
- Pause Game: ✗ (keep player control)
- Skippable: ✗ (short, impactful)
- Trigger Once: ✓
- Pre Delay: 0.0s (immediate impact)
```

### Tutorial Moments
```
Configuration:
- Pause Game: ✓
- Skippable: ✗ (ensure player sees instruction)
- Trigger Once: ✓
- VO On Start: [Instruction audio]
```

### Recurring Atmosphere
```
Configuration:
- Pause Game: ✗
- Skippable: ✓
- Trigger Once: ✗ (can replay)
- Required Actor Tag: "Player" (prevent NPCs triggering)
```

## Name-Based Playback

For cinematics triggered by code/Blueprint rather than level triggers:

1. **Register in Config:**
   ```
   Name: "BossIntro"
   Sequence: [Your boss introduction sequence]
   ```

2. **Call from Blueprint:**
   ```
   Get Cinematic Manager → Play Cinematic By Name
   ├── Cinematic Name: "BossIntro"
   ├── Pause Game: true
   ├── Skippable: true
   ├── Pre Delay: 2.0
   └── Post Delay: 1.0
   ```

## Skip Functionality

Players can skip cinematics when `bSkippable = true`:

**Default Skip Input:** Escape key or gamepad Start button
**Custom Skip Logic:** Call `Skip Current Cinematic` from your input Blueprint

## Debugging & Testing

### In-Editor Testing
1. **PIE (Play In Editor):**
   - Walk through trigger volumes to test activation
   - Check console for any error messages
   - Verify pause/unpause behavior

2. **Debug Visualization:**
   - Box Component shows trigger area in editor viewport
   - Green = properly configured
   - Red = missing sequence or config issues

### Common Issues & Solutions

**"Cinematic not playing":**
- ✓ Check sequence asset is assigned
- ✓ Verify config asset is set in Project Settings
- ✓ Ensure trigger volume encompasses player path

**"Game doesn't pause during cutscene":**
- ✓ Set `Pause Game = true` in Cinematic Package
- ✓ Check no other systems are overriding time dilation

**"VO not playing":**
- ✓ Assign sound assets to VO On Start/End
- ✓ Verify audio manager integration
- ✓ Check volume settings aren't zero

**"Skip not working":**
- ✓ Set `Skippable = true`
- ✓ Test with Escape key during playback
- ✓ Implement custom skip input if needed

## Best Practices

### Performance
- **Sequence Optimization:** Keep sequences under 30 seconds for memory efficiency
- **Trigger Placement:** Use smallest box extents that reliably catch player movement
- **Asset References:** Prefer name-based over direct asset references for large projects

### Player Experience
- **Skip Availability:** Always allow skipping on repeat playthroughs
- **Clear Transitions:** Use Pre/Post delays to avoid jarring cuts
- **Audio Balance:** Test VO ducking levels across different audio setups
- **Save Integration:** Ensure cinematics work properly with save/load systems

### Team Workflow
- **Naming Convention:** Use consistent naming for sequences (e.g., "Level01_BossIntro")
- **Config Management:** Keep cinematic config in shared/version-controlled location
- **Documentation:** Comment complex trigger setups for other team members
- **Testing:** Always test cinematics in context of full level playthrough

## Integration with Other Systems

### Save/Load
Cinematics automatically handle save states:
- Triggers remember if they've fired when `Trigger Once = true`
- Manager resets properly on level transitions
- No additional save data required

### Multiplayer Considerations
Current system is single-player focused:
- Cinematics play locally only
- For multiplayer, extend manager with network replication
- Consider player synchronization for shared cutscenes

### Localization
VO assets automatically support Unreal's localization system:
- Use localized Sound Cues for VO On Start/End
- Sequence subtitles handled by Level Sequence system
- No additional setup required for multi-language support

This workflow enables rapid iteration on cinematic content while maintaining professional polish and robustness.