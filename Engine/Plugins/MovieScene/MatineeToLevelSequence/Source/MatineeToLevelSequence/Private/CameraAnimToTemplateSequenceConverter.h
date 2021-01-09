// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class ACameraActor;
class AMatineeActorCameraAnim;
class FMatineeConverter;
class IAssetRegistry;
class IAssetTools;
class UCameraAnim;
class UFactory;
struct FToolMenuContext;

class FCameraAnimToTemplateSequenceConverter
{
public:
	FCameraAnimToTemplateSequenceConverter(const FMatineeConverter* InMatineeConverter);

	void ConvertCameraAnim(const FToolMenuContext& MenuContext);
	UObject* ConvertCameraAnim(IAssetTools& AssetTools, IAssetRegistry& AssetRegistry, UFactory* CameraAnimationSequenceFactoryNew, UCameraAnim* CameraAnim, TOptional<bool>& bAutoReuseExistingAsset, int32& NumWarnings, bool& bAssetCreated);

private:
	UObject* ConvertSingleCameraAnimToTemplateSequence(UCameraAnim* CameraAnimToConvert, IAssetTools& AssetTools, IAssetRegistry& AssetRegistry, UFactory* CameraAnimationSequenceFactoryNew, bool bPromptCreateAsset, TOptional<bool>& bAutoReuseExistingAsset, int32& NumWarnings, bool& bAssetCreated);

	void CreateMatineeActorForCameraAnim(UCameraAnim* InCameraAnim);
	void CreateCameraActorForCameraAnim(UCameraAnim* InCameraAnim);
	void CleanUpActors();

private:
	const FMatineeConverter* MatineeConverter;

	TWeakObjectPtr<AMatineeActorCameraAnim> PreviewMatineeActor;
	TWeakObjectPtr<ACameraActor> PreviewCamera;
};

