// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMovieGraphMembersTabContent.h"

#include "EdGraph/EdGraphSchema.h"
#include "SGraphActionMenu.h"

#define LOCTEXT_NAMESPACE "SMoviePipelineMembersTabContent"

const TArray<FText> SMovieGraphMembersTabContent::ActionMenuSectionNames
{
	LOCTEXT("ActionMenuSectionName_Invalid", "INVALID"),
	LOCTEXT("ActionMenuSectionName_Inputs", "Inputs"),
	LOCTEXT("ActionMenuSectionName_Outputs", "Outputs"),
	LOCTEXT("ActionMenuSectionName_Variables", "Variables")
};

void SMovieGraphMembersTabContent::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SGraphActionMenu)
		.AutoExpandActionMenu(true)
		.OnCollectStaticSections(this, &SMovieGraphMembersTabContent::CollectStaticSections)
		.OnGetSectionTitle(this, &SMovieGraphMembersTabContent::GetSectionTitle)
		.OnGetSectionWidget(this, &SMovieGraphMembersTabContent::GetSectionWidget)
		.UseSectionStyling(true)
		.OnCollectAllActions(this, &SMovieGraphMembersTabContent::CollectAllActions)
	];
}

void SMovieGraphMembersTabContent::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	// TODO: These are placeholder actions and need to be dynamically populated by what is in the graph
	
	const FText InputActionDesc = LOCTEXT("InputAction", "This is an input");
	const FText InputActionCategory;
	const FText InputActionDescription = LOCTEXT("InputActionTooltip", "This is an input tooltip.");
	const FText InputActionKeywords;
	const int32 InputActionSectionID = 1;
	const TSharedPtr<FEdGraphSchemaAction> InputAction(new FEdGraphSchemaAction(InputActionCategory, InputActionDesc, InputActionDescription, 0, InputActionKeywords, InputActionSectionID));

	const FText OutputActionDesc = LOCTEXT("OutputAction", "This is an output");
	const FText OutputActionCategory;
	const FText OutputActionDescription = LOCTEXT("OutputActionTooltip", "This is an output tooltip.");
	const FText OutputActionKeywords;
	const int32 OutputActionSectionID = 2;
	const TSharedPtr<FEdGraphSchemaAction> OutputAction(new FEdGraphSchemaAction(OutputActionCategory, OutputActionDesc, OutputActionDescription, 0, OutputActionKeywords, OutputActionSectionID));

	const FText VariableActionDesc = LOCTEXT("VariableAction", "This is a variable");
	const FText VariableActionCategory;
	const FText VariableActionDescription = LOCTEXT("VariableActionTooltip", "This is a variable tooltip.");
	const FText VariableActionKeywords;
	const int32 VariableActionSectionID = 3;
	const TSharedPtr<FEdGraphSchemaAction> VariableAction(new FEdGraphSchemaAction(VariableActionCategory, VariableActionDesc, VariableActionDescription, 0, VariableActionKeywords, VariableActionSectionID));
	
	FGraphActionMenuBuilder ActionMenuBuilder;
	ActionMenuBuilder.AddAction(InputAction);
	ActionMenuBuilder.AddAction(OutputAction);
	ActionMenuBuilder.AddAction(VariableAction);
	
	OutAllActions.Append(ActionMenuBuilder);
}

void SMovieGraphMembersTabContent::CollectStaticSections(TArray<int32>& StaticSectionIDs)
{
	// Start at index 1 to skip the invalid section
	for (int32 Index = 1; Index < static_cast<int32>(EActionSection::COUNT); ++Index)
	{
		StaticSectionIDs.Add(Index);
	}
}

FText SMovieGraphMembersTabContent::GetSectionTitle(int32 InSectionID)
{
	if (ensure(ActionMenuSectionNames.IsValidIndex(InSectionID)))
	{
		return ActionMenuSectionNames[InSectionID];
	}

	return FText::GetEmpty();
}

TSharedRef<SWidget> SMovieGraphMembersTabContent::GetSectionWidget(TSharedRef<SWidget> RowWidget, int32 InSectionID)
{
	FText ButtonTooltip;

	switch (static_cast<EActionSection>(InSectionID))
	{
	case EActionSection::Inputs:
		ButtonTooltip = LOCTEXT("ButtonTooltip_Inputs", "Add Input");
		break;

	case EActionSection::Outputs:
		ButtonTooltip = LOCTEXT("ButtonTooltip_Outputs", "Add Output");
		break;

	case EActionSection::Variables:
		ButtonTooltip = LOCTEXT("ButtonTooltip_Variables", "Add Variable");
		break;

	default:
		return SNullWidget::NullWidget;
	}
	
	return
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(1, 0))
		.ToolTipText(ButtonTooltip)
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

#undef LOCTEXT_NAMESPACE