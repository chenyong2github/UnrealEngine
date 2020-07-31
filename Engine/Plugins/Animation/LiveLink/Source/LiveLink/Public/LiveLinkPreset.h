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
	UPROPERTY(VisibleAnywhere, Category = "LiveLinkSourcePresets")
	TArray<FLiveLinkSourcePreset> Sources;

	UPROPERTY(VisibleAnywhere, Category = "LiveLinkSubjectPresets")
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

	/**
	 * Add the sources and subjects from this preset, but leave any existing sources and subjects connected.
	 *
	 * @param bRecreatePresets	When true, if subjects and sources from this preset already exist, we will recreate them.
	 *
	 * @return True is all sources and subjects from this preset could be created and added.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "LiveLink")
	bool AddToClient(const bool bRecreatePresets = true) const;

	/** Reset this preset and build the list of sources and subjects from the client. */
	UFUNCTION(BlueprintCallable, Category="LiveLink")
	void BuildFromClient();
};
