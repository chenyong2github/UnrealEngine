// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SelectionSystem/DataprepSelectionTransform.h"

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/TextProperty.h"

#include "DataprepGeometrySelectionTransforms.generated.h"


UCLASS(Category = SelectionTransform, Meta = (DisplayName = "Select Overlapping Actors", ToolTip = "Return all actors overlapping the selected actors"))
class UDataprepOverlappingActorsSelectionTransform : public UDataprepSelectionTransform
{
	GENERATED_BODY()

protected:
	virtual void OnExecution_Implementation(const TArray<UObject*>& InObjects, TArray<UObject*>& OutObjects) override;

	/** Accuracy of the distance field approximation, in cm. */
	UPROPERTY(EditAnywhere, Category = JacketingFilter, meta = (UIMin = "1", UIMax = "100"))
	float Accuracy;

	/** Merge distance used to fill gap, in cm. */
	UPROPERTY(EditAnywhere, Category = JacketingFilter)
	float MergeDistance;
};
