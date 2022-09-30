// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectNodeMorphMaterialDetails.h"
#include "CustomizableObjectEditorModule.h"
#include "../CustomizableObjectCompiler.h"
#include "PropertyCustomizationHelpers.h"
#include "CustomizableObjectEditorUtilities.h"

#include "LevelEditor.h"
#include "LevelEditorActions.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "GraphTraversal.h"
#include "LevelEditor.h"
#include "LevelEditorActions.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/MorphTarget.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/STextComboBox.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

#include "Runtime/Launch/Resources/Version.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectDetails"


TSharedRef<IDetailCustomization> FCustomizableObjectNodeMorphMaterialDetails::MakeInstance()
{
	return MakeShareable( new FCustomizableObjectNodeMorphMaterialDetails );
}


void FCustomizableObjectNodeMorphMaterialDetails::CustomizeDetails( IDetailLayoutBuilder& DetailBuilder )
{

	const IDetailsView* DetailsView = DetailBuilder.GetDetailsView();
	if ( DetailsView->GetSelectedObjects().Num() )
	{
		Node = Cast<UCustomizableObjectNodeMorphMaterial>( DetailsView->GetSelectedObjects()[0].Get() );
	}

	IDetailCategoryBuilder& BlocksCategory = DetailBuilder.EditCategory( "Customizable Object" );
	//BlocksCategory.CategoryIcon( "ActorClassIcon.CustomizableObject" );

	MorphTargetComboOptions.Empty();

	if (Node)
	{
		// Morph target selection
		TSharedPtr<FString> ItemToSelect;

		UCustomizableObjectNodeMaterialBase* ParentMaterialNode = Node->GetParentMaterialNode();
		if (ParentMaterialNode)
		{
			const UEdGraphPin* BaseSourcePin = FindMeshBaseSource(*ParentMaterialNode->OutputPin(), false );
			if ( BaseSourcePin )
			{
				if ( const UCustomizableObjectNodeSkeletalMesh* TypedSourceNode = Cast<UCustomizableObjectNodeSkeletalMesh>(BaseSourcePin->GetOwningNode()) )
				{
					for (int m = 0; m < TypedSourceNode->SkeletalMesh->GetMorphTargets().Num(); ++m )
					{
						FString MorphName = *TypedSourceNode->SkeletalMesh->GetMorphTargets()[m]->GetName();
						MorphTargetComboOptions.Add( MakeShareable( new FString( MorphName ) ) );

						if (Node->MorphTargetName == MorphName)
						{
							ItemToSelect = MorphTargetComboOptions.Last();
						}
					}
				}
			}
		}

        MorphTargetComboOptions.Sort(CompareNames);

		TSharedRef<IPropertyHandle> MorphTargetNameProperty = DetailBuilder.GetProperty("MorphTargetName");
		BlocksCategory.AddCustomRow( LOCTEXT("FCustomizableObjectNodeMorphMaterialDetails", "Target") )
		[
			SNew(SProperty, MorphTargetNameProperty)
			.ShouldDisplayName( false)
			.CustomWidget()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
				[
					SNew( SHorizontalBox )

					+ SHorizontalBox::Slot()
						.FillWidth(10.0f)
						.VAlign(VAlign_Center)
					[
						SNew( STextBlock )
							.Text( LOCTEXT("Morph Target","Morph Target") )
					]

					+ SHorizontalBox::Slot()
						.FillWidth(10.0f)
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Center)
					[
						SNew(STextComboBox)
							.OptionsSource( &MorphTargetComboOptions )
							.InitiallySelectedItem( ItemToSelect )
							.OnSelectionChanged(this, &FCustomizableObjectNodeMorphMaterialDetails::OnMorphTargetComboBoxSelectionChanged, MorphTargetNameProperty)
					]
				]
			]
		];
	}
	else
	{
		BlocksCategory.AddCustomRow( LOCTEXT("FCustomizableObjectNodeMorphMaterialDetails", "Node") )
		[
			SNew( STextBlock )
			.Text( LOCTEXT( "Node not found", "Node not found" ) )
		];
	}

}


void FCustomizableObjectNodeMorphMaterialDetails::OnMorphTargetComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> ParentProperty)
{	
	if (Selection.IsValid())
	{
		ParentProperty->SetValue(*Selection);	
	}
}


#undef LOCTEXT_NAMESPACE
