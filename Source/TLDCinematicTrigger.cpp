// TLDCinematicTrigger.cpp â€” HEAVILY DEBUGGED
#include "UI/TLDCinematicTrigger.h"

// Engine includes
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

// Project includes
#include "UI/TLDCinematicManager.h"
#include "UI/TLDCinematicConfig.h"
#include "Utilities/TLDProjectSettings.h"

// Logging
DEFINE_LOG_CATEGORY_STATIC(LogTLDCinematicTrigger, Log, All);

// ===============================
// CONSTRUCTION
// ===============================

ATLDCinematicTrigger::ATLDCinematicTrigger()
{
    PrimaryActorTick.bCanEverTick = false;

    Box = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
    SetRootComponent(Box);

    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    Box->SetCollisionObjectType(ECC_WorldStatic);

    Box->OnComponentBeginOverlap.AddDynamic(this, &ATLDCinematicTrigger::OnBoxBeginOverlap);

    Box->SetBoxExtent(FVector(200.f, 200.f, 100.f));
}

// ===============================
// ACTOR LIFECYCLE
// ===============================

void ATLDCinematicTrigger::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTLDCinematicTrigger, Warning,
        TEXT("[%s] BeginPlay - CinematicName='%s'  OneShot=%d  Pause=%d  Skip=%d  Pre=%.2f  Post=%.2f"),
        *GetName(), *CinematicName, bOneShot, bPauseGame, bSkippable, PreDelay, PostDelay);

    if (Box)
    {
        UE_LOG(LogTLDCinematicTrigger, Warning,
            TEXT("[%s] BoxExtent=%s  GenOverlap=%d"),
            *GetName(), *Box->GetUnscaledBoxExtent().ToString(), Box->GetGenerateOverlapEvents());
    }

    // Resolve manager right away
    ResolveManager();

    // Validate the cinematic name against config
    ValidateCinematicName();

    // Handle case: player already inside trigger
    FTimerHandle Tmp;
    GetWorld()->GetTimerManager().SetTimer(Tmp, this, &ATLDCinematicTrigger::CheckInitialOverlapOnce, 0.25f, false);
}

// ===============================
// EDITOR FUNCTIONS
// ===============================

#if WITH_EDITOR
TArray<FString> ATLDCinematicTrigger::GetAvailableCinematics() const
{
    TArray<FString> Options;
    Options.Add(""); // empty option

    const UTLDProjectSettings* PS = UTLDProjectSettings::Get();
    if (!PS || PS->CinematicConfigAsset.IsNull())
    {
        UE_LOG(LogTLDCinematicTrigger, Warning, TEXT("[Editor] No CinematicConfigAsset set in ProjectSettings"));
        return Options;
    }

    if (UTLDCinematicConfig* Config = PS->CinematicConfigAsset.LoadSynchronous())
    {
        for (const FTLDCinematicEntry& Entry : Config->Cinematics)
        {
            Options.Add(Entry.CinematicName);
        }
    }
    return Options;
}
#endif

// ===============================
// OVERLAP EVENTS
// ===============================

void ATLDCinematicTrigger::OnBoxBeginOverlap(
    UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
    bool bFromSweep, const FHitResult& Sweep)
{
    UE_LOG(LogTLDCinematicTrigger, Warning,
        TEXT("[%s] OnBoxBeginOverlap fired by %s"), *GetName(),
        OtherActor ? *OtherActor->GetName() : TEXT("<null>"));

    if (bOneShot && bHasFired)
    {
        UE_LOG(LogTLDCinematicTrigger, Warning,
            TEXT("[%s] Skipped - already fired (one-shot)"), *GetName());
        return;
    }

    if (!IsValidInstigator(OtherActor))
    {
        UE_LOG(LogTLDCinematicTrigger, Warning,
            TEXT("[%s] Skipped - invalid instigator"), *GetName());
        return;
    }

    if (!ResolveManager())
    {
        UE_LOG(LogTLDCinematicTrigger, Error,
            TEXT("[%s] No CinematicManager available"), *GetName());
        return;
    }

    TriggerCinematic();

    if (bOneShot)
    {
        bHasFired = true;
    }
}

// ===============================
// INTERNAL HELPERS
// ===============================

