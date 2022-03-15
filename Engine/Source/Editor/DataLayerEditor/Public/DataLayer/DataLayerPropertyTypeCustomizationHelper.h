// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/SharedPointer.h"
#include "Widgets/SWidget.h"

class UDataLayerInstance;

struct DATALAYEREDITOR_API FDataLayerPropertyTypeCustomizationHelper
{
	static TSharedRef<SWidget> CreateDataLayerMenu(TFunction<void(const UDataLayerInstance* DataLayer)> OnDataLayerSelectedFunction);
};
