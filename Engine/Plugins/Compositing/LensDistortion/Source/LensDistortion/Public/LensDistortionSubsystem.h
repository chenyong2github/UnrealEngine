// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/EngineSubsystem.h"

#include "LensFile.h"

#include "LensDistortionSubsystem.generated.h"


/**
 * 
 */
UCLASS()
class LENSDISTORTION_API ULensDistortionSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:

	/** Get the default lens file. */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	ULensFile* GetDefaultLensFile() const;

	/** Get the default lens file. */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	void SetDefaultLensFile(ULensFile* NewDefaultLensFile);

	/** Facilitator around the picker to get the desired lens file. */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	ULensFile* GetLensFile(const FLensFilePicker& Picker) const;


private:
	
	UPROPERTY(Transient)
	ULensFile* DefaultLensFile = nullptr;
};
