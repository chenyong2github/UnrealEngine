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
		.AlphaSortItems(false)
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
			UMovieGraphMember* GraphMember = Cast<UMovieGraphMember>(MovieGraphAction->ActionTarget.Get());
			CurrentGraph->DeleteMember(GraphMember);
		}
	}

	RefreshMemberActions();
}

bool SMovieGraphMembersTabContent::CanDeleteSelectedMembers() const
{
	if (!ActionMenu.IsValid())
	{
		return false;
	}

	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	ActionMenu->GetSelectedActions(SelectedActions);
	
	for (const TSharedPtr<FEdGraphSchemaAction>& SelectedAction : SelectedActions)
	{
		const FMovieGraphSchemaAction* MovieAction = static_cast<FMovieGraphSchemaAction*>(SelectedAction.Get());

		// Don't allow deletion if the member was explicitly marked as non-deletable
		if (const UMovieGraphMember* Member = Cast<UMovieGraphMember>(MovieAction->ActionTarget))
		{
			if (!Member->IsDeletable())
			{
				return false;
			}
		}
	}

	return true;
}

void SMovieGraphMembersTabContent::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	static const FText UserVariablesCategory = LOCTEXT("UserVariablesCategory", "User Variables");
	static const FText GlobalVariablesCategory = LOCTEXT("GlobalVariablesCategory", "Global Variables");
	static const FText EmptyCategory = FText::GetEmpty();
	
	if (!CurrentGraph)
	{
		return;
	}

	FGraphActionMenuBuilder ActionMenuBuilder;

	// Creates a new action in the action menu under a specific section w/ the provided action target
	auto AddToActionMenu = [&ActionMenuBuilder](UMovieGraphMember* ActionTarget, const EActionSection Section, const FText& Category) -> void
	{
		const FText MemberActionDesc = FText::FromString(ActionTarget->Name);
		const FText MemberActionTooltip;
		const FText MemberActionKeywords;
		const int32 MemberActionSectionID = static_cast<int32>(Section);
		const TSharedPtr<FMovieGraphSchemaAction> MemberAction(new FMovieGraphSchemaAction(Category, MemberActionDesc, MemberActionTooltip, 0, MemberActionKeywords, MemberActionSectionID));
		MemberAction->ActionTarget = ActionTarget;
		ActionMenuBuilder.AddAction(MemberAction);
	};

	for (UMovieGraphInput* Input : CurrentGraph->GetInputs())
	{
		if (Input && Input->IsDeletable())
		{
			AddToActionMenu(Input, EActionSection::Inputs, EmptyCategory);
            
            // Update actions when an input is updated (renamed, etc)
            Input->OnMovieGraphInputChangedDelegate.AddSP(this, &SMovieGraphMembersTabContent::RefreshMemberActions);
		}
	}

	for (UMovieGraphOutput* Output : CurrentGraph->GetOutputs())
	{
		if (Output && Output->IsDeletable())
		{
			AddToActionMenu(Output, EActionSection::Outputs, EmptyCategory);

			// Update actions when an output is updated (renamed, etc)
			Output->OnMovieGraphOutputChangedDelegate.AddSP(this, &SMovieGraphMembersTabContent::RefreshMemberActions);
		}
	}

	const bool bIncludeGlobal = true;
	const TArray<UMovieGraphVariable*> AllVariables = CurrentGraph->GetVariables(bIncludeGlobal);

	// Add non-global variables first
	for (UMovieGraphVariable* Variable : AllVariables)
	{
		if (Variable && !Variable->IsGlobal())
		{
			AddToActionMenu(Variable, EActionSection::Variables, UserVariablesCategory);

			// Update actions when a variable is updated (renamed, etc)
			Variable->OnMovieGraphVariableChangedDelegate.AddSP(this, &SMovieGraphMembersTabContent::RefreshMemberActions);
		}
	}

	// Add global variables after user-declared variables
	for (UMovieGraphVariable* Variable : AllVariables)
	{
		if (Variable && Variable->IsGlobal())
		{
			AddToActionMenu(Variable, EActionSection::Variables, GlobalVariablesCategory);
		}
	}
	
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

	if (Section == EActionSection::Inputs)
	{
		CurrentGraph->AddInput();
	}
	else if (Section == EActionSection::Outputs)
	{
		CurrentGraph->AddOutput();
	}
	else if (Section == EActionSection::Variables)
	{
		CurrentGraph->AddVariable();
	}

	RefreshMemberActions();

	return FReply::Handled();
}

void SMovieGraphMembersTabContent::RefreshMemberActions(UMovieGraphMember* UpdatedMember) const
{
	// Currently the entire action menu is refreshed rather than a specific action being targeted
	
	const bool bPreserveExpansion = true;
	ActionMenu->RefreshAllActions(bPreserveExpansion);
}

#undef LOCTEXT_NAMESPACE