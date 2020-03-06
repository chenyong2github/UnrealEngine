// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SelectionSystem/DataprepSelectionTransform.h"

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/TextProperty.h"

#include "DataprepSelectionTransforms.generated.h"

UENUM()
enum class EDataprepHierarchySelectionPolicy : uint8
{
	/** Select immediate children of the selected objects */
	ImmediateChildren,

	/** Select all descendants of the selected objects */
	AllDescendants,
};

UCLASS(Category = SelectionTransform, Meta = (DisplayName="Reference Selection Transform", ToolTip = "Return all the assets used/referenced by the selected objects") )
class UDataprepReferenceSelectionTransform : public UDataprepSelectionTransform
{
	GENERATED_BODY()

protected:
	virtual void OnExecution_Implementation(const TArray<UObject*>& InObjects, TArray<UObject*>& OutObjects) override;
};

UCLASS(Category = SelectionTransform, Meta = (DisplayName="Select Hierarchy", ToolTip = "Return immediate children or all the descendants of the selected objects") )
class UDataprepHierarchySelectionTransform : public UDataprepSelectionTransform
{
	GENERATED_BODY()

	UDataprepHierarchySelectionTransform()
		: SelectionPolicy(EDataprepHierarchySelectionPolicy::ImmediateChildren)
	{}

protected:
	virtual void OnExecution_Implementation(const TArray<UObject*>& InObjects, TArray<UObject*>& OutObjects) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HierarchySelectionOptions", meta = (DisplayName = "Select", ToolTip = "Specify policy of hierarchical parsing of selected objects"))
	EDataprepHierarchySelectionPolicy SelectionPolicy;
};