// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectLayout.h"
#include "CustomizableObjectCompiler.h"
#include "ICustomizableObjectEditor.h"
#include "GenerateMutableSource/GenerateMutableSource.h"
#include "GraphTraversal.h"

#include "Nodes/CustomizableObjectNode.h"
#include "Nodes/CustomizableObjectNodeSkeletalMesh.h"
#include "Nodes/CustomizableObjectNodeTable.h"
#include "Nodes/CustomizableObjectNodeLayoutBlocks.h"

#include "GenerateMutableSource/GenerateMutableSourceMesh.h"

#include "MutableTools/Public/NodeLayout.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"

UCustomizableObjectLayout::UCustomizableObjectLayout()
{
	GridSize = FIntPoint(4, 4);
	MaxGridSize = FIntPoint(4, 4);

	FCustomizableObjectLayoutBlock Block;
	Block.Min = FIntPoint(0, 0);
	Block.Max = FIntPoint(4, 4);
	Block.Id = FGuid::NewGuid();
	Block.Priority = 0;
	Blocks.Add(Block);

	PackingStrategy = ECustomizableObjectTextureLayoutPackingStrategy::Resizable;
}


void UCustomizableObjectLayout::SetLayout(UObject* InMesh, int32 LODIndex, int32 MatIndex, int32 UVIndex)
{
	Mesh = InMesh;
	LOD = LODIndex;
	Material = MatIndex;
	UVChannel = UVIndex;
}


void UCustomizableObjectLayout::SetPackingStrategy(ECustomizableObjectTextureLayoutPackingStrategy Strategy)
{
	PackingStrategy = Strategy;
}


void UCustomizableObjectLayout::SetGridSize(FIntPoint Size)
{
	GridSize = Size;
}


void UCustomizableObjectLayout::SetMaxGridSize(FIntPoint Size)
{
	MaxGridSize = Size;
}


void UCustomizableObjectLayout::SetLayoutName(FString Name)
{
	LayoutName = Name;
}


void UCustomizableObjectLayout::GenerateBlocksFromUVs()
{
	mu::NodeLayoutBlocksPtr Layout = nullptr;

	UCustomizableObjectNode* Node = Cast<UCustomizableObjectNode>(GetOuter());

	if (Node && Mesh)
	{
		//Creating a GenerationContext
		FCustomizableObjectCompiler* Compiler = new FCustomizableObjectCompiler();
		UCustomizableObject* Object = Node->GetGraphEditor()->GetCustomizableObject();
		FCompilationOptions Options = Object->CompileOptions;
	
		FMutableGraphGenerationContext GenerationContext(Object, Compiler, Options);
	
		//Transforming skeletalmesh to mutable mesh
		mu::MeshPtr	MutableMesh = nullptr;
		
		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh))
		{
			GenerationContext.ComponentInfos.Add(SkeletalMesh);
			MutableMesh = ConvertSkeletalMeshToMutable(SkeletalMesh, LOD, Material, GenerationContext, Node);
		}
		else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh))
		{
			MutableMesh = ConvertStaticMeshToMutable(StaticMesh, LOD, Material, GenerationContext, Node);
		}
	
		if (MutableMesh)
		{
			//Generating blocks with the mutable mesh
			Layout = mu::NodeLayoutBlocks::GenerateLayoutBlocks(MutableMesh, UVChannel, GridSize.X, GridSize.Y);
		}
	
		delete Compiler;
	
		if (Layout)
		{
			Blocks.Empty();
		
			//Generating the layout blocks with the mutable layout		
			for (int i = 0; i < Layout->GetBlockCount(); ++i)
			{
				int minX, minY, sizeX, sizeY;
		
				Layout->GetBlock(i, &minX, &minY, &sizeX, &sizeY);
		
				FCustomizableObjectLayoutBlock block;
				block.Min = FIntPoint(minX, minY);
				block.Max = FIntPoint(minX + sizeX, minY + sizeY);
				block.Id = FGuid::NewGuid();
				block.Priority = 0;
				Blocks.Add(block);
			}
		
			Node->PostEditChange();
			Node->GetGraph()->MarkPackageDirty();
		}
	}
}


void UCustomizableObjectLayout::GetUVChannel(TArray<FVector2f>& UVs, int32 UVChannelIndex) const
{
	if (UObject* Node = GetOuter())
	{
		if (const UCustomizableObjectNodeLayoutBlocks* TypedNodeLayout = Cast<UCustomizableObjectNodeLayoutBlocks>(Node))
		{
			if (const UEdGraphPin* ConnectedPin = FollowOutputPin(*TypedNodeLayout->OutputPin()))
			{
				if (const UCustomizableObjectNodeMesh* MeshNode = Cast<UCustomizableObjectNodeMesh>(ConnectedPin->GetOwningNode()))
				{
					MeshNode->GetUVChannelForPin(ConnectedPin, UVs, UVChannelIndex);
				}
			}
		}
		else if (const UCustomizableObjectNodeTable* TypedNodeTable = Cast<UCustomizableObjectNodeTable>(Node))
		{
			TypedNodeTable->GetUVChannel(this, UVs);
		}
	}
}


int32 UCustomizableObjectLayout::FindBlock(const FGuid& InId) const
{
	for (int Index = 0; Index < Blocks.Num(); ++Index)
	{
		if (Blocks[Index].Id == InId)
		{
			return Index;
		}
	}

	return -1;
}

#undef LOCTEXT_NAMESPACE
