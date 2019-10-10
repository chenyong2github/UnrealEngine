// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Sound/SoundNodeAssetReferencer.h"
#include "Sound/SoundNodeQualityLevel.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundCue.h"
#include "UObject/FrameworkObjectVersion.h"
#include "Interfaces/ITargetPlatform.h"
#include "AudioCompressionSettings.h"
#include "Sound/AudioSettings.h"

#define ASYNC_LOAD_RANDOMIZED_SOUNDS 1

bool USoundNodeAssetReferencer::ShouldHardReferenceAsset(const ITargetPlatform* TargetPlatform) const
{
	if (TargetPlatform)
	{
		if (const FPlatformAudioCookOverrides* CookOverrides = TargetPlatform->GetAudioCompressionSettings())
		{			
			// If the Quality nodes are cooked, everything is hard refs.
			if (CookOverrides->SoundCueCookQualityIndex != INDEX_NONE)
			{
				UE_LOG(LogAudio, Verbose, TEXT("HARD reffing '%s:%s', as we are cooking using quality '%s'"), 
					*GetNameSafe(GetOuter()),
					*GetName(),
					*GetDefault<UAudioSettings>()->FindQualityNameByIndex(CookOverrides->SoundCueCookQualityIndex)
				)
				return true;
			}
		}
	}

	bool bShouldHardReference = true;

	if (USoundCue* Cue = Cast<USoundCue>(GetOuter()))
	{
		TArray<USoundNodeQualityLevel*> QualityNodes;
		TArray<USoundNodeAssetReferencer*> WavePlayers;
		Cue->RecursiveFindNode(Cue->FirstNode, QualityNodes);

		for (USoundNodeQualityLevel* QualityNode : QualityNodes)
		{
			WavePlayers.Reset();
			Cue->RecursiveFindNode(QualityNode, WavePlayers);
			if (WavePlayers.Contains(this))
			{
				bShouldHardReference = false;
				break;
			}
		}

#if ASYNC_LOAD_RANDOMIZED_SOUNDS
		if (bShouldHardReference)
		{
			//Check for randomized sounds as well:
			TArray<USoundNodeRandom*> RandomNodes;
			Cue->RecursiveFindNode(Cue->FirstNode, RandomNodes);

			for (USoundNodeRandom* RandomNode : RandomNodes)
			{
				WavePlayers.Reset();
				Cue->RecursiveFindNode(RandomNode, WavePlayers);
				if (WavePlayers.Contains(this))
				{
					bShouldHardReference = false;
					break;
				}
			}
		}
#endif // ASYNC_LOAD_RANDOMIZED_SOUNDS

		
	}

	UE_LOG(LogAudio, Verbose, TEXT("%s reffing '%s:%s'."),
		bShouldHardReference ? TEXT("HARD") : TEXT("SOFT"),
		*GetNameSafe(GetOuter()),
		*GetName()
	);

	return bShouldHardReference;
}

#if WITH_EDITOR
void USoundNodeAssetReferencer::PostEditImport()
{
	Super::PostEditImport();

	LoadAsset();
}
#endif
