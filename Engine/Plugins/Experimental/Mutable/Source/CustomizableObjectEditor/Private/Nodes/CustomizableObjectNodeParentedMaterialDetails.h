// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

#include "CustomizableObject.h"	
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "Misc/Guid.h"
#include "Types/SlateEnums.h"
#include "PropertyHandle.h"
#include "CustomizableObjectEditorModule.h"
#include "Nodes/CustomizableObjectNode.h"
#include "Nodes/CustomizableObjectNodeParentedMaterial.h"

class FCustomizableObjectNodeParentedMaterialDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	// ILayoutDetails interface
	void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;

protected:
	UCustomizableObjectNode* Node = nullptr;
	FCustomizableObjectNodeParentedMaterial* NodeParentedMaterial = nullptr;

	struct FMaterialReference
	{
		TWeakObjectPtr<UCustomizableObject> Object;
		FGuid Id;
	};
	TArray<FMaterialReference> ParentMaterialOptionReferences;
	TArray<TSharedPtr<FString>> ParentMaterialOptionNames;

	/** Given a list of parent materials, get their ComboBox option entry names. 
	 *
	 * All ParentMaterialNodes pointers must be valid.
	 */
	TArray<TSharedPtr<FString>> GetComboBoxNames(const TArray<UCustomizableObjectNodeMaterial*>& ParentMaterialNodes) const;

	/** Return a fornamted name for the parent Material. */
	FString GetComboBoxParentMaterialName(const UCustomizableObjectNodeMaterial* ParentMaterial) const;

	virtual void OnParentComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> ParentProperty);
};
