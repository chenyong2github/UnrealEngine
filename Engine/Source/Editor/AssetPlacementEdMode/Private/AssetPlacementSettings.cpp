// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementSettings.h"

bool UAssetPlacementSettings::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	const FName PropertyName = InProperty->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, ScaleRangeUniform) || PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bAllowNegativeUniformScale))
	{
		return (ScalingType == EFoliageScaling::Uniform) && bUseRandomScale;
	}

	const bool bCanFreeScale = (ScalingType != EFoliageScaling::Uniform) && bUseRandomScale;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, ScaleRangeX) || PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bAllowNegativeXScale))
	{
		const bool bLockX = (ScalingType == EFoliageScaling::LockXY) || (ScalingType == EFoliageScaling::LockXZ);
		return bCanFreeScale && !bLockX;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, ScaleRangeY) || PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bAllowNegativeYScale))
	{
		const bool bLockY = (ScalingType == EFoliageScaling::LockXY) || (ScalingType == EFoliageScaling::LockYZ);
		return bCanFreeScale && !bLockY;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, FreeScaleRangeZ) || PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bAllowNegativeZScale))
	{
		const bool bLockZ = (ScalingType == EFoliageScaling::LockYZ) || (ScalingType == EFoliageScaling::LockXZ);
		return bCanFreeScale && !bLockZ;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bInvertNormalAxis))
	{
		return bAlignToNormal;
	}

	return true;
}
