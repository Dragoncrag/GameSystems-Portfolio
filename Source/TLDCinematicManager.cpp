#include "UI/TLDCinematicManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "MovieSceneSequencePlayer.h"
#include "UI/TLDCinematicConfig.h"
#include "Utilities/TLDProjectSettings.h"
#include "Utilities/TLDPresentationDebugSystem.h"

void UTLDCinematicManager::PlaySequence(ULevelSequence* Sequence, bool bPauseGame, bool bSkippable, float PreDelay, float PostDelay)
{
	if (!Sequence || !GetWorld())
	{
		TLD_PRESENTATION_ARCHITECTURE(this, TEXT("Cinematic Manager"), 
			TEXT("Plays cutscenes â†’ Controls game pause â†’ Handles skipping"), 
			TEXT("System validation â†’ Prevents crashes"));
		return;
	}

	if (bIsPlaying)
	{
		TLD_PRESENTATION_DESIGNER(this, TEXT("Single Playback"), 
			TEXT("One cinematic at a time"), 
			TEXT("Prevents conflicts â†’ Clean experience"));
		return;
	}

	TLD_PRESENTATION_DESIGNER(this, TEXT("Cutscene Request"), 
		FString::Printf(TEXT("%s â†’ Pause:%s Skip:%s"), *Sequence->GetName(), 
			bPauseGame ? TEXT("Y") : TEXT("N"), bSkippable ? TEXT("Y") : TEXT("N")), 
		TEXT("Designer controls â†’ Blueprint callable"));

	PendingSequence = Sequence;
	bPendingPause = bPauseGame;
	bPendingSkippable = bSkippable;
	PendingPostDelay = FMath::Max(0.f, PostDelay);
	bIsPlaying = true;
	bAllowSkip = false;

	if (PreDelay > 0.f)
	{
		TLD_PRESENTATION_DESIGNER(this, TEXT("Timing Control"), 
			FString::Printf(TEXT("%.1fs delay â†’ smooth transitions"), PreDelay), 
			TEXT("No jarring cuts â†’ Professional polish"));
		GetWorld()->GetTimerManager().SetTimer(PreDelayHandle, this, &UTLDCinematicManager::StartSequence, PreDelay, false);
	}
	else
	{
		StartSequence();
	}
}

bool UTLDCinematicManager::PlayCinematicByName(const FString& CinematicName, bool bPauseGame, bool bSkippable, float PreDelay, float PostDelay)
{
	TLD_PRESENTATION_ARCHITECTURE(this, TEXT("Name-Based System"), 
		FString::Printf(TEXT("'%s' â†’ Config lookup â†’ Asset resolution"), *CinematicName), 
		TEXT("No asset references â†’ Just type name"));

	if (CinematicName.IsEmpty())
	{
		TLD_PRESENTATION_INTEGRATION(this, TEXT("Input Validation"), 
			TEXT("Empty name â†’ Clear error"), 
			TEXT("Safe failure â†’ Easy debugging"));
		return false;
	}

	const UTLDProjectSettings* ProjectSettings = UTLDProjectSettings::Get();
	if (!ProjectSettings || ProjectSettings->CinematicConfigAsset.IsNull())
	{
		TLD_PRESENTATION_INTEGRATION(this, TEXT("Config Missing"), 
			TEXT("Project Settings â†’ TLD â†’ Set Cinematic Config"), 
			TEXT("Centralized setup â†’ Team workflow"));
		return false;
	}

	UTLDCinematicConfig* Config = ProjectSettings->CinematicConfigAsset.LoadSynchronous();
	if (!Config)
	{
		TLD_PRESENTATION_TECHNICAL(this, TEXT("Asset Loading"), 
			TEXT("Config load failed â†’ Check asset"), 
			TEXT("Asset validation â†’ Error reporting"));
		return false;
	}

	ULevelSequence* Sequence = Config->GetSequenceByName(CinematicName);
	if (!Sequence)
	{
		TLD_PRESENTATION_DESIGNER(this, TEXT("Sequence Not Found"), 
			FString::Printf(TEXT("'%s' missing from config"), *CinematicName), 
			TEXT("Add to config â†’ Update list"));
		return false;
	}

	TLD_PRESENTATION_INTEGRATION(this, TEXT("System Communication"), 
		TEXT("Config â†’ Manager â†’ Playback"), 
		TEXT("String name â†’ Asset â†’ Play"));
	PlaySequence(Sequence, bPauseGame, bSkippable, PreDelay, PostDelay);
	return true;
}

