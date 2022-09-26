// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectNodeObjectGroupDetails.h"
#include "Nodes/CustomizableObjectNodeObjectGroup.h"
#include "CustomizableObjectEditor.h"
#include "CustomizableObjectEditorModule.h"
#include "PropertyCustomizationHelpers.h"
//#include "SPropertyEditorAsset.h"
//#include "CustomizableObjectEditorUtilities.h"
//
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
//#include "LevelEditor.h"
//#include "LevelEditorActions.h"
//
//#include "Widgets/Input/SButton.h"
//#include "Widgets/Text/STextBlock.h"
//
//#include "Runtime/Launch/Resources/Version.h"
//#include "Engine/SkeletalMesh.h"
//#include "Widgets/Input/STextComboBox.h"
//#include "Math/Vector4.h"
//#include "Math/Vector.h"
//
//#include "Runtime/Launch/Resources/Version.h"
//#if (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION > 22) || ENGINE_MAJOR_VERSION >= 5
//#include "HAL/PlatformApplicationMisc.h"
//#endif


#define LOCTEXT_NAMESPACE "CustomizableObjectGroupDetails"


TSharedRef<IDetailCustomization> FCustomizableObjectNodeObjectGroupDetails::MakeInstance()
{
	return MakeShareable(new FCustomizableObjectNodeObjectGroupDetails);
}


void FCustomizableObjectNodeObjectGroupDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const IDetailsView* DetailsView = DetailBuilder.GetDetailsView();
	UCustomizableObjectNodeObjectGroup* NodeGroup = nullptr;
	if (DetailsView->GetSelectedObjects().Num())
	{
		if (DetailsView->GetSelectedObjects()[0].Get()->IsA(UCustomizableObjectNodeObjectGroup::StaticClass()))
		{
			NodeGroup = Cast<UCustomizableObjectNodeObjectGroup>(DetailsView->GetSelectedObjects()[0].Get());
		}
	}

	if (NodeGroup)
	{
		if (TSharedPtr<FCustomizableObjectEditor> GraphEditor = StaticCastSharedPtr<FCustomizableObjectEditor>(NodeGroup->GetGraphEditor()))
		{
			if (UCustomizableObject* NodeGroupCO = CastChecked<UCustomizableObject>(NodeGroup->GetCustomizableObjectGraph()->GetOuter()))
			{
				TArray<UCustomizableObject*> ChildNodes;
				IDetailCategoryBuilder& BlocksCategory = DetailBuilder.EditCategory("Group Info");
				GraphEditor->GetExternalChildObjects(NodeGroupCO, ChildNodes, false, EObjectFlags::RF_NoFlags);
				for (UCustomizableObject* ChildNode : ChildNodes)
				{
					if (ChildNode)
					{
						TArray<UCustomizableObjectNodeObject*> ObjectNodes;
						ChildNode->Source->GetNodesOfClass<UCustomizableObjectNodeObject>(ObjectNodes);
						UCustomizableObjectNodeObject* groupParent = nullptr;

						for (UCustomizableObjectNodeObject* ChildCONode : ObjectNodes)
						{
							if (ChildCONode->bIsBase)
							{
								groupParent = ChildCONode;
								break;
							}
						}

						if (!groupParent) continue;

						FString GroupID = groupParent->Identifier.ToString();

						FCustomizableObjectIdPair* DirectGroupChilds = NodeGroupCO->GroupNodeMap.Find(GroupID);
						if (!DirectGroupChilds) continue;

						if (NodeGroup->GroupName.Equals(DirectGroupChilds->CustomizableObjectGroupName))
						{
							BlocksCategory.AddCustomRow(LOCTEXT("FCustomizableObjectNodeObjectGroupDetails", "External Customizable Objects in this Group"))[
								SNew(SObjectPropertyEntryBox)
									.ObjectPath(ChildNode->GetPathName())
									.AllowedClass(UCustomizableObject::StaticClass())
									.AllowClear(false)
									.DisplayUseSelected(false)
									.DisplayBrowse(true)
									.EnableContentPicker(false)
									.DisplayThumbnail(true)
							];
						}
					}
				}
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE