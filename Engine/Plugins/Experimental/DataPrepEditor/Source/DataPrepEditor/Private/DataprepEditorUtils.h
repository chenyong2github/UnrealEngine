// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Parameterization/DataprepParameterizationUtils.h"

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

class FMenuBuilder;
class SWidget;
class UDataprepAsset;
class UDataprepParameterizableObject;

struct FDataprepParametrizationActionData : public FGCObject
{
	FDataprepParametrizationActionData(UDataprepAsset& InDataprepAsset, UDataprepParameterizableObject& InObject, const TArray<FDataprepPropertyLink>& InPropertyChain)
		: FGCObject()
		, DataprepAsset(&InDataprepAsset)
		, Object(&InObject)
		, PropertyChain(InPropertyChain)
	{}

	UDataprepAsset* DataprepAsset;
	UDataprepParameterizableObject* Object;
	TArray<FDataprepPropertyLink> PropertyChain;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	bool IsValid() const;
};

class FDataprepEditorUtils
{
public:
	/**
	 * Find the the owning dataprep asset of the source object and send a notification to the dataprep editor so it can react when its pipeline is modified
	 */
	static void NotifySystemOfChangeInPipeline(UObject* SourceObject);

	/**
	 * Populate a menu builder with the section made for the parameterization
	 * @param DataprepAsset the asset that own the object
	 * @param Object The Object on which we want to modify the parametrization binding
	 * @param PropertyChain The property chain is the property path from the class of the object to the property that we want to edit
	 */
	static void PopulateMenuForParameterization(FMenuBuilder& MenuBuilder, UDataprepAsset& DataprepAsset, UDataprepParameterizableObject& Object, const TArray<FDataprepPropertyLink>& PropertyChain);

	static FSlateFontInfo GetGlyphFont();

	/**
	 * Make a context menu widget to manage parameterization link of a property
	 */
	static TSharedPtr<SWidget> MakeContextMenu(const TSharedPtr<FDataprepParametrizationActionData>& ParameterizationActionData);
};
