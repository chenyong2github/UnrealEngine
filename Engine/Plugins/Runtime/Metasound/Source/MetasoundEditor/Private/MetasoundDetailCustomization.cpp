// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundDetailCustomization.h"

#include "CoreMinimal.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailGroup.h"
#include "Metasound.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"
#include "Templates/Casts.h"
#include "Templates/SharedPointer.h"


#define LOCTEXT_NAMESPACE "MetasoundEditor"

void FMetasoundDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	// General Category
	IDetailCategoryBuilder& GeneralCategoryBuilder = DetailLayout.EditCategory("General");

	TSharedPtr<IPropertyHandle> AuthorHandle = DetailLayout.GetProperty("RootMetasoundDocument.RootClass.Metadata.AuthorName");
	TSharedPtr<IPropertyHandle> DescHandle = DetailLayout.GetProperty("RootMetasoundDocument.RootClass.Metadata.MetasoundDescription");
	TSharedPtr<IPropertyHandle> NodeTypeHandle = DetailLayout.GetProperty("RootMetasoundDocument.RootClass.Metadata.NodeType");

	GeneralCategoryBuilder.AddProperty(NodeTypeHandle);
	GeneralCategoryBuilder.AddProperty(AuthorHandle);
	GeneralCategoryBuilder.AddProperty(DescHandle);

	IDetailCategoryBuilder& InputsCategoryBuilder = DetailLayout.EditCategory("Inputs");
	TSharedPtr<IPropertyHandle> InputsHandle = DetailLayout.GetProperty("RootMetasoundDocument.RootClass.Inputs");
	InputsCategoryBuilder.AddProperty(InputsHandle);

	IDetailCategoryBuilder& OutputsCategoryBuilder = DetailLayout.EditCategory("Outputs");
	TSharedPtr<IPropertyHandle> OutputsHandle = DetailLayout.GetProperty("RootMetasoundDocument.RootClass.Outputs");
	OutputsCategoryBuilder.AddProperty(OutputsHandle);

	// Hack to hide parent structs for nested metadata properties
	DetailLayout.HideCategory("Hidden");
}
#undef LOCTEXT_NAMESPACE