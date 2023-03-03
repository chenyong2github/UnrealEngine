// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMovieGraphMembersTabContent.h"

#include "EdGraph/EdGraphSchema.h"
#include "Framework/Commands/GenericCommands.h"
#include "Graph/MovieGraphConfig.h"
#include "Graph/MovieGraphSchema.h"
#include "SGraphActionMenu.h"
#include "Toolkits/AssetEditorToolkit.h"

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
	EditorToolkit = InArgs._Editor;
	CurrentGraph = InArgs._Graph;
	OnActionSelected = InArgs._OnActionSelected;
	
	ChildSlot
	[
		SAssignNew(ActionMenu, SGraphActionMenu)
		.OnActionSelected(OnActionSelected)
		.AutoExpandActionMenu(true)
		.OnCollectStaticSections(this, &SMovieGraphMembersTabContent::CollectStaticSections)
		.OnContextMenuOpening(this, &SMovieGraphMembersTabContent::OnContextMenuOpening)
		.OnGetSectionTitle(this, &SMovieGraphMembersTabContent::GetSectionTitle)
		.OnGetSectionWidget(this, &SMovieGraphMembersTabContent::GetSectionWidget)
		.UseSectionStyling(true)
		.OnCollectAllActions(this, &SMovieGraphMembersTabContent::CollectAllActions)
	];
}

void SMovieGraphMembersTabContent::ClearSelection() const
{
	if (ActionMenu.IsValid())
	{
		ActionMenu->SelectItemByName(NAME_None);
	}
}

void SMovieGraphMembersTabContent::DeleteSelectedMembers() const
{
	if (!ActionMenu.IsValid() || !CurrentGraph)
	{
		return;
	}

	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	ActionMenu->GetSelectedActions(SelectedActions);
	for (TSharedPtr<FEdGraphSchemaAction> SelectedAction : SelectedActions)
	{
		if (const FMovieGraphSchemaAction* MovieGraphAction = static_cast<FMovieGraphSchemaAction*>(SelectedAction.Get()))
		{
			CurrentGraph->DeleteMember(MovieGraphAction->ActionTarget.Get());
		}
	}

	RefreshActions();
}

bool SMovieGraphMembersTabContent::CanDeleteSelectedMembers() const
{
	if (!ActionMenu.IsValid())
	{
		return false;
	}

	// There may need to be some more extensive validation here in the future
	return true;
}

void SMovieGraphMembersTabContent::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	if (!CurrentGraph)
	{
		return;
	}
	
	// TODO: These are placeholder actions and need to be dynamically populated by what is in the graph

	FGraphActionMenuBuilder ActionMenuBuilder;
	
	const FText InputActionDesc = LOCTEXT("InputAction", "This is an input");
	const FText InputActionCategory;
	const FText InputActionDescription = LOCTEXT("InputActionTooltip", "This is an input tooltip.");
	const FText InputActionKeywords;
	const int32 InputActionSectionID = static_cast<int32>(EActionSection::Inputs);
	const TSharedPtr<FMovieGraphSchemaAction> InputAction(new FMovieGraphSchemaAction(InputActionCategory, InputActionDesc, InputActionDescription, 0, InputActionKeywords, InputActionSectionID));

	const FText OutputActionDesc = LOCTEXT("OutputAction", "This is an output");
	const FText OutputActionCategory;
	const FText OutputActionDescription = LOCTEXT("OutputActionTooltip", "This is an output tooltip.");
	const FText OutputActionKeywords;
	const int32 OutputActionSectionID = static_cast<int32>(EActionSection::Outputs);
	const TSharedPtr<FMovieGraphSchemaAction> OutputAction(new FMovieGraphSchemaAction(OutputActionCategory, OutputActionDesc, OutputActionDescription, 0, OutputActionKeywords, OutputActionSectionID));

	for (UMovieGraphVariable* Variable : CurrentGraph->GetVariables())
	{
		if (!Variable)
		{
			continue;
		}
		
		const FText VariableActionDesc = FText::FromString(Variable->Name);
		const FText VariableActionCategory;
		const FText VariableActionTooltip;
		const FText VariableActionKeywords;
		const int32 VariableActionSectionID = static_cast<int32>(EActionSection::Variables);
		const TSharedPtr<FMovieGraphSchemaAction> VariableAction(new FMovieGraphSchemaAction(VariableActionCategory, VariableActionDesc, VariableActionTooltip, 0, VariableActionKeywords, VariableActionSectionID));
		VariableAction->ActionTarget = Variable;
		ActionMenuBuilder.AddAction(VariableAction);

		// Update actions when a variable is renamed
		Variable->OnMovieGraphVariableChangedDelegate.AddSP(this, &SMovieGraphMembersTabContent::RefreshActions);
	}
	
	ActionMenuBuilder.AddAction(InputAction);
	ActionMenuBuilder.AddAction(OutputAction);
	
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
		.OnClicked(this, &SMovieGraphMembersTabContent::OnAddButtonClickedOnSection, InSectionID)
		.ContentPadding(FMargin(1, 0))
		.ToolTipText(ButtonTooltip)
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

TSharedPtr<SWidget> SMovieGraphMembersTabContent::OnContextMenuOpening()
{
	if (!ActionMenu.IsValid())
	{
		return nullptr;
	}

	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	ActionMenu->GetSelectedActions(SelectedActions);
	if (SelectedActions.IsEmpty())
	{
		return nullptr;
	}

	TSharedPtr< FAssetEditorToolkit> PinnedToolkit = EditorToolkit.Pin();
	if(!PinnedToolkit.IsValid())
	{
		return nullptr;
	}

	FMenuBuilder MenuBuilder(true, PinnedToolkit->GetToolkitCommands());
	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
	
	return MenuBuilder.MakeWidget();
}

FReply SMovieGraphMembersTabContent::OnAddButtonClickedOnSection(const int32 InSectionID)
{
	const EActionSection Section = static_cast<EActionSection>(InSectionID);
	
	if (Section == EActionSection::Variables)
	{
		CurrentGraph->AddVariable();
	}

	RefreshActions();

	return FReply::Handled();
}

void SMovieGraphMembersTabContent::RefreshActions(UMovieGraphVariable* UpdatedVariable) const
{
	// Currently the entire action menu is refreshed rather than a specific action being targeted
	
	const bool bPreserveExpansion = true;
	ActionMenu->RefreshAllActions(bPreserveExpansion);
}

#undef LOCTEXT_NAMESPACE