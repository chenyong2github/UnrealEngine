// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayer/DataLayerPropertyTypeCustomizationHelper.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "DataLayerMode.h"

#define LOCTEXT_NAMESPACE "DataLayer"

TSharedRef<SWidget> FDataLayerPropertyTypeCustomizationHelper::CreateDataLayerMenu(TFunction<void(const UDataLayer* DataLayer)> OnDataLayerSelectedFunction)
{
	return FDataLayerPickingMode::CreateDataLayerPickerWidget(FOnDataLayerPicked::CreateLambda([OnDataLayerSelectedFunction](UDataLayer* TargetDataLayer)
	{
		OnDataLayerSelectedFunction(TargetDataLayer);
	}));
}

#undef LOCTEXT_NAMESPACE