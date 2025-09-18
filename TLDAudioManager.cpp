#include "TLDAudioManager.h"

// Core audio includes
#include "Components/AudioComponent.h"

#include "TLDAudioCatalog.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundClass.h"
#include "TLDProjectSettings.h"
#include "TimerManager.h"


#include "GameplayTagsManager.h"
#include "TLDAudioCatalog.h"



USoundBase* UTLDAudioManager::ResolveMusicByTag(FGameplayTag Tag) const
{
    if (!Catalog)
    {
        UE_LOG(LogTemp, Warning, TEXT("AudioManager: No Catalog set; cannot resolve music tag %s"), *Tag.ToString());
        return nullptr;
    }

    if (USoundBase* const* Found = Catalog->MusicByTag.Find(Tag))
    {
        return *Found;
    }
    UE_LOG(LogTemp, Warning, TEXT("AudioManager: Music tag not found in catalog: %s"), *Tag.ToString());
    return nullptr;
}

USoundBase* UTLDAudioManager::ResolveMusicByPrefix(FGameplayTag Prefix, int32 PickIndex, bool bRandom) const
{
    if (!Catalog)
    {
        UE_LOG(LogTemp, Warning, TEXT("AudioManager: No Catalog set; cannot resolve music prefix %s"), *Prefix.ToString());
        return nullptr;
    }

    // Gather all children under the prefix from the tag manager
	FGameplayTagContainer TagContainer = UGameplayTagsManager::Get().RequestGameplayTagChildren(Prefix);
	TArray<FGameplayTag> Children;
	TagContainer.GetGameplayTagArray(Children);

    // Filter to only those that actually exist in the catalog
    TArray<USoundBase*> Candidates;
    Candidates.Reserve(Children.Num());
    for (const FGameplayTag& ChildTag : Children)
    {
        if (USoundBase* const* Track = Catalog->MusicByTag.Find(ChildTag))
        {
            Candidates.Add(*Track);
        }
    }

    if (Candidates.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("AudioManager: No catalog entries under prefix %s"), *Prefix.ToString());
        return nullptr;
    }

    if (PickIndex >= 0 && Candidates.IsValidIndex(PickIndex))
    {
        return Candidates[PickIndex];
    }

    if (bRandom)
    {
        int32 Idx = FMath::RandRange(0, Candidates.Num()-1);
        return Candidates[Idx];
    }

    // Default: first
    return Candidates[0];
}

void UTLDAudioManager::PlayMusicByTag(FGameplayTag MusicTag, float FadeTime)
{
	if (MusicTag == CurrentMusicTag)
	{
		// Same tag as current; skip to avoid unnecessary crossfade
		return;
	}

	USoundBase* Track = ResolveMusicByTag(MusicTag);
	if (!Track) return;

	EnsureMusicComponents();

	if (!ActiveMusic->IsPlaying() && !InactiveMusic->IsPlaying())
	{
		ActiveMusic->SetSound(Track);
		ActiveMusic->FadeIn(FadeTime, 1.f);
	}
	else
	{
		CrossfadeTo(Track, /*Out*/FadeTime, /*In*/FadeTime, /*Vol*/1.f);
	}

	// Update tracker
	CurrentMusicTag = MusicTag;
}

void UTLDAudioManager::PlayMusicByPrefix(FGameplayTag MusicPrefix, float FadeTime, int32 PickIndex, bool bRandom)
{
    USoundBase* Track = ResolveMusicByPrefix(MusicPrefix, PickIndex, bRandom);
    if (!Track) return;

    EnsureMusicComponents();

    if (!ActiveMusic->IsPlaying() && !InactiveMusic->IsPlaying())
    {
        ActiveMusic->SetSound(Track);
        ActiveMusic->FadeIn(FadeTime, 1.f);
        return;
    }

    CrossfadeTo(Track, /*Out*/FadeTime, /*In*/FadeTime, /*Vol*/1.f);
}





/**
 * Big picture of Initialize():
 * - Build a small pool of AudioComponents for SFX (so PlaySFX is cheap).
 * - Create the music AudioComponent.
 * - Optionally push a default SoundMix (volume/EQ snapshot).
 */
