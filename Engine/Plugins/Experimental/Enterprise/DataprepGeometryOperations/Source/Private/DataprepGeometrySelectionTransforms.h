// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SelectionSystem/DataprepSelectionTransform.h"

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Delegates/DelegateCombinations.h"
#include "DetailLayoutBuilder.h"
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
};

// The purpose of this class is to hide the field bOutputCanIncludeInput, since it does not make sense for this operation
class FDataprepOverlappingActorsSelectionTransformDetails : public IDetailCustomization
{
public:
	static TSharedRef< IDetailCustomization > MakeDetails() { return MakeShared<FDataprepOverlappingActorsSelectionTransformDetails>(); };

	/** Called when details should be customized */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override
	{
		DetailBuilder.HideProperty( DetailBuilder.GetProperty( GET_MEMBER_NAME_CHECKED( UDataprepSelectionTransform, bOutputCanIncludeInput ), UDataprepSelectionTransform::StaticClass() ) );
	}
};
