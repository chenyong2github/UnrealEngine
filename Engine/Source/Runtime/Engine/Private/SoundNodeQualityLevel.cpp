// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Sound/SoundNodeQualityLevel.h"
#include "EngineGlobals.h"
#include "ActiveSound.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundCue.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"

#if WITH_EDITORONLY_DATA
#include "Settings/LevelEditorPlaySettings.h"
#include "Editor.h"
#endif
#include "Interfaces/ITargetPlatform.h"

#if WITH_EDITOR

void USoundNodeQualityLevel::ReconcileNode(bool bReconstructNode)
{
	while (ChildNodes.Num() > GetMinChildNodes())
	{
		RemoveChildNode(ChildNodes.Num()-1);
	}
	while (ChildNodes.Num() < GetMinChildNodes())
	{
		InsertChildNode(ChildNodes.Num());
	}
#if WITH_EDITORONLY_DATA
	if (GIsEditor && bReconstructNode && GraphNode)
	{
		GraphNode->ReconstructNode();
		GraphNode->GetGraph()->NotifyGraphChanged();
	}
#endif
}

FText USoundNodeQualityLevel::GetInputPinName(int32 PinIndex) const
{
	return GetDefault<UAudioSettings>()->GetQualityLevelSettings(PinIndex).DisplayName;
}
#endif

void USoundNodeQualityLevel::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITOR
	ReconcileNode(false);
#endif //WITH_EDITOR

	UE_CLOG(Cast<USoundCue>(GetOuter()) && Cast<USoundCue>(GetOuter())->GetCookedQualityIndex() != CookedQualityLevelIndex && CookedQualityLevelIndex != INDEX_NONE,
		LogAudio,
		Warning, TEXT("'%s' has been cooked with multiple quality levels. '%s'(%d) vs '%s'(%d)"),
		*GetFullNameSafe(this),
		*GetDefault<UAudioSettings>()->FindQualityNameByIndex(USoundCue::GetCachedQualityLevel()),
		USoundCue::GetCachedQualityLevel(),
		*GetDefault<UAudioSettings>()->FindQualityNameByIndex(CookedQualityLevelIndex),
		CookedQualityLevelIndex
	);
}

void USoundNodeQualityLevel::PrimeChildWavePlayers(bool bRecurse)
{
	// If we're able to retrieve a valid cached quality level for this sound cue,
	// only prime that quality level.
	int32 QualityLevel = USoundCue::GetCachedQualityLevel();

#if WITH_EDITOR
	if (GIsEditor && QualityLevel < 0)
	{
		QualityLevel = GetDefault<ULevelEditorPlaySettings>()->PlayInEditorSoundQualityLevel;
	}
#endif

	if (ChildNodes.IsValidIndex(QualityLevel) && ChildNodes[QualityLevel])
	{
		ChildNodes[QualityLevel]->PrimeChildWavePlayers(bRecurse);
	}
}

int32 USoundNodeQualityLevel::GetMaxChildNodes() const
{
	return GetDefault<UAudioSettings>()->QualityLevels.Num();
}

int32 USoundNodeQualityLevel::GetMinChildNodes() const
{
	return GetDefault<UAudioSettings>()->QualityLevels.Num();
}

void USoundNodeQualityLevel::ParseNodes( FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash, FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams, TArray<FWaveInstance*>& WaveInstances )
{
#if WITH_EDITOR
	int32 QualityLevel = 0;

	if (GIsEditor)
	{
		RETRIEVE_SOUNDNODE_PAYLOAD( sizeof( int32 ) );
		DECLARE_SOUNDNODE_ELEMENT( int32, CachedQualityLevel );

		if (*RequiresInitialization)
		{
			const bool bIsPIESound = ((GEditor->bIsSimulatingInEditor || GEditor->PlayWorld != nullptr) && ActiveSound.GetWorldID() > 0);
			if (bIsPIESound)
			{
				CachedQualityLevel = GetDefault<ULevelEditorPlaySettings>()->PlayInEditorSoundQualityLevel;
			}
		}

		QualityLevel = CachedQualityLevel;
	}
	else
	{
		QualityLevel = USoundCue::GetCachedQualityLevel();
	}
#else
	
	int32 QualityLevel = USoundCue::GetCachedQualityLevel();
	
	// If CookedQualityLevelIndex has been set, we will have a *single* quality level.
	if (CookedQualityLevelIndex >= 0)
	{	
		// Remap to index 0 (as all other levels have been removed by cooker).
		QualityLevel = 0;
	}
#endif	
	
	if (ChildNodes.IsValidIndex(QualityLevel) && ChildNodes[QualityLevel])
	{
		ChildNodes[QualityLevel]->ParseNodes( AudioDevice, GetNodeWaveInstanceHash(NodeWaveInstanceHash, ChildNodes[QualityLevel], QualityLevel), ActiveSound, ParseParams, WaveInstances );
	}
}

void USoundNodeQualityLevel::Serialize(FArchive& Ar)
{
#if WITH_EDITOR

	if (Ar.IsCooking() && Ar.IsSaving() && Ar.CookingTarget() )
	{			
		if (const FPlatformAudioCookOverrides* AudioCookOverrides = Ar.CookingTarget()->GetAudioCompressionSettings())
		{
			// Prevent any other thread saving this class while we are modifying the ChildNode array.
			FScopeLock Lock(&EditorOnlyCs);
			
			// Are we doing any cook quality level optimization for this Node?			
			if (ChildNodes.IsValidIndex(AudioCookOverrides->SoundCueCookQualityIndex))
			{
				// Set our cook quality, as we serialize. 
				CookedQualityLevelIndex = AudioCookOverrides->SoundCueCookQualityIndex;

				// Move out all nodes.
				TArray<USoundNode*> ChildNodesBackup;
				ChildNodesBackup = MoveTemp(ChildNodes);
				
				// Put *just* the node we care about in our child array to be serialized by the Super
				ChildNodes.Add(ChildNodesBackup[CookedQualityLevelIndex]);

				// Call base serialize that will walk all properties and serialize them.
				Super::Serialize(Ar);

				// Return to our original state. (careful the cook only variables don't leak out).
				ChildNodes = MoveTemp(ChildNodesBackup);
				CookedQualityLevelIndex = INDEX_NONE;

				// We are done.
				return;
			}	
		}		
	}
		
#endif //WITH_EDITOR	

	// ... in all other cases, we just call the super.
	Super::Serialize(Ar);
}