void UTLDCinematicManager::StartSequence()
{
	if (!PendingSequence || !GetWorld())
	{
		TLD_PRESENTATION_INTEGRATION(this, TEXT("State Check"), 
			TEXT("Missing data â†’ Safe cleanup"), 
			TEXT("Defensive code â†’ System stability"));
		bIsPlaying = false;
		return;
	}

	TLD_PRESENTATION_TECHNICAL(this, TEXT("UE5 Player Creation"), 
		FString::Printf(TEXT("LevelSequencePlayer â†’ %s"), *PendingSequence->GetName()), 
		TEXT("Engine integration â†’ Input control"));

	FMovieSceneSequencePlaybackSettings Settings;
	Settings.bDisableLookAtInput = true;
	Settings.bDisableMovementInput = true;
	Settings.bHideHud = false;

	ActivePlayer = nullptr;
	ActiveActor = nullptr;

	ActivePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(GetWorld(), PendingSequence, Settings, ActiveActor);

	if (!ActivePlayer || !ActiveActor)
	{
		TLD_PRESENTATION_INTEGRATION(this, TEXT("Player Failed"), 
			TEXT("Engine creation failed â†’ Cleanup"), 
			TEXT("Graceful failure â†’ Error handling"));
		bIsPlaying = false;
		return;
	}

	TLD_PRESENTATION_ARCHITECTURE(this, TEXT("Systems Connected"), 
		TEXT("Player â†’ Input â†’ Pause â†’ Skip"), 
		TEXT("All systems talking â†’ Clean state"));

	ActivePlayer->OnFinished.AddDynamic(this, &UTLDCinematicManager::HandleSequenceFinished);
	ApplyPause(bPendingPause);
	bAllowSkip = bPendingSkippable;
	ActivePlayer->Play();
}

void UTLDCinematicManager::HandleSequenceFinished()
{
	TLD_PRESENTATION_TECHNICAL(this, TEXT("Cleanup Sequence"), 
		FString::Printf(TEXT("Cinematic ended â†’ %.1fs post-delay"), PendingPostDelay), 
		TEXT("Restore game state â†’ Clean transition"));

	if (!GetWorld())
	{
		bIsPlaying = false;
		return;
	}

	bAllowSkip = false;

	if (PendingPostDelay > 0.f)
	{
		GetWorld()->GetTimerManager().SetTimer(PostDelayHandle, [this]()
		{
			TLD_PRESENTATION_TECHNICAL(this, TEXT("Post-Delay Complete"), 
				TEXT("Timer â†’ Unpause â†’ Reset â†’ Ready"), 
				TEXT("System cleanup â†’ Next cinematic ready"));
			ApplyPause(false);
			ActivePlayer = nullptr;
			ActiveActor = nullptr;
			bIsPlaying = false;
		}, PendingPostDelay, false);
	}
	else
	{
		ApplyPause(false);
		ActivePlayer = nullptr;
		ActiveActor = nullptr;
		bIsPlaying = false;
	}
}

void UTLDCinematicManager::SkipCurrentCinematic()
{
	TLD_PRESENTATION_DESIGNER(this, TEXT("Skip Control"), 
		FString::Printf(TEXT("Skip allowed? %s"), bAllowSkip ? TEXT("Yes") : TEXT("No")), 
		TEXT("User input â†’ System check â†’ Action"));

	if (!bIsPlaying || !bAllowSkip || !ActivePlayer)
	{
		TLD_PRESENTATION_DESIGNER(this, TEXT("Skip Denied"), 
			TEXT("Not skippable â†’ Request ignored"), 
			TEXT("Designer control â†’ Protected sequences"));
		return;
	}

	TLD_PRESENTATION_INTEGRATION(this, TEXT("Skip Execute"), 
		TEXT("Stop player â†’ Trigger cleanup â†’ Restore game"), 
		TEXT("Immediate response â†’ Clean state"));
	ActivePlayer->Stop();
	HandleSequenceFinished();
}

void UTLDCinematicManager::ApplyPause(bool bPause)
{
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			TLD_PRESENTATION_INTEGRATION(this, TEXT("Game Pause"), 
				FString::Printf(TEXT("%s game"), bPause ? TEXT("Pausing") : TEXT("Resuming")), 
				TEXT("PlayerController â†’ Pause state â†’ Game flow"));
			PC->SetPause(bPause);
		}
		else
		{
			TLD_PRESENTATION_INTEGRATION(this, TEXT("Pause Failed"), 
				TEXT("No PlayerController â†’ Can't pause"), 
				TEXT("Missing controller â†’ Check setup"));
		}
	}
}
