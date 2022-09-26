// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenerateMutableSourceLayout.h"

#include "GenerateMutableSource/GenerateMutableSourceFloat.h"

#include "Templates/UnrealTemplate.h"
#include "Engine/AssetManager.h"
#include "Engine/TextureLODSettings.h"
#include "ClothingAsset.h"
#include "EdGraphToken.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UnrealEditorPortabilityHelpers.h"
#include "PlatformInfo.h"

#include "CustomizableObjectCompiler.h"
#include "UnrealToMutableTextureConversionUtils.h"
#include "UnrealConversionUtils.h"
#include "CustomizableObjectEditorModule.h"
#include "CustomizableObject.h"
#include "GraphTraversal.h"

#include "MutableTools/Public/Node.h"
#include "MutableTools/Public/NodeLayout.h"
#include "MutableTools/Public/Compiler.h"
#include "MutableTools/Public/Streams.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


mu::NodeLayoutPtr GenerateMutableSourceLayout(const UEdGraphPin * Pin, FMutableGraphGenerationContext & GenerationContext)
{
	check(Pin)
	RETURN_ON_CYCLE(*Pin, GenerationContext)

	CheckNumOutputs(*Pin, GenerationContext);

	UCustomizableObjectNode* Node = CastChecked<UCustomizableObjectNode>(Pin->GetOwningNode());

	const FGeneratedKey Key(reinterpret_cast<void*>(&GenerateMutableSourceLayout), *Pin, *Node, GenerationContext, true);
	if (const FGeneratedData* Generated = GenerationContext.Generated.Find(Key))
	{
		return static_cast<mu::NodeLayout*>(Generated->Node.get());
	}

	mu::NodeLayoutPtr Result;
	
	if (const UCustomizableObjectNodeLayoutBlocks* TypedNodeBlocks = Cast<UCustomizableObjectNodeLayoutBlocks>(Node))
	{
		if (UCustomizableObjectNodeSkeletalMesh* SkeletalMeshNode = Cast<UCustomizableObjectNodeSkeletalMesh>(FollowOutputPin(*TypedNodeBlocks->OutputPin())->GetOwningNode()))
		{
			int32 LayoutIndex;
			FString MaterialName;

			if (!SkeletalMeshNode->CheckIsValidLayout(Pin, LayoutIndex, MaterialName))
			{
				FString msg = "Layouts ";
				for (int32 i = 0; i < LayoutIndex; ++i)
				{
					msg += "UV" + FString::FromInt(i);
					if (i < LayoutIndex - 1)
					{
						msg += ", ";
					}
				}
				msg += " of " + MaterialName + " must be also connected to a Layout Blocks Node. ";
				GenerationContext.Compiler->CompilerLog(FText::FromString(msg), Node, EMessageSeverity::Error);
				return nullptr;
			}
		}

		mu::NodeLayoutBlocksPtr LayoutNode = new mu::NodeLayoutBlocks;
		Result = LayoutNode;

		LayoutNode->SetGridSize(TypedNodeBlocks->Layout->GetGridSize().X, TypedNodeBlocks->Layout->GetGridSize().Y);
		LayoutNode->SetMaxGridSize(TypedNodeBlocks->Layout->GetMaxGridSize().X, TypedNodeBlocks->Layout->GetMaxGridSize().Y);
		LayoutNode->SetBlockCount(TypedNodeBlocks->Layout->Blocks.Num());

		mu::EPackStrategy strategy = mu::EPackStrategy::RESIZABLE_LAYOUT;

		switch (TypedNodeBlocks->Layout->GetPackingStrategy())
		{
		case ECustomizableObjectTextureLayoutPackingStrategy::Resizable:
			strategy = mu::EPackStrategy::RESIZABLE_LAYOUT;
			break;
		case ECustomizableObjectTextureLayoutPackingStrategy::Fixed:
			strategy = mu::EPackStrategy::FIXED_LAYOUT;
			break;
		default:
			break;
		}

		LayoutNode->SetLayoutPackingStrategy(strategy);

		for (int BlockIndex = 0; BlockIndex < TypedNodeBlocks->Layout->Blocks.Num(); ++BlockIndex)
		{
			LayoutNode->SetBlock(BlockIndex,
				TypedNodeBlocks->Layout->Blocks[BlockIndex].Min.X,
				TypedNodeBlocks->Layout->Blocks[BlockIndex].Min.Y,
				TypedNodeBlocks->Layout->Blocks[BlockIndex].Max.X - TypedNodeBlocks->Layout->Blocks[BlockIndex].Min.X,
				TypedNodeBlocks->Layout->Blocks[BlockIndex].Max.Y - TypedNodeBlocks->Layout->Blocks[BlockIndex].Min.Y);

			LayoutNode->SetBlockPriority(BlockIndex, TypedNodeBlocks->Layout->Blocks[BlockIndex].Priority);
		}
	}
	
	else
	{
		GenerationContext.Compiler->CompilerLog(LOCTEXT("UnimplementedNode", "Node type not implemented yet."), Node);
	}

	GenerationContext.Generated.Add(Key, FGeneratedData(Node, Result));
	GenerationContext.GeneratedNodes.Add(Node);

	
	if (Result)
	{
		Result->SetMessageContext(Node);
	}

	return Result;
}