void UTLDAudioManager::Initialize(FSubsystemCollectionBase& Collection)
{
    UE_LOG(LogTemp, Warning, TEXT("AudioManager: Initialize() starting"));
    
    // Try to load catalog from project settings first
	// Load catalog from Project Settings
	if (const UTLDProjectSettings* ProjectSettings = UTLDProjectSettings::Get())
	{
		Catalog = ProjectSettings->AudioCatalog.LoadSynchronous();
		if (Catalog)
		{
			UE_LOG(LogTemp, Warning, TEXT("AudioManager: Catalog loaded from Project Settings"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("AudioManager: No AudioCatalog configured in Project Settings"));
		}
	}

	if (!Catalog)
	{
		UE_LOG(LogTemp, Error, TEXT("AudioManager: Failed to initialize - no audio catalog"));
		return;
	}
    // Reserve memory to avoid reallocations
    SFXPool.Reserve(16);

    // Create 16 reusable audio components for SFX
    for (int32 i = 0; i < 16; ++i)
    {
        UAudioComponent* AC = NewObject<UAudioComponent>(GetTransientPackage());
        AC->bAutoActivate = false;
        AC->bAutoDestroy  = false;
        AC->RegisterComponentWithWorld(GetWorld());
        SFXPool.Add(AC);
    }

    // Create/prepare our music component
    EnsureMusicComponent();

    // Push a default mix if you want a consistent global sound from the start
    if (DefaultMix)
    {
        UGameplayStatics::PushSoundMixModifier(GetWorld(), DefaultMix);
    }
    
    UE_LOG(LogTemp, Warning, TEXT("AudioManager: Initialize() completed"));
}

/**
 * Catalog lookup with validation for tag-based audio asset resolution
 * Returns nullptr for invalid tags to prevent null pointer crashes
 */
USoundBase* UTLDAudioManager::GetSFXByTag(FGameplayTag Tag) const
{
	// Validate catalog reference before lookup
	if (!Catalog) 
	{
		UE_LOG(LogTemp, Warning, TEXT("AudioManager: No catalog assigned for tag lookup"));
		return nullptr;
	}

	// Fast hash table lookup using gameplay tag as key
	if (USoundBase* const* Found = Catalog->SFXByTag.Find(Tag))
	{
		return *Found;
	}

	// Tag not found in catalog
	UE_LOG(LogTemp, Warning, TEXT("AudioManager: Tag '%s' not found in catalog"), *Tag.ToString());
	return nullptr;
}

/**
 * High-level interface: Tag resolution + pooled playback in single call
 * Leverages existing pooled audio system for performance optimization
 */
void UTLDAudioManager::PlaySFXByTag(FGameplayTag Tag, FVector Location, float Volume, float Pitch)
{
	// Resolve tag to asset using catalog
	if (USoundBase* Sound = GetSFXByTag(Tag))
	{
		// Use existing pooled playback system for performance
		PlaySFXAtLocation(Sound, Location, Volume, Pitch);
	}
	// Error logging handled in GetSFXByTag for cleaner call sites
}



/**
 * Deinitialize():
 * - Stop music and any playing SFX.
 * - Clear the pool.
 */
void UTLDAudioManager::Deinitialize()
{
	if (MusicAC)
	{
		MusicAC->Stop();
	}

	for (UAudioComponent* AC : SFXPool)
	{
		if (AC) { AC->Stop(); }
	}
	SFXPool.Empty();
}


/** If the music component does not exist yet, create and register it. */
void UTLDAudioManager::EnsureMusicComponent()
{
	if (!MusicAC)
	{
		MusicAC = NewObject<UAudioComponent>(GetTransientPackage());
		MusicAC->bAutoActivate = false;  // we’ll call FadeIn/FadeOut
		MusicAC->bAutoDestroy  = false;
		MusicAC->RegisterComponentWithWorld(GetWorld());

		// Force this component to use the MusicClass so ducking targets music only.
		if (MusicClass)
		{
			MusicAC->SoundClassOverride = MusicClass;
		}
	}
}

/** Grab the next pooled SFX component and advance the round-robin index. */
UAudioComponent* UTLDAudioManager::GetPooledSFX()
{
	if (SFXPool.Num() == 0) return nullptr;

	UAudioComponent* AC = SFXPool[PoolIndex];
	PoolIndex = (PoolIndex + 1) % SFXPool.Num(); // wrap-around
	return AC;
}

/**
 * PlaySFXAtLocation:
 * - Finds a pooled component
 * - Moves it to the world location
 * - Sets sound/volume/pitch
 * - Plays it
 */
void UTLDAudioManager::PlaySFXAtLocation(USoundBase* Sound, FVector Location, float Volume, float Pitch)
{
	if (!Sound) return;

	if (UAudioComponent* AC = GetPooledSFX())
	{
		AC->SetWorldLocation(Location);
		AC->SetSound(Sound);
		AC->SetVolumeMultiplier(Volume);
		AC->SetPitchMultiplier(Pitch);
		AC->Play();
	}
}

/**
 * PlaySFXAttached:
 * - Same idea as above, but attaches the component to a parent (e.g. a weapon socket).
 */
void UTLDAudioManager::PlaySFXAttached(USoundBase* Sound, USceneComponent* Parent, FName SocketName, float Volume, float Pitch)
{
	if (!Sound || !Parent) return;

	if (UAudioComponent* AC = GetPooledSFX())
	{
		AC->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform, SocketName);
		AC->SetRelativeLocation(FVector::ZeroVector);
		AC->SetSound(Sound);
		AC->SetVolumeMultiplier(Volume);
		AC->SetPitchMultiplier(Pitch);
		AC->Play();
	}
}

/**
 * PlayMusicState:
 * - If we’re already in this state, do nothing.
 * - Otherwise, fade out current track and fade in the new one.
 * - Music tracks come from the MusicByState map (set in editor).
 */
