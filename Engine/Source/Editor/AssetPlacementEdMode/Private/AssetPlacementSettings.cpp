// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementSettings.h"

bool UAssetPlacementSettings::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	const FName PropertyName = InProperty->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bAllowNegativeScale))
	{
		return bUseRandomScale;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bAllowNegativeRotationX))
	{
		return bUseRandomRotationX;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bAllowNegativeRotationY))
	{
		return bUseRandomRotationY;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bAllowNegativeRotationZ))
	{
		return bUseRandomRotationZ;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bInvertNormalAxis))
	{
		return bAlignToNormal;
	}

	return true;
}
