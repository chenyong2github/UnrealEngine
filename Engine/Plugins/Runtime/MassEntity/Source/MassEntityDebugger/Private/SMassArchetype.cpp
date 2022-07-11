// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMassArchetype.h"
#include "MassDebuggerModel.h"
#include "MassEntityTypes.h"
#include "SMassBitSet.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SMassDebugger"

//----------------------------------------------------------------------//
// SMassArchetype
//----------------------------------------------------------------------//
void SMassArchetype::Construct(const FArguments& InArgs, TSharedPtr<FMassDebuggerArchetypeData> InArchetypeData)
{
	using UE::Mass::Debugger::UI::AddBitSet;

	if (!InArchetypeData)
	{
		return;
	}
	
	ArchetypeData = InArchetypeData;

	FMassDebuggerArchetypeData& ArchetypeDebugData = *InArchetypeData.Get();

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	const TArray<FText> LabelBits = {
		LOCTEXT("MassArchetypeLabel", "Archetype")
		, FText::FromString(InArchetypeData->Label)
	};

	Box->AddSlot()
	.AutoHeight()
	[
		SNew(SRichTextBlock)
		.Text(FText::Join(FText::FromString(TEXT(": ")), LabelBits))
		.DecoratorStyleSet(&FAppStyle::Get())
		.TextStyle(FAppStyle::Get(), "LargeText")
	];

	FString ArchetypeDescription = FString::Printf(TEXT("EntitiesCount: %d\nEntitiesCountPerChunk: %d\nChunksCount: %d")
		, ArchetypeDebugData.EntitiesCount
		, ArchetypeDebugData.EntitiesCountPerChunk
		, ArchetypeDebugData.ChunksCount);
	
	Box->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
		.Text(FText::FromString(ArchetypeDescription))
	];

	const FMassArchetypeCompositionDescriptor& Composition = ArchetypeData->Composition;
	const FSlateBrush* Brush = FMassDebuggerStyle::GetBrush("MassDebug.Fragment");

	AddBitSet(Box, Composition.Fragments, TEXT("Fragments"), Brush);
	AddBitSet(Box, Composition.Tags, TEXT("Tags"), Brush);
	AddBitSet(Box, Composition.ChunkFragments, TEXT("Chunk Fragments"), Brush);
	AddBitSet(Box, Composition.SharedFragments, TEXT("Shared Fragments"), Brush);

	ChildSlot
	[
		SNew(SBorder)
		.Padding(5.0f)
		[
			Box
		]
	];
	
}

#undef LOCTEXT_NAMESPACE