void UTLDAudioManager::PlayMusicState(EAudioState NewState, float FadeTime)
{
	if (CurrentState == NewState)
	{
		return; // avoid restarting the same music
	}

	CurrentState = NewState;
	EnsureMusicComponent();

	if (USoundBase** Found = MusicByState.Find(NewState))
	{
		USoundBase* NewTrack = *Found;

		// If something is playing, fade it out first to avoid clicks/pops
		if (MusicAC->IsPlaying())
		{
			MusicAC->FadeOut(FadeTime, 0.f);
		}

		// Set the new track and fade it in
		MusicAC->SetSound(NewTrack);
		MusicAC->FadeIn(FadeTime, 1.f);
	}
}

/**
 * PlayVoiceLog:
 * - Plays a voice sound near the player.
 * - Ducks the music by a specified dB amount while the voice plays.
 * - When the voice ends, we restore music volume (via a timer).
 */
void UTLDAudioManager::PlayVoiceLog(USoundBase* Voice, float DuckDb)
{
	if (!Voice) return;

	// Convert dB to linear gain: linear = 10^(dB/20)
	const float DuckFactor = FMath::Pow(10.f, DuckDb / 20.f);

	// Lower music volume over a short fade
	if (MusicAC)
	{
		MusicAC->AdjustVolume(0.25f, DuckFactor);
	}

	// Find a good location for VO (player’s location works well for non-spatial voice)
	FVector ListenerLoc = FVector::ZeroVector;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (APawn* P = PC->GetPawn())
		{
			ListenerLoc = P->GetActorLocation();
		}
	}

	// Play the voice using a pooled SFX component
	if (UAudioComponent* AC = GetPooledSFX())
	{
		AC->SetWorldLocation(ListenerLoc);
		AC->SetSound(Voice);
		AC->Play();

		// Schedule restoring music after voice duration
		const float Duration = Voice->GetDuration();
		GetWorld()->GetTimerManager().ClearTimer(DuckTimer);
		GetWorld()->GetTimerManager().SetTimer(
			DuckTimer, this, &UTLDAudioManager::RestoreAfterVO, Duration + 0.1f, false
		);
	}
}

/** Called after a voice line finishes to bring music back to normal. */
void UTLDAudioManager::RestoreAfterVO()
{
	if (MusicAC)
	{
		MusicAC->AdjustVolume(0.25f, 1.f); // back to full volume
	}
}

/**
 * SetMixSnapshot:
 * - Push a SoundMix (like a preset for volumes/EQs/routings).
 * - Pushing the same mix multiple times is okay; Unreal handles stacking.
 * - To “undo”, you can PopSoundMixModifier in your own logic if needed.
 */
void UTLDAudioManager::SetMixSnapshot(USoundMix* Mix, float /*FadeTime*/)
{
	if (!Mix) return;
	UGameplayStatics::PushSoundMixModifier(GetWorld(), Mix);
}

/**
 * SetGlobalScalarParam:
 * - Placeholder for sending a float parameter to your audio system (MetaSounds/Modulation).
 * - Hook this into your system of choice later.
 */
void UTLDAudioManager::SetGlobalScalarParam(FName /*Name*/, float /*Value*/)
{
	// Example if using Audio Modulation:
	// UAudioModulationStatics::SetGlobalBusMixValue(GetWorld(), Name, Value);
}
void UTLDAudioManager::EnsureMusicComponents()
{
	if (!ActiveMusic)
	{
		ActiveMusic = NewObject<UAudioComponent>(GetTransientPackage());
		ActiveMusic->bAutoActivate = false;
		ActiveMusic->bAutoDestroy  = false;
		ActiveMusic->RegisterComponentWithWorld(GetWorld());
		if (MusicClass) ActiveMusic->SoundClassOverride = MusicClass;
	}

	if (!InactiveMusic)
	{
		InactiveMusic = NewObject<UAudioComponent>(GetTransientPackage());
		InactiveMusic->bAutoActivate = false;
		InactiveMusic->bAutoDestroy  = false;
		InactiveMusic->RegisterComponentWithWorld(GetWorld());
		if (MusicClass) InactiveMusic->SoundClassOverride = MusicClass;
	}
}

void UTLDAudioManager::CrossfadeTo(USoundBase* NewTrack, float FadeOutTime, float FadeInTime, float TargetVolume)
{
	if (!NewTrack) return;
	EnsureMusicComponents();

	// Swap roles: active → fading out, inactive → fading in
	UAudioComponent* Old = ActiveMusic;
	UAudioComponent* New = InactiveMusic;

	if (Old && Old->IsPlaying())
	{
		Old->FadeOut(FadeOutTime, 0.f);
	}

	if (New)
	{
		New->SetSound(NewTrack);
		New->FadeIn(FadeInTime, TargetVolume);
	}

	// Swap pointers for next time
	ActiveMusic = New;
	InactiveMusic = Old;
}