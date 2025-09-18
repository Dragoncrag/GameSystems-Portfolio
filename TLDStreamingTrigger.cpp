// Fill out your copyright...

#include "TLDStreamingTrigger.h"

// Engine
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

// Your manager
#include "TLDLevelStreamingManager.h"

// Config support
#include "TLDLevelConfig.h"
#include "TLDProjectSettings.h"

/*
 * Local helper only in this .cpp (anonymous namespace prevents symbol conflicts)
 * Converts a level asset soft path into the short package name expected by your manager.
 * Example: "/Game/Maps/Sub/Forest_01" -> "Forest_01"
 */
namespace
{
	static FName AssetToShortLevelName(const TSoftObjectPtr<UWorld>& LevelAsset)
	{
		// Not set / invalid soft pointer → return None so callers can skip
		if (!LevelAsset.ToSoftObjectPath().IsValid())
		{
			return NAME_None;
		}

		// GetLongPackageName() returns "/Game/Maps/Sub/Forest_01"
		const FString LongPkg = LevelAsset.GetLongPackageName();
		if (LongPkg.IsEmpty())
		{
			return NAME_None;
		}

		// Extract the last segment after '/'
		FString Short;
		LongPkg.Split(TEXT("/"), nullptr, &Short, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		return FName(*Short);
	}
}

// ===============================
// Construction
// ===============================
ATLDStreamingTrigger::ATLDStreamingTrigger()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create and set the trigger volume as root so transform affects the box
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);

	// Default: overlap pawns only (designers can tweak if needed)
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// Bind overlap events
	Box->OnComponentBeginOverlap.AddDynamic(this, &ATLDStreamingTrigger::OnBoxBeginOverlap);
	Box->OnComponentEndOverlap.AddDynamic(this, &ATLDStreamingTrigger::OnBoxEndOverlap);

	// Friendly default size
	Box->InitBoxExtent(FVector(300.f, 300.f, 200.f));
}

// ===============================
// BeginPlay
// ===============================
void ATLDStreamingTrigger::BeginPlay()
{
	Super::BeginPlay();

	// Try to resolve the manager now
	ResolveManager();

#if WITH_EDITOR
	if (GIsEditor && !IsRunningGame())
	{
		if (!CachedManager)
		{
			UE_LOG(LogTemp, Display, TEXT("[StreamingTrigger] No manager resolved (will auto-find at runtime)."));
		}
	}
#endif

	// Delay to ensure the player pawn is spawned and collisions are settled
	FTimerHandle TempHandle;
	GetWorld()->GetTimerManager().SetTimer(TempHandle, this, &ATLDStreamingTrigger::CheckInitialOverlapOnce, 0.1f, false);
}

void ATLDStreamingTrigger::CheckInitialOverlapOnce()
{
	UE_LOG(LogTemp, Log, TEXT("[StreamingTrigger] Running CheckInitialOverlapOnce()"));

	if (bHasFired && bOneShot)
	{
		UE_LOG(LogTemp, Log, TEXT("[StreamingTrigger] Skipped because already fired."));
		return;
	}

	if (!ResolveManager())
	{
		UE_LOG(LogTemp, Error, TEXT("[StreamingTrigger] Failed to resolve manager in CheckInitialOverlapOnce."));
		return;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[StreamingTrigger] PlayerPawn is NULL in CheckInitialOverlapOnce."));
		return;
	}

	if (Box->IsOverlappingActor(PlayerPawn))
	{
		UE_LOG(LogTemp, Log, TEXT("[StreamingTrigger] Player is inside the trigger box at CheckInitialOverlapOnce."));
		OnBoxBeginOverlap(Box, PlayerPawn, nullptr, 0, false, FHitResult());
	}
}

#if WITH_EDITOR
TArray<FString> ATLDStreamingTrigger::GetAvailableLevelGroups() const
{
    TArray<FString> Options;
    Options.Add(""); // Empty option to clear selection
    
    const UTLDProjectSettings* ProjectSettings = UTLDProjectSettings::Get();
    if (!ProjectSettings || ProjectSettings->LevelConfigAsset.IsNull())
    {
        return Options;
    }
    
    if (UTLDLevelConfig* Config = ProjectSettings->LevelConfigAsset.LoadSynchronous())
    {
        for (const FTLDLevelGroup& Group : Config->LevelGroups)
        {
            if (!Group.GroupName.IsEmpty())
            {
                Options.Add(Group.GroupName);
            }
        }
    }
    
    return Options;
}
#endif

