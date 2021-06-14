// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/CreateControlAssetRigSettings.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"


UCreateControlPoseAssetRigSettings::UCreateControlPoseAssetRigSettings(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	AssetName = FString("ControlRigPose");
}
