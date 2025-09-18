// TLDLevelStreamingManager.cpp
// Designer Guide: This file manages ALL level streaming in your game
// Think of this as the "Level Loading Brain" - it decides what gets loaded, when, and how

#include "TLDLevelStreamingManager.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "TLDLevelConfig.h"
#include "TLDProjectSettings.h"
#include "Engine/DataTable.h"

/**
 * CONSTRUCTOR - Setting Up the Manager
 * WHAT: Creates the level streaming manager and enables it to run every frame
 * WHY: We need per-frame updates to handle smooth level transitions and timing
 * HOW: Enables "Tick" so the manager can check loading progress every frame
 * 
 * DESIGNER NOTE: This runs automatically when the manager is created in your level
 */
ATLDLevelStreamingManager::ATLDLevelStreamingManager()
{
	// Enable per-frame updates - required for smooth level transitions
	// Without this, levels would appear instantly with possible hitches
	PrimaryActorTick.bCanEverTick = true;
}

/**
 * BEGINPLAY - Auto-Loading Startup Levels
 * WHAT: Automatically loads levels that should be available from game start
 * WHY: Some levels (like UI backgrounds, core areas) need to be ready immediately
 * HOW: Reads from either a DataTable asset or a simple array you configure
 * 
 * DESIGNER WORKFLOW:
 * 1. Create a DataTable with FTLDStarterChunkRow entries
 * 2. Add rows for each level that should load at startup
 * 3. Set visibility and warm-up timing per level
 * 4. Assign the DataTable to this manager's "Starter Chunks Table" property
 */
void ATLDLevelStreamingManager::BeginPlay()
{
	Super::BeginPlay();

	// OPTION 1: DataTable-driven startup (RECOMMENDED for designers)
	// DataTable lets you configure startup levels in a spreadsheet-like interface
	// You can add/remove/modify startup levels without touching code
	if (StarterChunksTable.IsValid() || StarterChunksTable.ToSoftObjectPath().IsValid())
	{
		LoadStarterChunksFromTable();
	}
	else
	{
		// OPTION 2: Legacy array fallback (for backward compatibility)
		// Uses the simple array property on the manager component
		// Less flexible but simpler for small projects
		for (const FTLDStarterChunk& Starter : StarterChunks)
		{
			if (Starter.LevelName.IsNone())
			{
				continue; // Skip empty entries
			}
			LoadChunkAsync(Starter.LevelName, Starter.bVisibleOnLoad, Starter.WarmUpSeconds);
		}
	}
}

/**
 * TICK - The Frame-by-Frame Level Management
 * WHAT: Runs every frame to manage loading states and timing
 * WHY: Smooth transitions require precise timing and state management
 * HOW: Checks each tracked level's status and handles state changes
 * 
 * DESIGNER UNDERSTANDING:
 * This is like a traffic controller for your levels. Every frame it asks:
 * - "Is this level done loading? Move it to 'ready' status"
 * - "Is this level's warm-up timer done? Show it to the player"
 * - "Does anything need a state change this frame?"
 * 
 * The warm-up system prevents visual "popping" by loading levels hidden first
 */
void ATLDLevelStreamingManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Check every level we're currently tracking
	for (TPair<FName, FTLDChunkRecord>& Pair : ChunkRecords)
	{
		FTLDChunkRecord& Rec = Pair.Value;

		// STATE TRANSITION 1: Loading → LoadedHidden
		// WHAT: Detect when a level finishes loading from disk
		// WHY: We need to know when loading is complete to start warm-up timing
		// HOW: Ask Unreal if the level is loaded, update our tracking
		if (Rec.State == ETLDChunkState::Loading && Rec.Streaming && Rec.Streaming->IsLevelLoaded())
		{
			Rec.State = ETLDChunkState::LoadedHidden;
			// Level is now loaded but hidden - warm-up can begin
		}

		// STATE TRANSITION 2: LoadedHidden → Visible (with warm-up delay)
		// WHAT: Count down warm-up timer and show level when ready
		// WHY: Prevents jarring "pop-in" by giving levels time to settle
		// HOW: Reduce timer each frame, show level when timer hits zero
		// 
		// DESIGNER BENEFIT: Warm-up lets you load a level behind the scenes,
		// then reveal it smoothly after a short delay (like a fade-in)
		if (Rec.State == ETLDChunkState::LoadedHidden && Rec.WarmUpSeconds > 0.f)
		{
			Rec.WarmUpSeconds -= DeltaTime; // Count down
			if (Rec.WarmUpSeconds <= 0.f)
			{
				// Timer finished - make the level visible to players
				ApplyVisibility(Rec, true);
			}
		}
	}
}

