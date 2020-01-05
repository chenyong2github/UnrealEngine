// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "LiveLinkPresetTypes.h"
#include "LiveLinkPreset.generated.h"


UCLASS(BlueprintType)
class LIVELINK_API ULiveLinkPreset : public UObject
{
	GENERATED_BODY()

private:
	UPROPERTY()
	TArray<FLiveLinkSourcePreset> Sources;

	UPROPERTY()
	TArray<FLiveLinkSubjectPreset> Subjects;

public:
	/** Get the list of source presets. */
	const TArray<FLiveLinkSourcePreset>& GetSourcePresets() const { return Sources; }

	/** Get the list of subject presets. */
	const TArray<FLiveLinkSubjectPreset>& GetSubjectPresets() const { return Subjects; }

	/**
	 * Remove all previous sources and subjects and add the sources and subjects from this preset.
	 * @return True is all sources and subjects from this preset could be created and added.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category="LiveLink")
	bool ApplyToClient() const;

	/** Reset this preset and build the list of sources and subjects from the client. */
	UFUNCTION(BlueprintCallable, Category="LiveLink")
	void BuildFromClient();
};
