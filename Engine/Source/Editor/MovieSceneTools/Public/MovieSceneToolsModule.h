// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "IMovieSceneTools.h"

struct FAssetData;

class UK2Node;
class UBlueprint;
class UMovieSceneSection;
class UMovieSceneEventSectionBase;

class IMovieSceneToolsTakeData
{
public:
	virtual bool GatherTakes(const UMovieSceneSection* Section, TArray<FAssetData>& AssetData, uint32& OutCurrentTakeNumber) = 0;
	virtual bool GetTakeNumber(const UMovieSceneSection* Section, FAssetData AssetData, uint32& OutTakeNumber) = 0;
	virtual bool SetTakeNumber(const UMovieSceneSection*, uint32 InTakeNumber) = 0;
};

/**
* Implements the MovieSceneTools module.
*/
class MOVIESCENETOOLS_API FMovieSceneToolsModule
	: public IMovieSceneTools
{
public:

	static inline FMovieSceneToolsModule& Get()
	{
		return FModuleManager::LoadModuleChecked< FMovieSceneToolsModule >("MovieSceneTools");
	}

	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void RegisterTakeData(IMovieSceneToolsTakeData*);
	void UnregisterTakeData(IMovieSceneToolsTakeData*);

	bool GatherTakes(const UMovieSceneSection* Section, TArray<FAssetData>& AssetData, uint32& OutCurrentTakeNumber);
	bool GetTakeNumber(const UMovieSceneSection* Section, FAssetData AssetData, uint32& OutTakeNumber);
	bool SetTakeNumber(const UMovieSceneSection* Section, uint32 InTakeNumber);

private:

	void RegisterClipboardConversions();

	static void FixupPayloadParameterNameForSection(UMovieSceneEventSectionBase* Section, UK2Node* InNode, FName OldPinName, FName NewPinName);
	static bool UpgradeLegacyEventEndpointForSection(UMovieSceneEventSectionBase* Section, UBlueprint* Blueprint);

private:

	/** Registered delegate handles */
	FDelegateHandle BoolPropertyTrackCreateEditorHandle;
	FDelegateHandle BytePropertyTrackCreateEditorHandle;
	FDelegateHandle ColorPropertyTrackCreateEditorHandle;
	FDelegateHandle FloatPropertyTrackCreateEditorHandle;
	FDelegateHandle IntegerPropertyTrackCreateEditorHandle;
	FDelegateHandle VectorPropertyTrackCreateEditorHandle;
	FDelegateHandle TransformPropertyTrackCreateEditorHandle;
	FDelegateHandle EulerTransformPropertyTrackCreateEditorHandle;
	FDelegateHandle VisibilityPropertyTrackCreateEditorHandle;
	FDelegateHandle ActorReferencePropertyTrackCreateEditorHandle;
	FDelegateHandle StringPropertyTrackCreateEditorHandle;
	FDelegateHandle ObjectTrackCreateEditorHandle;

	FDelegateHandle AnimationTrackCreateEditorHandle;
	FDelegateHandle AttachTrackCreateEditorHandle;
	FDelegateHandle AudioTrackCreateEditorHandle;
	FDelegateHandle EventTrackCreateEditorHandle;
	FDelegateHandle ParticleTrackCreateEditorHandle;
	FDelegateHandle ParticleParameterTrackCreateEditorHandle;
	FDelegateHandle PathTrackCreateEditorHandle;
	FDelegateHandle CameraCutTrackCreateEditorHandle;
	FDelegateHandle CinematicShotTrackCreateEditorHandle;
	FDelegateHandle SlomoTrackCreateEditorHandle;
	FDelegateHandle SubTrackCreateEditorHandle;
	FDelegateHandle TransformTrackCreateEditorHandle;
	FDelegateHandle ComponentMaterialTrackCreateEditorHandle;
	FDelegateHandle FadeTrackCreateEditorHandle;
	FDelegateHandle SpawnTrackCreateEditorHandle;
	FDelegateHandle LevelVisibilityTrackCreateEditorHandle;
	FDelegateHandle CameraAnimTrackCreateEditorHandle;
	FDelegateHandle CameraShakeTrackCreateEditorHandle;
	FDelegateHandle MPCTrackCreateEditorHandle;
	FDelegateHandle PrimitiveMaterialCreateEditorHandle;

	FDelegateHandle GenerateEventEntryPointsHandle;
	FDelegateHandle FixupPayloadParameterNameHandle;
	FDelegateHandle UpgradeLegacyEventEndpointHandle;

	TArray<IMovieSceneToolsTakeData*> TakeDatas;
};