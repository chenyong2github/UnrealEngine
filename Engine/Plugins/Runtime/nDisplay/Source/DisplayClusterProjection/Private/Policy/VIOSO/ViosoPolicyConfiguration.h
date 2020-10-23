// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FViosoPolicyConfiguration
{
	FString OriginCompId;

	FString INIFile;
	FString ChannelName;

	// Calibration file and display segment with geometry
	FString CalibrationFile;
	int     CalibrationIndex = -1;

	FMatrix BaseMatrix;

	//@todo add more vioso options, if required
	float Gamma = 1.f;

	bool Initialize(const TMap<FString, FString>& InParameters, const FString& InViewportId);

	FString ToString() const;
};

