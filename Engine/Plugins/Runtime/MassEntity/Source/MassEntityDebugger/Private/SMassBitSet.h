// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "MassDebuggerStyle.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "MassProcessingTypes.h"

struct FMassDebuggerQueryData;

enum class EMassBitSetDisplayMode 
{
	ReadOnly,
	ReadWrite,
	MAX
};

template<typename TBitSet>
class SMassBitSet : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMassBitSet)
		: _SlotPadding(5.f)
		, _TextColor(FLinearColor(1.f, 1.f, 1.f))
		, _BackgroundBrush(FMassDebuggerStyle::GetBrush("MassDebug.Fragment"))
		{}
		SLATE_ATTRIBUTE(FMargin, SlotPadding)
		SLATE_ATTRIBUTE(FLinearColor, TextColor)
		SLATE_ATTRIBUTE(const FSlateBrush*, BackgroundBrush)
	SLATE_END_ARGS()

	void Construct(const SMassBitSet::FArguments& InArgs, const FString& Label, const TBitSet& BitSet)
	{
		Construct(InArgs, Label, MakeArrayView(&BitSet, 1));
	}

	void Construct(const SMassBitSet::FArguments& InArgs, const FString& Label, TConstArrayView<TBitSet> BitSets, TConstArrayView<const FSlateBrush*> InBrushes = TConstArrayView<const FSlateBrush*>())
	{		
		/*Content = TEXT("<LargeText>Large test</>, <RichTextBlock.Bold>Bold</>");
		Content += BitSet.DebugGetStringDesc();*/
		TSharedRef<SWrapBox> ButtonBox = SNew(SWrapBox).UseAllottedSize(true);
		
		for (int i = 0; i < BitSets.Num(); ++i)
		{
			const FSlateBrush* Brush = InBrushes.IsValidIndex(i) ? InBrushes[i] : InArgs._BackgroundBrush.Get();
			AddBitSet(InArgs, ButtonBox, BitSets[i], Brush);
		}

		ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
			]
			+ SHorizontalBox::Slot()
			[
				ButtonBox
			]
		];
	}

protected:
	void AddBitSet(const SMassBitSet::FArguments& InArgs, TSharedRef<SWrapBox>& ButtonBox, const TBitSet& BitSet, const FSlateBrush* Brush)
	{
#if WITH_MASSENTITY_DEBUG
		TArray<FName> TypeNames;
		BitSet.DebugGetIndividualNames(TypeNames);

		for (const FName& Name : TypeNames)
		{
			ButtonBox->AddSlot()
			.Padding(InArgs._SlotPadding)
			[
				SNew(SBorder)
				.ColorAndOpacity(InArgs._TextColor)
				.BorderImage(Brush)
				[
					SNew(STextBlock)
					.Text(FText::FromName(Name))
				]
			];
		}
#endif // WITH_MASSENTITY_DEBUG
	}
};

namespace UE::Mass::Debugger::UI
{
	template<typename TBitSet>
	void AddBitSet(TSharedRef<SVerticalBox>& Box, const TBitSet& BitSetAccess, const FString& Label, const FSlateBrush* Brush)
	{
		if (BitSetAccess.IsEmpty() == false)
		{
			Box->AddSlot()
			.AutoHeight()
			[
				SNew(SMassBitSet<TBitSet>, Label, BitSetAccess)
				.BackgroundBrush(Brush)
				.SlotPadding(5)
			];
		}
	}
}
