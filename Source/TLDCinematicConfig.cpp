// TLDCinematicConfig.cpp
#include "UI/TLDCinematicConfig.h"

ULevelSequence* UTLDCinematicConfig::GetSequenceByName(const FString& CinematicName) const
{
	for (const FTLDCinematicEntry& Entry : Cinematics)
	{
		if (Entry.CinematicName == CinematicName && Entry.Sequence.ToSoftObjectPath().IsValid())
		{
			return Entry.Sequence.LoadSynchronous();
		}
	}
	return nullptr;
}
