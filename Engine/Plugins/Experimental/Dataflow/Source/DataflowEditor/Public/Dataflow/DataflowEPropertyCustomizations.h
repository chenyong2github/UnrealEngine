// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"
#include "UObject/ObjectMacros.h"

#include "DataflowEPropertyCustomizations.generated.h"

struct FDataflowNode;

class UDataflow;

UCLASS()
class DATAFLOWEDITOR_API UDataflowSEditorObject : public UObject
{
	GENERATED_BODY()
public:
	TSharedPtr<FDataflowNode> Node = nullptr;
	UDataflow* Graph = nullptr;

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
};

/**
* Detail customization for PS5 target settings panel
*/
class DATAFLOWEDITOR_API FDataflowSEditorCustomization : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

};

