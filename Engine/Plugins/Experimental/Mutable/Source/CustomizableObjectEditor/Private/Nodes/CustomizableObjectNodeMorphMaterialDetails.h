// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "Misc/Guid.h"
#include "Types/SlateEnums.h"
#include "PropertyHandle.h"
#include "CustomizableObjectEditorModule.h"

class FCustomizableObjectNodeMorphMaterialDetails : public IDetailCustomization
{
public:
	// Makes a new instance of this detail layout class for a specific detail view requesting it 
	static TSharedRef<IDetailCustomization> MakeInstance();

	// ILayoutDetails interface
	void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;


private:

	class UCustomizableObjectNodeMorphMaterial* Node = nullptr;

	TArray< TSharedPtr<FString> > MorphTargetComboOptions;

	void OnParentComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> ParentProperty);
	void OnMorphTargetComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> Property);

	TSharedPtr<FString> PrepareComboboxSelection(const int LODIndex, TArray<UCustomizableObjectNodeObject*>& ParentObjectNodes);

};
