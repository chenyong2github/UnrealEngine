// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LiveLinkRole.h"
#include "LiveLinkTypes.h"
#include "Templates/SubclassOf.h"
#include "LiveLinkPresetTypes.generated.h"


class ULiveLinkSourceSettings;
class ULiveLinkSubjectSettings;
class ULiveLinkVirtualSubject;


USTRUCT()
struct FLiveLinkSourcePreset
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Guid;

	UPROPERTY()
	ULiveLinkSourceSettings* Settings = nullptr;

	/** The SourceType when the source was saved to a Preset. */
	UPROPERTY()
	FText SourceType;
};


USTRUCT()
struct FLiveLinkSubjectPreset
{
	GENERATED_BODY()

	UPROPERTY()
	FLiveLinkSubjectKey Key;

	UPROPERTY()
	TSubclassOf<ULiveLinkRole> Role;

	UPROPERTY()
	ULiveLinkSubjectSettings* Settings = nullptr;

	UPROPERTY()
	ULiveLinkVirtualSubject* VirtualSubject = nullptr;

	UPROPERTY()
	bool bEnabled = false;
};