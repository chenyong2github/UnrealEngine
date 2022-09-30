// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectNodeLayoutBlocksDetails.h"
#include "CustomizableObjectEditorModule.h"
#include "PropertyCustomizationHelpers.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "LevelEditor.h"
#include "LevelEditorActions.h"

#include "Widgets/Text/STextBlock.h"

#include "Runtime/Launch/Resources/Version.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectDetails"


TSharedRef<IDetailCustomization> FCustomizableObjectNodeLayoutBlocksDetails::MakeInstance()
{
	return MakeShareable( new FCustomizableObjectNodeLayoutBlocksDetails );
}


void FCustomizableObjectNodeLayoutBlocksDetails::CustomizeDetails( IDetailLayoutBuilder& DetailBuilder )
{
	Node = 0;
	const IDetailsView* DetailsView = DetailBuilder.GetDetailsView();
	if (DetailsView->GetSelectedObjects().Num())
	{
		Node = Cast<UCustomizableObjectNodeLayoutBlocks>(DetailsView->GetSelectedObjects()[0].Get());
	}

	IDetailCategoryBuilder& BlocksCategory = DetailBuilder.EditCategory( "Blocks" );

	if (Node)
	{
		BlocksCategory.AddCustomRow( LOCTEXT("FCustomizableObjectNodeLayoutBlocksDetails", "BlockInstructions") )
		[
			SNew( STextBlock )
			.Text(  LOCTEXT("Use the node preview panel to set up the blocks.","Use the node preview panel to set up the blocks.") )
		];
	}
	else
	{
		BlocksCategory.AddCustomRow( LOCTEXT("FCustomizableObjectNodeLayoutBlocksDetails", "NodeNotFound") )
		[
			SNew( STextBlock )
			.Text( LOCTEXT( "Node not found", "Node not found" ) )
		];
	}

}


FIntPoint FCustomizableObjectNodeLayoutBlocksDetails::GetGridSize() const
{
	if (Node->Layout)
	{
		return Node->Layout->GetGridSize();
	}

	return FIntPoint(0);
}


void FCustomizableObjectNodeLayoutBlocksDetails::OnBlockChanged( int BlockIndex, FIntRect Block )
{
	if (Node->Layout)
	{
		Node->Layout->Blocks[BlockIndex].Min = Block.Min;
		Node->Layout->Blocks[BlockIndex].Max = Block.Max;
		Node->PostEditChange();
	}
}


TArray<FIntRect> FCustomizableObjectNodeLayoutBlocksDetails::GetBlocks() const
{
	TArray<FIntRect> Blocks;

	if (Node->Layout)
	{
		Blocks.SetNum(Node->Layout->Blocks.Num());

		for (int BlockIndex = 0; BlockIndex < Node->Layout->Blocks.Num(); ++BlockIndex)
		{
			Blocks[BlockIndex] = FIntRect(Node->Layout->Blocks[BlockIndex].Min, Node->Layout->Blocks[BlockIndex].Max);
		}
	}

	return Blocks;
}


void FCustomizableObjectNodeLayoutBlocksDetails::OnGridComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo)
{
	/*
	if (CustomInstance)
	{
		mu::Model* Model = CustomInstance->CustomizableObject->GetModel();

		CustomInstance->PreEditChange(NULL);
		CustomInstance->State = Model->FindState( TCHAR_TO_ANSI(**Selection) );
		CustomInstance->PostEditChange();

		ResetParamBox();
	}
	*/
}

#undef LOCTEXT_NAMESPACE