/**
 * LOADCHUNKASYNC - The Main Level Loading Function
 * WHAT: Loads a specific level (sublevel) into memory
 * WHY: This is how triggers and other systems request level loading
 * HOW: Starts async loading, optionally with delayed visibility
 * 
 * DESIGNER USAGE:
 * - Called by streaming triggers when player enters an area
 * - Can be called from Blueprints for scripted loading
 * - Handles both immediate and delayed visibility
 * 
 * PARAMETERS EXPLAINED:
 * @param LevelName - The short name of your sublevel (like "Forest_Combat")
 * @param bMakeVisibleAfterLoad - true = show immediately, false = load but keep hidden
 * @param WarmUpSeconds - Delay before showing (0 = instant, 2.0 = 2-second delay)
 */
void ATLDLevelStreamingManager::LoadChunkAsync(FName LevelName, bool bMakeVisibleAfterLoad, float WarmUpSeconds)
{
	// Input validation - prevent crashes from invalid data
	if (LevelName.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[TLDStream] LoadChunkAsync: Invalid LevelName"));
		return;
	}

	// Get or create a tracking record for this level
	// This lets us monitor loading progress and state
	FTLDChunkRecord& Rec = GetOrAddRecord(LevelName);
	
	// Delegate to the internal loading logic
	ApplyLoad(Rec, bMakeVisibleAfterLoad, WarmUpSeconds);
}

/**
 * UNLOADCHUNKASYNC - Level Unloading Function
 * WHAT: Removes a level from memory completely
 * WHY: Free up memory and hide areas the player doesn't need
 * HOW: Tells Unreal to unload the level and resets our tracking
 * 
 * DESIGNER USAGE:
 * - Called by streaming triggers when player leaves an area
 * - Use for memory management in large worlds
 * - Called automatically by "unload on exit" trigger behavior
 */
void ATLDLevelStreamingManager::UnloadChunkAsync(FName LevelName)
{
	if (LevelName.IsNone())
	{
		return; // Nothing to unload
	}

	FTLDChunkRecord& Rec = GetOrAddRecord(LevelName);
	ApplyUnload(Rec); // Handle the actual unloading
}

/**
 * SETCHUNKVISIBLE - Show/Hide Levels Without Loading/Unloading
 * WHAT: Controls whether a loaded level is visible to the player
 * WHY: Allows dynamic reveal/hide without memory loading costs
 * HOW: Changes visibility state while keeping level in memory
 * 
 * DESIGNER SCENARIOS:
 * - Hide a level during a cutscene, show it after
 * - Toggle between day/night versions of the same area
 * - Create dramatic reveals by loading hidden, then showing
 */
void ATLDLevelStreamingManager::SetChunkVisible(FName LevelName, bool bVisible)
{
	if (LevelName.IsNone())
	{
		return;
	}

	FTLDChunkRecord& Rec = GetOrAddRecord(LevelName);
	ApplyVisibility(Rec, bVisible);
}

/**
 * ISCHUNKLOADED - Query Function: Is Level in Memory?
 * WHAT: Checks if a level is loaded (whether visible or not)
 * WHY: Lets other systems know if a level is ready for use
 * HOW: Checks our tracking first, then asks Unreal as backup
 * 
 * DESIGNER USAGE:
 * - Blueprint conditions: "If forest level is loaded, spawn enemies"
 * - Debugging: Check what levels are currently in memory
 * - Optimization: Don't load what's already loaded
 */
