// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectNodeMeshReshapeDetails.h"
#include "Nodes/CustomizableObjectNodeMeshReshape.h"
#include "Nodes/CustomizableObjectNodeSkeletalMesh.h"
#include "Nodes/CustomizableObjectNodeTable.h"
#include "GenerateMutableSource/GenerateMutableSource.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "PropertyCustomizationHelpers.h"

#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "Engine/SkeletalMesh.h"

#include "Widgets/Text/STextBlock.h"
#include "BoneSelectionWidget.h"
#include "GraphTraversal.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectDetails"


TSharedRef<IDetailCustomization> FCustomizableObjectNodeMeshReshapeDetails::MakeInstance()
{
	return MakeShareable(new FCustomizableObjectNodeMeshReshapeDetails);
}


void FCustomizableObjectNodeMeshReshapeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
}


void FCustomizableObjectNodeMeshReshapeDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
}


#undef LOCTEXT_NAMESPACE