// ===============================
// Overlap: Begin
// ===============================
void ATLDStreamingTrigger::OnBoxBeginOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*Sweep*/)
{
	if (bOnlyPlayerPawn)
	{
		APawn* Pawn = Cast<APawn>(OtherActor);
		if (!Pawn || !Pawn->IsPlayerControlled())
		{
			return; // ignore AI, physics actors, etc.
		}
	}

	if (bOneShot && bHasFired)
	{
		return;
	}

	if (!ResolveManager())
	{
	//	UE_LOG(LogTemp, Warning, TEXT("[StreamingTrigger] No TLDLevelStreamingManager found. Cannot stream levels."));
		return;
	}

	LoadConfiguredLevels();

	if (bOneShot)
	{
		bHasFired = true;
	}
}

// ===============================
// Overlap: End
// ===============================
void ATLDStreamingTrigger::OnBoxEndOverlap(
	UPrimitiveComponent* OverlappedComp, 
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp, 
	int32 OtherBodyIndex)
{
	if (bOnlyPlayerPawn)
	{
		APawn* Pawn = Cast<APawn>(OtherActor);
		if (!Pawn || !Pawn->IsPlayerControlled())
		{
	//		UE_LOG(LogTemp, Log, TEXT("[StreamingTrigger] Ignoring non-player actor exit"));
			return; // ignore AI or non-player actors
		}
	}

	//UE_LOG(LogTemp, Warning, TEXT("[StreamingTrigger] Player exited trigger. bUnloadOnExit=%s, LoadedByThisTrigger.Num()=%d"), 
	//	bUnloadOnExit ? TEXT("true") : TEXT("false"), LoadedByThisTrigger.Num());

	if (bUnloadOnExit)
	{
		if (ATLDLevelStreamingManager* Mgr = ResolveManager())
		{
	//		UE_LOG(LogTemp, Warning, TEXT("[StreamingTrigger] Manager found, unloading %d levels"), LoadedByThisTrigger.Num());
			
			for (const FName& LevelName : LoadedByThisTrigger)
			{
	//			UE_LOG(LogTemp, Warning, TEXT("[StreamingTrigger] Unloading level: %s"), *LevelName.ToString());
				Mgr->UnloadChunkAsync(LevelName);
			}
		}
		else
		{
	//		UE_LOG(LogTemp, Error, TEXT("[StreamingTrigger] No manager found for unload!"));
		}

		LoadedByThisTrigger.Empty();
		bHasFired = false; // optional if you want re-triggering
	}
}

// ===============================
// Manager resolve
// ===============================
ATLDLevelStreamingManager* ATLDStreamingTrigger::ResolveManager()
{
	// 1) Use explicit override if designer set one
	if (!CachedManager && ManagerOverride.IsValid())
	{
		CachedManager = ManagerOverride.Get();
	}

	// 2) Otherwise find the first instance in the world (assumes one manager)
	if (!CachedManager)
	{
		AActor* Found = UGameplayStatics::GetActorOfClass(GetWorld(), ATLDLevelStreamingManager::StaticClass());
		CachedManager = Cast<ATLDLevelStreamingManager>(Found);
	}

	return CachedManager;
}

// ===============================
// Load helpers
// ===============================
void ATLDStreamingTrigger::LoadConfiguredLevels()
{
    LoadedByThisTrigger.Reset();

    if (LevelGroupName.IsEmpty())
    {
 //       UE_LOG(LogTemp, Warning, TEXT("[StreamingTrigger] No level group selected"));
        return;
    }

    const UTLDProjectSettings* ProjectSettings = UTLDProjectSettings::Get();
    if (!ProjectSettings || ProjectSettings->LevelConfigAsset.IsNull())
    {
    //    UE_LOG(LogTemp, Warning, TEXT("[StreamingTrigger] No level config asset found"));
        return;
    }

    if (UTLDLevelConfig* Config = ProjectSettings->LevelConfigAsset.LoadSynchronous())
    {
        TArray<TSoftObjectPtr<UWorld>> GroupLevels = Config->GetLevelsInGroup(LevelGroupName);
        for (const TSoftObjectPtr<UWorld>& LevelAsset : GroupLevels)
        {
            const FName Short = AssetToShortLevelName(LevelAsset);
            if (Short.IsNone()) continue;
            
       //     UE_LOG(LogTemp, Log, TEXT("[StreamingTrigger] Loading level: %s"), *Short.ToString());
            CachedManager->LoadChunkAsync(Short, bVisibleAfterLoad, WarmUpSeconds);
            LoadedByThisTrigger.AddUnique(Short);
        }
        
     //   UE_LOG(LogTemp, Log, TEXT("[StreamingTrigger] Loaded %d levels from group: %s. LoadedByThisTrigger now has %d entries"), 
     //       GroupLevels.Num(), *LevelGroupName, LoadedByThisTrigger.Num());
    }
}