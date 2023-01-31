// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerInstanceWithAsset.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "Engine/Level.h"
#include "Misc/StringFormatArg.h"
#include "UObject/UnrealType.h"
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationErrorHandler.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataLayerInstanceWithAsset)

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
	if (Super::IsReadOnly())
	{
		return true;
	}
	return GetOuterAWorldDataLayers()->IsSubWorldDataLayers();
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
	check(GetTypedOuter<ULevel>() == Actor->GetLevel()); // Make sure the instance is part of the same world as the actor.
	check(UDataLayerManager::GetDataLayerManager(Actor)->GetDataLayerInstance(DataLayerAsset) != nullptr); // Make sure the DataLayerInstance exists for this level
	return Actor->AddDataLayer(DataLayerAsset);
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

	// Get the DataLayerManager for this DataLayerInstance which will be the one of its outer world
	UDataLayerManager* DataLayerManager = UDataLayerManager::GetDataLayerManager(this);
	if (ensure(DataLayerManager))
	{
		DataLayerManager->ForEachDataLayerInstance([&bIsValid, this, ErrorHandler](UDataLayerInstance* DataLayerInstance)
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
		});
	}

	bIsValid &= Super::Validate(ErrorHandler);

	return bIsValid;
}

void UDataLayerInstanceWithAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDataLayerInstanceWithAsset, DataLayerAsset))
	{
		GetOuterAWorldDataLayers()->ResolveActorDescContainers();
	}
}

void UDataLayerInstanceWithAsset::PreEditUndo()
{
	Super::PreEditUndo();
	CachedDataLayerAsset = DataLayerAsset;
}

void UDataLayerInstanceWithAsset::PostEditUndo()
{
	Super::PostEditUndo();
	if (CachedDataLayerAsset != DataLayerAsset)
	{
		GetOuterAWorldDataLayers()->ResolveActorDescContainers();
	}
	CachedDataLayerAsset = nullptr;
}
#endif
