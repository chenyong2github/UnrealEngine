// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMassProcessor.h"
#include "MassDebuggerModel.h"
#include "MassDebuggerStyle.h"
#include "SMassQuery.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/AppStyle.h"


#define LOCTEXT_NAMESPACE "SMassDebugger"


//----------------------------------------------------------------------//
// SMassQueryTableRow
//----------------------------------------------------------------------//
using FMassDebuggerQueryDataPtr = TSharedPtr<FMassDebuggerQueryData, ESPMode::ThreadSafe>;

class SMassQueryTableRow : public STableRow<FMassDebuggerQueryDataPtr>
{
public:
	SLATE_BEGIN_ARGS(SMassQueryTableRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const FMassDebuggerQueryDataPtr InEntryItem)
	{
		Item = InEntryItem;

		STableRow<FMassDebuggerQueryDataPtr>::Construct(STableRow<FMassDebuggerQueryDataPtr>::FArguments(), InOwnerTableView);

		ChildSlot
			[
				SNew(SMassQuery, Item)
			];
	}

	FMassDebuggerQueryDataPtr Item;
};

//----------------------------------------------------------------------//
// SMassProcessor
//----------------------------------------------------------------------//
void SMassProcessor::Construct(const FArguments& InArgs, TSharedPtr<FMassDebuggerProcessorData> InProcessorData)
{
	ProcessorData = InProcessorData;
	if (!ProcessorData)
	{
		return;
	}

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot()
	.AutoHeight()
	[
		SNew(SBorder)
		.Padding(FMargin(10, 5))
		[
			SNew(SRichTextBlock)
			.Text(FText::FromString(ProcessorData->Label))
			.DecoratorStyleSet(&FAppStyle::Get())
			.TextStyle(FAppStyle::Get(), "LargeText")
		]
	];
	
	for (TSharedPtr<FMassDebuggerQueryData>& QueryData : ProcessorData->Queries)
	{
		Box->AddSlot()
		.AutoHeight()
		[
			SNew(SMassQuery, QueryData)
		];
	}

	ChildSlot
	[
		Box
	];
}

#undef LOCTEXT_NAMESPACE