bool ATLDLevelStreamingManager::IsChunkLoaded(FName LevelName) const
{
	// Check our internal tracking first (most accurate)
	if (const FTLDChunkRecord* Rec = ChunkRecords.Find(LevelName))
	{
		return Rec->State == ETLDChunkState::LoadedHidden || Rec->State == ETLDChunkState::Visible;
	}
	
	// Fallback: Ask Unreal directly (for unmanaged levels)
	if (ULevelStreaming* SL = FindStreamingLevelByName(LevelName))
	{
		return SL->IsLevelLoaded();
	}
	return false; // Not found anywhere
}

/**
 * ISCHUNKVISIBLE - Query Function: Is Level Currently Shown?
 * WHAT: Checks if a level is visible to the player right now
 * WHY: Different from "loaded" - a level can be loaded but hidden
 * HOW: Checks for "Visible" state specifically
 * 
 * DESIGNER USAGE:
 * - Conditional logic: "If boss arena is visible, start boss music"
 * - UI feedback: Show/hide loading indicators
 * - Performance: Only run expensive logic for visible areas
 */
bool ATLDLevelStreamingManager::IsChunkVisible(FName LevelName) const
{
	// Check our precise state tracking
	if (const FTLDChunkRecord* Rec = ChunkRecords.Find(LevelName))
	{
		return Rec->State == ETLDChunkState::Visible;
	}

	// Fallback: Ask Unreal's streaming system directly
	if (ULevelStreaming* SL = FindStreamingLevelByName(LevelName))
	{
		return SL->IsLevelVisible();
	}
	return false;
}

/**
 * LOADLEVELGROUP - Load Multiple Levels from Config
 * WHAT: Loads all levels defined in a level group from your config asset
 * WHY: Allows designers to define related levels once, use them everywhere
 * HOW: Looks up the group in config, loads each level in that group
 * 
 * DESIGNER WORKFLOW:
 * 1. Define level groups in your TLDLevelConfig asset (like "Act1_Forest")
 * 2. Add multiple level assets to each group
 * 3. Streaming triggers can load entire groups with one selection
 * 4. Change group contents without touching individual triggers
 * 
 * EXAMPLE: "Act1_Forest" group might contain:
 * - Forest_Entrance
 * - Forest_Path  
 * - Forest_Clearing
 * - Forest_Cave
 * All load together when group is requested
 */
void ATLDLevelStreamingManager::LoadLevelGroup(const FString& GroupName, bool bMakeVisibleAfterLoad, float WarmUpSeconds)
{
	// Get project settings to access the level configuration
	const UTLDProjectSettings* ProjectSettings = UTLDProjectSettings::Get();
	if (!ProjectSettings || ProjectSettings->LevelConfigAsset.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LevelStreamingManager] No level config found for group: %s"), *GroupName);
		return;
	}

	// Load the configuration asset that contains level group definitions
	UTLDLevelConfig* Config = ProjectSettings->LevelConfigAsset.LoadSynchronous();
	if (!Config)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LevelStreamingManager] Failed to load config for group: %s"), *GroupName);
		return;
	}

	// Get all levels defined in this group
	TArray<TSoftObjectPtr<UWorld>> GroupLevels = Config->GetLevelsInGroup(GroupName);
	if (GroupLevels.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LevelStreamingManager] No levels found in group: %s"), *GroupName);
		return;
	}

	// Load each level in the group with the same settings
	for (const TSoftObjectPtr<UWorld>& LevelAsset : GroupLevels)
	{
		if (!LevelAsset.ToSoftObjectPath().IsValid()) continue;
		
		// Convert full asset path to short name that Unreal expects
		// Example: "/Game/Maps/Forest/Forest_01" becomes "Forest_01"
		const FString LongPkg = LevelAsset.GetLongPackageName();
		if (LongPkg.IsEmpty()) continue;
		
		FString Short;
		LongPkg.Split(TEXT("/"), nullptr, &Short, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		FName ShortName(*Short);
		
		// Load this individual level
		LoadChunkAsync(ShortName, bMakeVisibleAfterLoad, WarmUpSeconds);
	}

	UE_LOG(LogTemp, Log, TEXT("[LevelStreamingManager] Loaded %d levels from group: %s"), 
		GroupLevels.Num(), *GroupName);
}