UTLDCinematicManager* ATLDCinematicTrigger::ResolveManager()
{
    if (CachedManager) return CachedManager;

    if (UGameInstance* GI = GetGameInstance())
    {
        CachedManager = GI->GetSubsystem<UTLDCinematicManager>();
        if (CachedManager)
        {
            UE_LOG(LogTLDCinematicTrigger, Warning,
                TEXT("[%s] Resolved CinematicManager subsystem"), *GetName());
        }
        else
        {
            UE_LOG(LogTLDCinematicTrigger, Error,
                TEXT("[%s] Failed to resolve CinematicManager subsystem"), *GetName());
        }
    }
    else
    {
        UE_LOG(LogTLDCinematicTrigger, Error,
            TEXT("[%s] No GameInstance available to resolve manager"), *GetName());
    }

    return CachedManager;
}

bool ATLDCinematicTrigger::IsValidInstigator(AActor* OtherActor) const
{
    if (!OtherActor)
    {
        UE_LOG(LogTLDCinematicTrigger, Warning, TEXT("[%s] Null instigator"), *GetName());
        return false;
    }

    if (bOnlyPlayerPawn)
    {
        APawn* Pawn = Cast<APawn>(OtherActor);
        if (!Pawn)
        {
            UE_LOG(LogTLDCinematicTrigger, Warning,
                TEXT("[%s] Instigator %s is not a Pawn"), *GetName(), *OtherActor->GetName());
            return false;
        }
        if (!Pawn->IsPlayerControlled())
        {
            UE_LOG(LogTLDCinematicTrigger, Warning,
                TEXT("[%s] Instigator %s is a Pawn but NOT player controlled"), *GetName(), *OtherActor->GetName());
            return false;
        }
    }
    return true;
}

void ATLDCinematicTrigger::TriggerCinematic()
{
    if (!CachedManager)
    {
        UE_LOG(LogTLDCinematicTrigger, Error,
            TEXT("[%s] TriggerCinematic but no manager"), *GetName());
        return;
    }

    if (CinematicName.IsEmpty())
    {
        UE_LOG(LogTLDCinematicTrigger, Error,
            TEXT("[%s] CinematicName is empty. Please select from dropdown."), *GetName());
        return;
    }

    UE_LOG(LogTLDCinematicTrigger, Warning,
        TEXT("[%s] Requesting CinematicManager to play '%s'"), *GetName(), *CinematicName);

    const bool bSuccess = CachedManager->PlayCinematicByName(
        CinematicName, bPauseGame, bSkippable, PreDelay, PostDelay);

    if (bSuccess)
    {
        UE_LOG(LogTLDCinematicTrigger, Warning,
            TEXT("[%s] SUCCESS - Cinematic '%s' triggered"), *GetName(), *CinematicName);
    }
    else
    {
        UE_LOG(LogTLDCinematicTrigger, Error,
            TEXT("[%s] FAILED - Cinematic '%s' not found in config?"), *GetName(), *CinematicName);
    }
}

void ATLDCinematicTrigger::CheckInitialOverlapOnce()
{
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!PlayerPawn)
    {
        UE_LOG(LogTLDCinematicTrigger, Error,
            TEXT("[%s] CheckInitialOverlapOnce: No PlayerPawn"), *GetName());
        return;
    }

    if (Box && Box->IsOverlappingActor(PlayerPawn))
    {
        UE_LOG(LogTLDCinematicTrigger, Warning,
            TEXT("[%s] PlayerPawn is ALREADY inside trigger at BeginPlay. Auto-firing."), *GetName());
        OnBoxBeginOverlap(Box, PlayerPawn, nullptr, 0, false, FHitResult());
    }
    else
    {
        UE_LOG(LogTLDCinematicTrigger, Warning,
            TEXT("[%s] PlayerPawn NOT inside trigger at BeginPlay."), *GetName());
    }
}

void ATLDCinematicTrigger::ValidateCinematicName() const
{
    const UTLDProjectSettings* PS = UTLDProjectSettings::Get();
    if (!PS || PS->CinematicConfigAsset.IsNull())
    {
        UE_LOG(LogTLDCinematicTrigger, Error,
            TEXT("[%s] No CinematicConfigAsset set in ProjectSettings"), *GetName());
        return;
    }

    if (UTLDCinematicConfig* Config = PS->CinematicConfigAsset.LoadSynchronous())
    {
        const ULevelSequence* Seq = Config->GetSequenceByName(CinematicName);
        if (Seq)
        {
            UE_LOG(LogTLDCinematicTrigger, Warning,
                TEXT("[%s] CinematicName '%s' validated -> %s"),
                *GetName(), *CinematicName, *Seq->GetName());
        }
        else
        {
            UE_LOG(LogTLDCinematicTrigger, Error,
                TEXT("[%s] CinematicName '%s' NOT found in config. Re-select in editor."),
                *GetName(), *CinematicName);
        }
    }
}
