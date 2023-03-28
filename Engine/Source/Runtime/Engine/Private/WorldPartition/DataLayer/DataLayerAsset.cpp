// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataLayerAsset)

UDataLayerAsset::UDataLayerAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DataLayerType(EDataLayerType::Editor)
	, DebugColor(FColor::Black)
{}

bool UDataLayerAsset::IsPrivate() const
{
	return !!GetTypedOuter<UDataLayerInstance>();
}

#if WITH_EDITOR
void UDataLayerAsset::PostLoad()
{
	if (DebugColor == FColor::Black)
	{
		DebugColor = FColor::MakeRandomSeededColor(GetTypeHash(GetName()));
	}

	Super::PostLoad();
}

bool UDataLayerAsset::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	if (InProperty)
	{
		if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UDataLayerAsset, DataLayerType))
		{
			// UDataLayerAsset outered to a UDataLayerInstance do not support Runtime type
			return !IsPrivate();
		}
	}

	return true;
}
#endif