/**
 * UNLOADLEVELGROUP - Unload Multiple Levels from Config
 * WHAT: Unloads all levels that were defined in a specific level group
 * WHY: Memory management - remove entire areas when no longer needed
 * HOW: Looks up group definition, unloads each level individually
 * 
 * DESIGNER USAGE:
 * - Player leaves an act/area, unload all related levels
 * - Triggered by "unload on exit" behavior on streaming triggers
 * - Memory cleanup when transitioning between major areas
 */
void ATLDLevelStreamingManager::UnloadLevelGroup(const FString& GroupName)
{
	// Same config loading as LoadLevelGroup
	const UTLDProjectSettings* ProjectSettings = UTLDProjectSettings::Get();
	if (!ProjectSettings || ProjectSettings->LevelConfigAsset.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LevelStreamingManager] No level config found for group: %s"), *GroupName);
		return;
	}

	UTLDLevelConfig* Config = ProjectSettings->LevelConfigAsset.LoadSynchronous();
	if (!Config)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LevelStreamingManager] Failed to load config for group: %s"), *GroupName);
		return;
	}

	// Get the same levels that were in the group
	TArray<TSoftObjectPtr<UWorld>> GroupLevels = Config->GetLevelsInGroup(GroupName);
	for (const TSoftObjectPtr<UWorld>& LevelAsset : GroupLevels)
	{
		if (!LevelAsset.ToSoftObjectPath().IsValid()) continue;
		
		// Convert to short name (same logic as loading)
		const FString LongPkg = LevelAsset.GetLongPackageName();
		if (LongPkg.IsEmpty()) continue;
		
		FString Short;
		LongPkg.Split(TEXT("/"), nullptr, &Short, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		FName ShortName(*Short);
		
		// Unload this individual level
		UnloadChunkAsync(ShortName);
	}

	UE_LOG(LogTemp, Log, TEXT("[LevelStreamingManager] Unloaded group: %s"), *GroupName);
}

// =====================================
// INTERNAL HELPER FUNCTIONS
// These handle the complex logic but designers don't call them directly
// =====================================

/**
 * FINDSTREAMINGLEVELBYNAME - Internal Helper: Find Level by Name
 * WHAT: Searches Unreal's level list to find a specific sublevel
 * WHY: We need the actual ULevelStreaming object to control loading
 * HOW: Uses multiple search methods for reliability
 * 
 * TECHNICAL DETAIL: Unreal can name levels differently than expected,
 * so we try multiple approaches to find the right one
 */
ULevelStreaming* ATLDLevelStreamingManager::FindStreamingLevelByName(FName LevelName) const
{
	if (!GetWorld())
	{
		return nullptr; // No world = no levels
	}

	// METHOD 1: Use Unreal's built-in helper (most reliable)
	if (ULevelStreaming* Found = UGameplayStatics::GetStreamingLevel(GetWorld(), LevelName))
	{
		return Found;
	}

	// METHOD 2: Manual search through all streaming levels
	// This handles edge cases where naming doesn't match expectations
	for (ULevelStreaming* SL : GetWorld()->GetStreamingLevels())
	{
		if (!SL) continue;

		// Compare using just the short name part
		const FString ShortName = FPackageName::GetShortName(SL->GetWorldAssetPackageFName());
		if (ShortName.Equals(LevelName.ToString(), ESearchCase::IgnoreCase))
		{
			return SL;
		}
	}
	return nullptr; // Not found
}

/**
 * GETORADDRRECORD - Internal Helper: Get/Create Level Tracking
 * WHAT: Ensures we have a tracking record for every level we manage
 * WHY: We need to track state, timing, and references for each level
 * HOW: Returns existing record or creates a new one with defaults
 * 
 * DESIGN PATTERN: This ensures we never lose track of a level's state
 */
