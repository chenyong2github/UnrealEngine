// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeMeshReshapeDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshReshape.h"
#include "MuCOE/Nodes/CustomizableObjectNodeSkeletalMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTable.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSource.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "PropertyCustomizationHelpers.h"

#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "Engine/SkeletalMesh.h"

#include "Widgets/Text/STextBlock.h"
#include "BoneSelectionWidget.h"
#include "MuCOE/GraphTraversal.h"

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
