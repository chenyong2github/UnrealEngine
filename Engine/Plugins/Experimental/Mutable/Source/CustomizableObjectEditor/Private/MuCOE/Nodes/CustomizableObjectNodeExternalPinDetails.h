// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "Types/SlateEnums.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "PropertyHandle.h"
#include "AssetRegistry/AssetData.h"
#include "Widgets/Input/STextComboBox.h"
#include "MuCOE/Nodes/CustomizableObjectNodeExternalPin.h"


class FCustomizableObjectNodeExternalPinDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** ILayoutDetails interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;

private:
	IDetailLayoutBuilder* DetailBuilderPtr = nullptr;

	UCustomizableObjectNodeExternalPin* Node = nullptr;
	
	TArray<TSharedPtr<FString>> GroupNodeComboBoxOptions;

	void ParentObjectSelectionChanged(const FAssetData& AssetData);

	void OnGroupNodeComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo);
};
