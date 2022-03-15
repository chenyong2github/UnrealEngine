// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayer/DataLayerPropertyTypeCustomizationHelper.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "DataLayerMode.h"

#define LOCTEXT_NAMESPACE "DataLayer"

TSharedRef<SWidget> FDataLayerPropertyTypeCustomizationHelper::CreateDataLayerMenu(TFunction<void(const UDataLayerInstance* DataLayer)> OnDataLayerSelectedFunction)
{
	return FDataLayerPickingMode::CreateDataLayerPickerWidget(FOnDataLayerPicked::CreateLambda([OnDataLayerSelectedFunction](UDataLayerInstance* TargetDataLayer)
	{
		OnDataLayerSelectedFunction(TargetDataLayer);
	}));
}

#undef LOCTEXT_NAMESPACE