// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMatineeConverter;
class IAssetTools;
class UCameraAnim;
class UFactory;
struct FToolMenuContext;

class FCameraAnimToTemplateSequenceConverter
{
public:
	FCameraAnimToTemplateSequenceConverter(const FMatineeConverter* InMatineeConverter);

	void ConvertCameraAnim(const FToolMenuContext& MenuContext);
	UObject* ConvertCameraAnim(IAssetTools& AssetTools, UFactory* CameraAnimationSequenceFactoryNew, UCameraAnim* CameraAnim, int32& NumWarnings);

private:
	UObject* ConvertSingleCameraAnimToTemplateSequence(UCameraAnim* CameraAnimToConvert, IAssetTools& AssetTools, UFactory* CameraAnimationSequenceFactoryNew, bool bPromptCreateAsset, int32& NumWarnings);

private:
	const FMatineeConverter* MatineeConverter;
};

