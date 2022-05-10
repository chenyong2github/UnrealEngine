// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerInstanceWithAsset.h"

#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationErrorHandler.h"

UDataLayerInstanceWithAsset::UDataLayerInstanceWithAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

#if WITH_EDITOR
FName UDataLayerInstanceWithAsset::MakeName(const UDataLayerAsset* DeprecatedDataLayer)
{
	return FName(FString::Format(TEXT("DataLayer_{0}"), { FGuid::NewGuid().ToString() }));
}

void UDataLayerInstanceWithAsset::OnCreated(const UDataLayerAsset* Asset)
{
	check(!GetOuterAWorldDataLayers()->HasDeprecatedDataLayers() || IsRunningCommandlet());

	Modify(/*bAlwaysMarkDirty*/false);

	check(DataLayerAsset == nullptr);
	DataLayerAsset = Asset;

	SetVisible(true);
}

bool UDataLayerInstanceWithAsset::IsReadOnly() const
{
	return !GetOuterAWorldDataLayers()->IsMainWorldDataLayers();
}

bool UDataLayerInstanceWithAsset::IsLocked() const
{
	if (Super::IsLocked())
	{
		return true;
	}
	return IsReadOnly();
}

bool UDataLayerInstanceWithAsset::AddActor(AActor* Actor) const
{
	// If actor's level WorldDataLayers doesn't match this DataLayerInstance outer WorldDataLayers, 
	// Make sure that an instance for this Data Layer asset exists in the outer level AWorldDataLayer and forward the call on it.
	AWorldDataLayers* OuterWorldDataLayers = Actor->GetLevel()->GetWorldDataLayers();
	if (GetOuterAWorldDataLayers() != OuterWorldDataLayers)
	{
		check(OuterWorldDataLayers);
		UDataLayerInstanceWithAsset* OuterDataLayerInstanceWithAsset = Cast<UDataLayerInstanceWithAsset>(const_cast<UDataLayerInstance*>(OuterWorldDataLayers->GetDataLayerInstance(DataLayerAsset)));
		if (!OuterDataLayerInstanceWithAsset)
		{
			OuterDataLayerInstanceWithAsset = OuterWorldDataLayers->CreateDataLayer<UDataLayerInstanceWithAsset>(DataLayerAsset);
		}
		return OuterDataLayerInstanceWithAsset->AddActor(Actor);
	}
	else
	{
		return Actor->AddDataLayer(DataLayerAsset);
	}
}

bool UDataLayerInstanceWithAsset::RemoveActor(AActor* Actor) const
{
	return Actor->RemoveDataLayer(DataLayerAsset);
}

bool UDataLayerInstanceWithAsset::Validate(IStreamingGenerationErrorHandler* ErrorHandler) const
{
	bool bIsValid = true;

	if (GetAsset() == nullptr)
	{
		ErrorHandler->OnInvalidReferenceDataLayerAsset(this);
		return false;
	}

	UDataLayerSubsystem* DataLayerSubsystem = UWorld::GetSubsystem<UDataLayerSubsystem>(GetWorld());
	DataLayerSubsystem->ForEachDataLayer([&bIsValid, this, ErrorHandler](UDataLayerInstance* DataLayerInstance)
	{
		if (DataLayerInstance != this)
		{
			if (UDataLayerInstanceWithAsset* DataLayerInstanceWithAsset = Cast<UDataLayerInstanceWithAsset>(DataLayerInstance))
			{
				if (DataLayerInstanceWithAsset->GetAsset() == GetAsset())
				{
					ErrorHandler->OnDataLayerAssetConflict(this, DataLayerInstanceWithAsset);
					bIsValid = false;
					return false;
				}
			}
		}

		return true;
	}, GetOuterAWorldDataLayers()->GetLevel()); // Resolve DataLayerInstances based on outer level

	bIsValid &= Super::Validate(ErrorHandler);

	return bIsValid;
}
#endif