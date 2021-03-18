// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IDetailCustomization.h"

#include "CoreMinimal.h"
#include "PropertyHandle.h"

struct FRemoteControlEntity;
class FStructOnScope;
class IDetailCategoryBuilder;

/**
 * Implements a details panel customization for remote control entities.
 */
class FRemoteControlEntityCustomization : public IDetailCustomization
{
public:
	FRemoteControlEntityCustomization();
	//~ Begin IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ Begin IPropertyTypeCustomization interface

	/** Makes a new instance of this detail layout class for a specific detail view requesting it. */
	static TSharedRef<IDetailCustomization> MakeInstance();

private:
	/** Create a widget that handles modifying the min and max properties. */
	void CreateRangeWidget(IDetailLayoutBuilder& LayoutBuilder, IDetailCategoryBuilder& CategoryBuilder);

	/** Handler called upon modifying the metadata of an exposed entity. */
	void OnMetadataKeyCommitted(const FText& Text, ETextCommit::Type Type, FName MetadataKey);

	/** Get a pointer to the entity currently being displayed in the details view. */
	FRemoteControlEntity* GetEntityPtr();
	
	/** Get a const pointer to the entity currently being displayed in the details view. */
	const FRemoteControlEntity* GetEntityPtr() const;
	
	/** Get a value from the metadata map of the entity. */
	FText GetMetadataValue(FName Key) const;

	/** Generate property row widgets */
	void GeneratePropertyRows(IDetailLayoutBuilder& DetailBuilder, IDetailCategoryBuilder& CategoryBuilder);

private:
	/** Entity currently being displayed. */
	TSharedPtr<FStructOnScope> DisplayedEntity;
	
	DECLARE_DELEGATE_TwoParams(FOnCustomizeMetadata, IDetailLayoutBuilder&, IDetailCategoryBuilder&);
	/** Dispatch table for metadata items. */
	TMap<FName, FOnCustomizeMetadata> MetadataCustomizations;
};
