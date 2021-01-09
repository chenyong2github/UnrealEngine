// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "Engine/AssetUserData.h"
#include "LevelSequenceAnimSequenceLink.generated.h"

class UAnimSequence;

/** Link To Anim Sequence that we are linked too.*/
USTRUCT()
struct LEVELSEQUENCE_API FLevelSequenceAnimSequenceLinkItem
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FGuid SkelTrackGuid;

	UPROPERTY()
	FSoftObjectPath PathToAnimSequence;

	//From Editor Only UAnimSeqExportOption we cache this since we can re-import dynamically
	UPROPERTY()
	bool bExportTransforms = true;
	UPROPERTY()
	bool bExportCurves = true;
	UPROPERTY()
	bool bRecordInWorldSpace = false;

	void SetAnimSequence(UAnimSequence* InAnimSequence);
	UAnimSequence* ResolveAnimSequence();

};

/** Link To Set of Anim Sequences that we may be linked to.*/
UCLASS()
class LEVELSEQUENCE_API ULevelSequenceAnimSequenceLink : public UAssetUserData
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY()
	TArray< FLevelSequenceAnimSequenceLinkItem> AnimSequenceLinks;
};