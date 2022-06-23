// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerInstanceCustomization.h"
#include "DetailLayoutBuilder.h"

#define LOCTEXT_NAMESPACE "FDataLayerInstanceDetails"

TSharedRef<IDetailCustomization> FDataLayerInstanceDetails::MakeInstance()
{
	return MakeShareable(new FDataLayerInstanceDetails);
}

void FDataLayerInstanceDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	
	bool bHasRuntime = false;
	for (const TWeakObjectPtr<UObject>& SelectedObject : ObjectsBeingCustomized)
	{
		UDataLayerInstance* DataLayerInstance = Cast<UDataLayerInstance>(SelectedObject.Get());
		if (DataLayerInstance && DataLayerInstance->IsRuntime())
		{
			bHasRuntime = true;
			break;
		}
	}
	if (!bHasRuntime)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDataLayerInstance, InitialRuntimeState));
	}
}

#undef LOCTEXT_NAMESPACE
