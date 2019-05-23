// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintEditor.h"
#include "IHasPersonaToolkit.h"

class IAnimationBlueprintEditor : public FBlueprintEditor, public IHasPersonaToolkit
{
public:
	/** Get the last pin type we used to create a graph pin */
	virtual const FEdGraphPinType& GetLastGraphPinTypeUsed() const = 0;

	/** Set the last pin type we used to create a graph pin */
	virtual void SetLastGraphPinTypeUsed(const FEdGraphPinType& InType) = 0;
};
