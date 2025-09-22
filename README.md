# TLD Cinematic System

A production-ready cinematic management system for Unreal Engine that eliminates complex Blueprint setups and streamlines designer workflows. Built for teams that need reliable, scalable cutscene integration.

## 🎬 One-Sentence Workflow
**Drop trigger → Set box → Fill package → Automatic playback**

No more complex Blueprint graphs, asset management headaches, or per-trigger custom logic.

## ✨ Key Features

- **Designer-First Interface:** Inline data packages eliminate external asset management
- **Name-Based Playback:** Reference cinematics by string names, not hard asset references  
- **Integrated Audio:** Built-in VO support with automatic music ducking
- **Smart State Management:** Proper pause/unpause handling with skip functionality
- **Production Ready:** Comprehensive error handling and debug integration
- **Blueprint Accessible:** Full designer control without touching code

## 🚀 Quick Start

### 1. Basic Setup (30 seconds)
```cpp
// 1. Add TLDCinematicTrigger to your level
// 2. Set box collision size
// 3. Assign your Level Sequence to Package.Sequence
// 4. Set Package.PauseGame = true
// 5. Done!
```

### 2. Advanced Usage
```cpp
// Play by name from anywhere in your code
GetCinematicManager()->PlayCinematicByName(
    "BossIntro",     // Registered in config
    true,            // Pause game
    true,            // Skippable
    1.0f,            // 1s pre-delay
    0.5f             // 0.5s post-delay
);
```

## 📁 System Components

| Component | Purpose | Designer Impact |
|-----------|---------|-----------------|
| **TLDCinematicManager** | Handles playback, state, timing | Blueprint functions for triggering |
| **TLDCinematicTrigger** | Level-placed volume triggers | Drag-drop setup, no Blueprint needed |
| **TLDCinematicPackage** | Data container for each cutscene | Inline editor properties |
| **TLDCinematicConfig** | Name-to-asset mapping | Centralized sequence management |

## 🎯 Why This System?

**Before:**
- Complex Blueprint trigger setups per cutscene
- Hard asset references creating circular dependencies
- Manual state management (pause/unpause/skip)
- Inconsistent behavior across team members
- Audio integration requires separate systems

**After:**
- Single actor drop-in for any cutscene
- Data-driven sequence management
- Automatic state handling with safety checks
- Consistent workflow across entire team
- Built-in VO support with ducking

## 🔧 Technical Highlights

- **Memory Efficient:** Lazy loading with soft references
- **Thread Safe:** Single-threaded design with proper cleanup
- **Scalable:** O(1) name lookup, linear memory scaling
- **Robust:** Graceful degradation with clear error feedback
- **Integrated:** Works seamlessly with Unreal's Level Sequence system

## 📖 Documentation

- **[Technical Overview](Documentation/technical-overview.md)** - Architecture and implementation details
- **[Designer Workflow](Documentation/designer-workflow.md)** - Step-by-step usage guide with best practices

## 🎮 Perfect For

- **Narrative-driven games** requiring frequent cutscenes
- **Large teams** needing consistent cinematic workflows  
- **Rapid prototyping** of story beats and player moments
- **Production environments** where reliability is critical

## ⚡ Performance

- **Runtime:** Minimal per-frame overhead, optimal state management
- **Memory:** Sequences loaded on-demand, automatic cleanup
- **Loading:** Smart asset streaming with config-driven optimization
- **Debug:** Comprehensive logging without performance impact

---

**Built for production workflows.** This system powers cinematic content in shipped titles, designed by technical designers who understand both code architecture and designer needs.

### 🏗️ Integration

Compatible with Unreal Engine 4.26+ and 5.x. Drop into existing projects with minimal setup - designed to complement, not replace, your current systems.

*Developed with ❤️ for the technical design community*