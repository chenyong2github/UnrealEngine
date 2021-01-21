// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMatineeConverter;
class IAssetTools;
class IAssetRegistry;
class UBlueprint;
class UFactory;
class UMatineeCameraShake;
class FCameraAnimToTemplateSequenceConverter;
class SWidget;
class UK2Node_VariableGet;
struct FAssetData;

struct FMatineeCameraShakeToNewCameraShakeConversionStats
{
	int32 NumAnims = 0;
	int32 NumWarnings = 0;

	TArray<UPackage*> NewPackages;
	TArray<UPackage*> ConvertedPackages;
	TArray<UPackage*> ReusedPackages;
};

class FMatineeCameraShakeToNewCameraShakeConverter
{
public:
	FMatineeCameraShakeToNewCameraShakeConverter(const FMatineeConverter* InMatineeConverter);

	static TSharedRef<SWidget> CreateMatineeCameraShakeConverter(const FMatineeConverter* InMatineeConverter);

	void ConvertMatineeCameraShakes(const TArray<FAssetData>& AssetDatas);

private:
	bool ConvertSingleMatineeCameraShakeToNewCameraShakeSimple(FCameraAnimToTemplateSequenceConverter& CameraAnimConverter, IAssetTools& AssetTools, IAssetRegistry& AssetRegistry, UFactory* CameraAnimationSequenceFactoryNew, UBlueprint* OldShakeBlueprint, TOptional<bool>& bAutoReuseExistingAsset, FMatineeCameraShakeToNewCameraShakeConversionStats& Stats);

	void ReportConversionStats(const FMatineeCameraShakeToNewCameraShakeConversionStats& Stats, bool bPromptForCheckoutAndSave);

private:
	const FMatineeConverter* MatineeConverter;
};