FTLDChunkRecord& ATLDLevelStreamingManager::GetOrAddRecord(FName LevelName)
{
	// Return existing record if we're already tracking this level
	if (FTLDChunkRecord* Existing = ChunkRecords.Find(LevelName))
	{
		return *Existing;
	}

	// Create new tracking record with sensible defaults
	FTLDChunkRecord NewRec;
	NewRec.LevelName = LevelName;
	NewRec.State = ETLDChunkState::Unloaded;
	NewRec.Streaming = FindStreamingLevelByName(LevelName);  // Cache the streaming reference
	return ChunkRecords.Add(LevelName, NewRec);
}

/**
 * APPLYLOAD - Internal Implementation: The Actual Loading Logic
 * WHAT: Handles the complex logic of loading a level with proper state management
 * WHY: Loading has many edge cases (already loaded, timing, visibility)
 * HOW: Manages state transitions and communicates with Unreal's streaming system
 * 
 * COMPLEXITY HANDLED:
 * - What if level is already loaded?
 * - Should it be visible immediately or after a delay?
 * - How do we track the loading progress?
 * - What if the level doesn't exist?
 */
void ATLDLevelStreamingManager::ApplyLoad(FTLDChunkRecord& Rec, bool bMakeVisibleAfterLoad, float WarmUpSeconds)
{
	// Ensure we have a reference to Unreal's streaming level object
	if (!Rec.Streaming)
		Rec.Streaming = FindStreamingLevelByName(Rec.LevelName);

	// VALIDATION: Check that this level actually exists as a sublevel
	if (!Rec.Streaming)
	{
		UE_LOG(LogTemp, Error, TEXT("[TLDStream] '%s' is not a sublevel of this World (check Levels panel)."),
			*Rec.LevelName.ToString());
		return;
	}

	// CASE 1: Level is already loaded - just handle visibility
	if (Rec.Streaming->IsLevelLoaded())
	{
		// Set visibility based on warm-up requirements
		const bool bShowNow = bMakeVisibleAfterLoad && WarmUpSeconds <= 0.f;
		Rec.Streaming->SetShouldBeVisible(bShowNow);
		Rec.State = bShowNow ? ETLDChunkState::Visible : ETLDChunkState::LoadedHidden;
		Rec.WarmUpSeconds = (bMakeVisibleAfterLoad && WarmUpSeconds > 0.f) ? WarmUpSeconds : 0.f;
		return;
	}

	// CASE 2: Level needs to be loaded from disk
	// Start the async loading process
	Rec.Streaming->SetShouldBeLoaded(true);

	// OPTIMIZATION: If no warm-up delay, request visibility immediately
	// This prevents an extra frame of delay for instant-show levels
	const bool bRequestVisibleNow = (bMakeVisibleAfterLoad && WarmUpSeconds <= 0.f);
	Rec.Streaming->SetShouldBeVisible(bRequestVisibleNow);

	// Set initial state based on visibility requirements
	Rec.State = bRequestVisibleNow ? ETLDChunkState::Visible : ETLDChunkState::Loading;
	Rec.WarmUpSeconds = (bMakeVisibleAfterLoad ? FMath::Max(0.f, WarmUpSeconds) : 0.f);
}

/**
 * APPLYUNLOAD - Internal Implementation: The Actual Unloading Logic
 * WHAT: Removes a level from memory and cleans up all tracking
 * WHY: Proper cleanup prevents memory leaks and state corruption
 * HOW: Tells Unreal to hide then unload, resets our tracking state
 * 
 * UNLOAD PROCESS:
 * 1. Hide the level first (prevents visual glitches)
 * 2. Tell Unreal to unload from memory
 * 3. Reset our internal tracking to "Unloaded"
 */
