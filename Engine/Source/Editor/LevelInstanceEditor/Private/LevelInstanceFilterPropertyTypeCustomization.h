// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"
#include "Internationalization/Text.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Templates/SharedPointer.h"
#include "WorldPartition/Filter/WorldPartitionActorFilterPropertyTypeCustomization.h"
#include "WorldPartition/Filter/WorldPartitionActorFilterMode.h"

class ILevelInstanceInterface;

class FLevelInstancePropertyTypeIdentifier : public IPropertyTypeIdentifier
{
	virtual bool IsPropertyTypeCustomized(const IPropertyHandle& PropertyHandle) const override
	{
		return PropertyHandle.HasMetaData(TEXT("LevelInstanceFilter"));
	}
};

// Registered (FLevelInstanceEditorModule::StartupModule) Property Customization for properties of type FWorldPartitionActorFilter for Level Instances
struct FLevelInstanceFilterPropertyTypeCustomization : public FWorldPartitionActorFilterPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLevelInstanceFilterPropertyTypeCustomization);
	}
private:
	virtual TSharedPtr<FWorldPartitionActorFilterMode::FFilter> CreateModeFilter(TArray<UObject*> OuterObjects) override;
	virtual void ApplyFilter(const FWorldPartitionActorFilterMode& Mode) override;

	TArray<ILevelInstanceInterface*> LevelInstances;
};
