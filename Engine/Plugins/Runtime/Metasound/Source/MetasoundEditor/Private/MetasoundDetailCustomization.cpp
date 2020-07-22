// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundDetailCustomization.h"

#include "CoreMinimal.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Metasound.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"
#include "Templates/Casts.h"
#include "Templates/SharedPointer.h"


void FMetasoundDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	IDetailCategoryBuilder& Category = DetailLayout.EditCategory("General");

	TSharedPtr<IPropertyHandle> AuthorHandle = DetailLayout.GetProperty("RootMetasoundDocument.RootClass.Metadata.AuthorName");
	TSharedPtr<IPropertyHandle> DescHandle = DetailLayout.GetProperty("RootMetasoundDocument.RootClass.Metadata.MetasoundDescription");
	TSharedPtr<IPropertyHandle> NodeTypeHandle = DetailLayout.GetProperty("RootMetasoundDocument.RootClass.Metadata.NodeType");

	Category.AddProperty(NodeTypeHandle);
	Category.AddProperty(AuthorHandle);
	Category.AddProperty(DescHandle);

	// Hack to hide parent structs for nested metadata properties
	DetailLayout.HideCategory("Hidden");
}
