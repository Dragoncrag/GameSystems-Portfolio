// TLDCinematicConfig.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LevelSequence.h"
#include "TLDCinematicConfig.generated.h"

USTRUCT(BlueprintType)
struct FTLDCinematicEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cinematic")
	FString CinematicName;  // "Act1_Intro", "BossFight_Victory", etc.

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cinematic", meta=(AllowedClasses="LevelSequence"))
	TSoftObjectPtr<ULevelSequence> Sequence;
};

UCLASS(BlueprintType)
class THELASTDROP_API UTLDCinematicConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cinematics")
	TArray<FTLDCinematicEntry> Cinematics;

	UFUNCTION(BlueprintPure, Category="Cinematics")
	ULevelSequence* GetSequenceByName(const FString& CinematicName) const;
};