void ATLDLevelStreamingManager::ApplyUnload(FTLDChunkRecord& Rec)
{
	// Get streaming reference if we don't have it cached
	if (!Rec.Streaming)
	{
		Rec.Streaming = FindStreamingLevelByName(Rec.LevelName);
		if (!Rec.Streaming)
		{
			// Level doesn't exist, just mark as unloaded
			Rec.State = ETLDChunkState::Unloaded;
			return;
		}
	}

	// UNLOAD SEQUENCE: Hide first, then unload (prevents visual glitches)
	Rec.Streaming->SetShouldBeVisible(false);  // Hide immediately
	Rec.Streaming->SetShouldBeLoaded(false);   // Remove from memory

	// Reset all tracking state
	Rec.State = ETLDChunkState::Unloaded;
	Rec.WarmUpSeconds = 0.f;
}

/**
 * APPLYVISIBILITY - Internal Implementation: Show/Hide Level Logic  
 * WHAT: Controls whether a level is visible without affecting loading state
 * WHY: Allows dynamic reveal/hide effects independent of memory loading
 * HOW: Manages the relationship between loading state and visibility
 * 
 * SMART BEHAVIOR:
 * - If level isn't loaded yet, start loading but keep hidden
 * - If level is loaded, change visibility immediately
 * - Always maintains proper state tracking
 */
void ATLDLevelStreamingManager::ApplyVisibility(FTLDChunkRecord& Rec, bool bVisible)
{
	// Ensure we have streaming object reference
	if (!Rec.Streaming)
	{
		Rec.Streaming = FindStreamingLevelByName(Rec.LevelName);
	}

	if (!Rec.Streaming)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TLDStream] SetChunkVisible: '%s' not found as a sublevel."), *Rec.LevelName.ToString());
		return;
	}

	// PREREQUISITE CHECK: Visibility requires loading first
	if (!Rec.Streaming->IsLevelLoaded())
	{
		// AUTO-LOAD: If trying to show an unloaded level, load it first
		Rec.Streaming->SetShouldBeLoaded(true);
		Rec.Streaming->SetShouldBeVisible(false);  // But keep hidden until loaded
		Rec.State = ETLDChunkState::Loading;
		return;
	}

	// MAIN CASE: Level is loaded, change visibility
	Rec.Streaming->SetShouldBeVisible(bVisible);
	Rec.State = bVisible ? ETLDChunkState::Visible : ETLDChunkState::LoadedHidden;
	Rec.WarmUpSeconds = 0.f;  // Clear any pending warm-up
}

/**
 * LOADSTARTERCHUNKSFROMTABLE - DataTable Integration
 * WHAT: Reads startup level configuration from a DataTable asset
 * WHY: Allows designers to configure startup levels without code changes
 * HOW: Loads the table, reads each row, applies the configuration
 * 
 * DESIGNER WORKFLOW:
 * 1. Create a DataTable based on FTLDStarterChunkRow structure
 * 2. Add rows for each level that should load at game start
 * 3. Configure per-level settings (visibility, timing)
 * 4. Assign the DataTable to the manager's "StarterChunksTable" property
 * 5. Levels will auto-load when the game starts
 * 
 * DATATABLE BENEFITS:
 * - Spreadsheet-like editing interface
 * - No code changes needed to modify startup levels
 * - Validation and error checking built-in
 * - Can be modified by non-programmers
 */
void ATLDLevelStreamingManager::LoadStarterChunksFromTable()
{
	// Load the DataTable asset (safe during initialization)
	UDataTable* Table = StarterChunksTable.LoadSynchronous();
	if (!Table)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TLDStream] StarterChunksTable not set or failed to load."));
		return;
	}

	// Extract all rows from the table
	TArray<FTLDStarterChunkRow*> Rows;
	Table->GetAllRows<FTLDStarterChunkRow>(TEXT("StarterLoad"), Rows);

	// Process each configured entry
	int32 LoadedCount = 0;
	for (const FTLDStarterChunkRow* Row : Rows)
	{
		if (!Row || Row->LevelName.IsNone())
			continue; // Skip invalid entries

		// Apply the configured loading behavior for this startup level
		LoadChunkAsync(Row->LevelName, Row->bVisibleOnLoad, Row->WarmUpSeconds);
		++LoadedCount;
	}

	UE_LOG(LogTemp, Log, TEXT("[TLDStream] Loaded %d starter chunks from DataTable '%s'"),
		LoadedCount, *Table->GetName());
}