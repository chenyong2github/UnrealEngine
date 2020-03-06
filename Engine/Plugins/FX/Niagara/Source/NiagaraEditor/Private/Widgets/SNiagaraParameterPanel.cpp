// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraParameterPanel.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraParameterPanelViewModel.h"
#include "Framework/Commands/UICommandList.h"
#include "SGraphActionMenu.h"
#include "NiagaraActions.h"
#include "NiagaraEditorUtilities.h"
#include "Framework/Commands/GenericCommands.h"
#include "DetailLayoutBuilder.h"
#include "Widgets/Input/SSearchBox.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "SNiagaraParameterPanelPaletteItem.h"
#include "SNiagaraGraphActionWidget.h"
#include "NiagaraScriptVariable.h"

#define LOCTEXT_NAMESPACE "NiagaraParameterPanel"

SNiagaraParameterPanel::~SNiagaraParameterPanel()
{
	ParameterPanelViewModel->GetOnRefreshed().Unbind();

	// Unregister all commands for right click on action node
	ToolkitCommands->UnmapAction(FNiagaraParameterPanelCommands::Get().DeleteEntry);
	ToolkitCommands->UnmapAction(FGenericCommands::Get().Rename);
}

void FNiagaraParameterPanelCommands::RegisterCommands()
{
	UI_COMMAND(DeleteEntry, "Delete", "Delete this parameter", EUserInterfaceActionType::Button, FInputChord(EKeys::Platform_Delete));
}

void SNiagaraParameterPanel::Construct(const FArguments& InArgs, const TSharedPtr<INiagaraParameterPanelViewModel>& InParameterPanelViewModel, const TSharedPtr<FUICommandList>& InToolkitCommands)
{
	ParameterPanelViewModel = InParameterPanelViewModel;
	ToolkitCommands = InToolkitCommands;

	ParameterPanelViewModel->GetOnRefreshed().BindRaw(this, &SNiagaraParameterPanel::Refresh);

	AddParameterButtons.SetNum(NiagaraParameterPanelSectionID::PARTICLES + 1); //@Todo(ng) verify

	// Register all commands for right click on action node
	FNiagaraParameterPanelCommands::Register();
	TSharedPtr<FUICommandList> ToolKitCommandList = ToolkitCommands;
	ToolKitCommandList->MapAction(FNiagaraParameterPanelCommands::Get().DeleteEntry,
		FExecuteAction::CreateSP(this, &SNiagaraParameterPanel::TryDeleteEntries),
		FCanExecuteAction::CreateSP(this, &SNiagaraParameterPanel::CanTryDeleteEntries));
	ToolKitCommandList->MapAction(FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SNiagaraParameterPanel::OnRequestRenameOnActionNode),
		FCanExecuteAction::CreateSP(this, &SNiagaraParameterPanel::CanRequestRenameOnActionNode));

	// create the main action list piece of this widget
	SAssignNew(GraphActionMenu, SGraphActionMenu, false)
		.OnCreateWidgetForAction(this, &SNiagaraParameterPanel::OnCreateWidgetForAction)
		.OnCollectAllActions(this, &SNiagaraParameterPanel::CollectAllActions)
 		.OnCollectStaticSections(this, &SNiagaraParameterPanel::CollectStaticSections)
 		.OnActionDragged(this, &SNiagaraParameterPanel::OnActionDragged)
		.OnActionSelected(this, &SNiagaraParameterPanel::OnActionSelected)
// 		.OnActionDoubleClicked(this, &SNiagaraParameterMapView::OnActionDoubleClicked) //@todo(ng) impl
// 		.OnContextMenuOpening(this, &SNiagaraParameterMapView::OnContextMenuOpening)
// 		.OnCanRenameSelectedAction(this, &SNiagaraParameterMapView::CanRequestRenameOnActionNode)
 		.OnGetSectionTitle(this, &SNiagaraParameterPanel::OnGetSectionTitle)
 		.OnGetSectionWidget(this, &SNiagaraParameterPanel::OnGetSectionWidget)
 		.OnCreateCustomRowExpander_Static(&SNiagaraParameterPanel::CreateCustomActionExpander)
		.OnActionMatchesName(this, &SNiagaraParameterPanel::HandleActionMatchesName)
		.AutoExpandActionMenu(false)
		.AlphaSortItems(false)
		.UseSectionStyling(true)
		.ShowFilterTextBox(true);

	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(300)
		[
			SNew(SVerticalBox)	
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				GraphActionMenu.ToSharedRef()
			]
		]
	];
}

void SNiagaraParameterPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bNeedsRefresh)
	{
		GraphActionMenu->RefreshAllActions(true);
		bNeedsRefresh = false;
	}
}

void SNiagaraParameterPanel::AddParameter(FNiagaraVariable NewVariable, const NiagaraParameterPanelSectionID::Type SectionID)
{
	FNiagaraVariableMetaData NewVariableMetaData = FNiagaraVariableMetaData();
	NewVariableMetaData.Scope = NiagaraParameterPanelSectionID::GetScopeForNewParametersInSection(SectionID);
	NewVariableMetaData.Usage = NiagaraParameterPanelSectionID::GetUsageForNewParametersInSection(SectionID);

	ParameterPanelViewModel->AddParameter(NewVariable, NewVariableMetaData);

	GraphActionMenu->RefreshAllActions(true);
	GraphActionMenu->SelectItemByName(NewVariable.GetName());
	GraphActionMenu->OnRequestRenameOnActionNode();
}

bool SNiagaraParameterPanel::AllowMakeType(const FNiagaraTypeDefinition& InType) const
{
	return InType != FNiagaraTypeDefinition::GetParameterMapDef() && InType != FNiagaraTypeDefinition::GetGenericNumericDef();
}

TSharedRef<SWidget> SNiagaraParameterPanel::OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData)
{
	TSharedPtr<SWidget> ParameterVisualWidget = ParameterPanelViewModel->GetScriptParameterVisualWidget(InCreateData);
	return SNew(SNiagaraParameterPanelPaletteItem, InCreateData, ParameterVisualWidget->AsShared());
}

void SNiagaraParameterPanel::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	const FText TooltipFormat = LOCTEXT("Parameters", "Name: {0} \nType: {1}"); //@todo(ng) move to const static

	TArray<FNiagaraScriptVariableAndViewInfo> VarAndViewInfos = ParameterPanelViewModel->GetViewedParameters();
	VarAndViewInfos.Sort([](const FNiagaraScriptVariableAndViewInfo& A, const FNiagaraScriptVariableAndViewInfo& B) { return A.MetaData.CachedNamespacelessVariableName.LexicalLess(B.MetaData.CachedNamespacelessVariableName); });
	for (const FNiagaraScriptVariableAndViewInfo& VarAndViewInfo : VarAndViewInfos)
	{
		const FText Name = FText::FromName(VarAndViewInfo.ScriptVariable.GetName());
		const FText Tooltip = FText::Format(TooltipFormat, FText::FromName(VarAndViewInfo.ScriptVariable.GetName()), VarAndViewInfo.ScriptVariable.GetType().GetNameText());
		const NiagaraParameterPanelSectionID::Type Section = ParameterPanelViewModel->GetSectionForVarAndViewInfo(VarAndViewInfo);
		TSharedPtr<FNiagaraScriptVarAndViewInfoAction> ScriptVarAndViewInfoAction(new FNiagaraScriptVarAndViewInfoAction(VarAndViewInfo, FText::GetEmpty(), Name, Tooltip, 0, FText(), Section /*= 0*/));
		OutAllActions.AddAction(ScriptVarAndViewInfoAction);
	}
}
 
void SNiagaraParameterPanel::CollectStaticSections(TArray<int32>& StaticSectionIDs) const
{
	return ParameterPanelViewModel->CollectStaticSections(StaticSectionIDs);
}

FReply SNiagaraParameterPanel::OnActionDragged(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, const FPointerEvent& MouseEvent)
{
	if (InActions.Num() == 1 && InActions[0].IsValid())
	{
		return ParameterPanelViewModel->HandleActionDragged(InActions[0], MouseEvent);
	}
	return FReply::Handled();
}

void SNiagaraParameterPanel::OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& InActions, ESelectInfo::Type InSelectionType)
{
	if (InActions.Num() == 1 && InActions[0].IsValid())
	{
		ParameterPanelViewModel->HandleActionSelected(InActions[0], InSelectionType);
	}
}

FText SNiagaraParameterPanel::OnGetSectionTitle(int32 InSectionID)
{
	return NiagaraParameterPanelSectionID::OnGetSectionTitle((NiagaraParameterPanelSectionID::Type)InSectionID);
}

TSharedRef<SWidget> SNiagaraParameterPanel::OnGetSectionWidget(TSharedRef<SWidget> RowWidget, int32 InSectionID)
{
	TWeakPtr<SWidget> WeakRowWidget = RowWidget;
	FText AddNewText = LOCTEXT("AddNewParameter", "Add Parameter");
	FName MetaDataTag = TEXT("AddNewParameter");
	return CreateAddToSectionButton((NiagaraParameterPanelSectionID::Type) InSectionID, WeakRowWidget, AddNewText, MetaDataTag); //@todo(ng) refactor to get rid of int32 insectionid
}

TSharedRef<SWidget> SNiagaraParameterPanel::CreateAddToSectionButton(const NiagaraParameterPanelSectionID::Type InSection, TWeakPtr<SWidget> WeakRowWidget, FText AddNewText, FName MetaDataTag)
{
	TSharedPtr<SComboButton> Button;
	SAssignNew(Button, SComboButton)
	.ButtonStyle(FEditorStyle::Get(), "RoundButton")
	.ForegroundColor(FEditorStyle::GetSlateColor("DefaultForeground"))
	.ContentPadding(FMargin(2, 0))
	.OnGetMenuContent(this, &SNiagaraParameterPanel::OnGetParameterMenu, InSection)
	//.IsEnabled(this, &SNiagaraParameterMapView::ParameterAddEnabled) //@Todo(ng) impl
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	.HasDownArrow(false)
	.AddMetaData<FTagMetaData>(FTagMetaData(MetaDataTag))
	.ButtonContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(FMargin(0, 1))
		[
			SNew(SImage)
			.Image(FEditorStyle::GetBrush("Plus"))
		]

		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(FMargin(2, 0, 0, 0))
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
			.Text(AddNewText)
			//.Visibility(this, &SNiagaraParameterMapView::OnAddButtonTextVisibility, WeakRowWidget, InSection) //@todo(ng) impl
			.ShadowOffset(FVector2D(1, 1))
		]
	];

	AddParameterButtons[(int32)InSection] = Button;

	return Button.ToSharedRef();
}

TSharedRef<SWidget> SNiagaraParameterPanel::OnGetParameterMenu(const NiagaraParameterPanelSectionID::Type InSection)
{
	ENiagaraParameterScope NewParameterScope = NiagaraParameterPanelSectionID::GetScopeForNewParametersInSection(InSection);

	TSharedRef<SNiagaraAddParameterMenu2> MenuWidget = SNew(SNiagaraAddParameterMenu2, ParameterPanelViewModel->GetEditableGraphs(), NewParameterScope)
		.OnAddParameter(this, &SNiagaraParameterPanel::AddParameter, InSection)
		.OnAllowMakeType(this, &SNiagaraParameterPanel::AllowMakeType)
		.ShowGraphParameters(false)
		.AutoExpandMenu(true);

	AddParameterButtons[(int32)InSection]->SetMenuContentWidgetToFocus(MenuWidget->GetSearchBox()->AsShared());
	return MenuWidget;
}

void SNiagaraParameterPanel::TryDeleteEntries()
{
	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);

	for (auto& Action : SelectedActions)
	{
		TSharedPtr<FNiagaraScriptVarAndViewInfoAction> ScriptVarAndViewInfoAction = StaticCastSharedPtr<FNiagaraScriptVarAndViewInfoAction>(Action);
		if (ScriptVarAndViewInfoAction.Get())
		{
			const FNiagaraVariable& Parameter = ScriptVarAndViewInfoAction->ScriptVariableAndViewInfo.ScriptVariable;
			const FNiagaraVariableMetaData& ParameterMetaData = ScriptVarAndViewInfoAction->ScriptVariableAndViewInfo.MetaData;
			if (ParameterPanelViewModel->CanRemoveParameter(Parameter, ParameterMetaData))
			{
				ParameterPanelViewModel->RemoveParameter(Parameter, ParameterMetaData);
			}
		}
	}
}

bool SNiagaraParameterPanel::CanTryDeleteEntries() const
{
	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);

	for (auto& Action : SelectedActions)
	{
		TSharedPtr<FNiagaraScriptVarAndViewInfoAction> ScriptVarAndViewInfoAction = StaticCastSharedPtr<FNiagaraScriptVarAndViewInfoAction>(Action);
		if (ScriptVarAndViewInfoAction.Get())
		{
			const FNiagaraVariable& Parameter = ScriptVarAndViewInfoAction->ScriptVariableAndViewInfo.ScriptVariable;
			const FNiagaraVariableMetaData& ParameterMetaData = ScriptVarAndViewInfoAction->ScriptVariableAndViewInfo.MetaData;
			if (ParameterPanelViewModel->CanRemoveParameter(Parameter, ParameterMetaData))
			{
				return true;
			}
		}
	}
	return false;
}

void SNiagaraParameterPanel::OnRequestRenameOnActionNode()
{
	GraphActionMenu->OnRequestRenameOnActionNode();
}

bool SNiagaraParameterPanel::CanRequestRenameOnActionNode(TWeakPtr<struct FGraphActionNode> InSelectedNode) const
{
	return true; //@todo(ng) delegate to viewmodel
}

bool SNiagaraParameterPanel::CanRequestRenameOnActionNode() const
{
	TArray<TSharedPtr<FEdGraphSchemaAction> > SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);

	// If there is anything selected in the GraphActionMenu, check the item for if it can be renamed.
	if (SelectedActions.Num() > 0)
	{
		return GraphActionMenu->CanRequestRenameOnActionNode();
	}
	return false;
}

bool SNiagaraParameterPanel::HandleActionMatchesName(FEdGraphSchemaAction* InAction, const FName& InName) const
{
	return FName(*InAction->GetMenuDescription().ToString()) == InName;
}

void SNiagaraParameterPanel::Refresh()
{
	bNeedsRefresh = true;
}

	/************************************************************************/
	/* SNiagaraAddParameterMenu2                                             */
	/************************************************************************/
void SNiagaraAddParameterMenu2::Construct(const FArguments& InArgs, TArray<TWeakObjectPtr<UNiagaraGraph>> InGraphs, ENiagaraParameterScope InNewParameterScope)
{
	this->OnAddParameter = InArgs._OnAddParameter;
	this->OnCollectCustomActions = InArgs._OnCollectCustomActions;
	this->OnAllowMakeType = InArgs._OnAllowMakeType;
	this->AllowCreatingNew = InArgs._AllowCreatingNew;
	this->ShowGraphParameters = InArgs._ShowGraphParameters;
	this->AutoExpandMenu = InArgs._AutoExpandMenu;
	this->IsParameterRead = InArgs._IsParameterRead;

	Graphs = InGraphs;
	NewParameterScope = InNewParameterScope;
	NewParameterScopeText = FText::FromString(FNiagaraStackGraphUtilities::GetNamespaceStringForScriptParameterScope(NewParameterScope));

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			.MinDesiredWidth(300)
			[
				SAssignNew(GraphMenu, SGraphActionMenu)
				.OnActionSelected(this, &SNiagaraAddParameterMenu2::OnActionSelected)
				.OnCollectAllActions(this, &SNiagaraAddParameterMenu2::CollectAllActions)
				.AutoExpandActionMenu(AutoExpandMenu.Get())
				.ShowFilterTextBox(true)
				.OnCreateCustomRowExpander_Static(&SNiagaraParameterPanel::CreateCustomActionExpander)
				.OnCreateWidgetForAction_Lambda([](const FCreateWidgetForActionData* InData)
				{
					return SNew(SNiagaraGraphActionWidget, InData);
				})
			]
		]
	];
}

TSharedRef<SEditableTextBox> SNiagaraAddParameterMenu2::GetSearchBox()
{
	return GraphMenu->GetFilterTextBox();
}

void SNiagaraAddParameterMenu2::OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (int32 ActionIndex = 0; ActionIndex < SelectedActions.Num(); ActionIndex++)
		{
			TSharedPtr<FNiagaraMenuAction> CurrentAction = StaticCastSharedPtr<FNiagaraMenuAction>(SelectedActions[ActionIndex]);

			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				CurrentAction->ExecuteAction();
			}
		}
	}
}

void SNiagaraAddParameterMenu2::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	if (OnCollectCustomActions.IsBound())
	{
		bool bCreateRemainingActions = true;
		OnCollectCustomActions.Execute(OutAllActions, bCreateRemainingActions);
		if (!bCreateRemainingActions)
		{
			return;
		}
	}

	struct FNiagaraMenuActionInfo
	{
		FNiagaraMenuActionInfo()
			: CategoryText()
			, DisplayNameText()
			, TooltipText()
			, NewVariable()
			, Grouping(0)
			, KeywordsText()
			, bIsExperimental(false)
		{};

		FText CategoryText;
		FText DisplayNameText;
		FText TooltipText;
		FNiagaraVariable NewVariable;
		int32 Grouping;
		FText KeywordsText;
		bool bIsExperimental;
	};

	if (AllowCreatingNew.Get())
	{
		// Create actions to create new FNiagaraVariables of every allowed FNiagaraTypeDefinition type.
		TArray<FNiagaraMenuActionInfo> NewParameterMenuActionInfos;

		TArray<FNiagaraTypeDefinition> Types = FNiagaraTypeRegistry::GetRegisteredTypes();
		for (const FNiagaraTypeDefinition& RegisteredType : Types)
		{
			bool bAllowType = true;
			if (OnAllowMakeType.IsBound())
			{
				bAllowType = OnAllowMakeType.Execute(RegisteredType);
			}

			if (bAllowType)
			{
				FNiagaraMenuActionInfo NewMenuActionInfo = FNiagaraMenuActionInfo();
				NewMenuActionInfo.CategoryText = LOCTEXT("NiagaraCreateNewParameterMenu", "Create New Parameter");

				FText RegisteredTypeNameText = RegisteredType.GetNameText();
				NewMenuActionInfo.DisplayNameText = RegisteredTypeNameText;

				if (const UStruct* VariableStruct = RegisteredType.GetStruct())
				{
					NewMenuActionInfo.TooltipText = VariableStruct->GetToolTipText(true);
				}

				const FName NewVariableName = FName(*(NewParameterScopeText.ToString() + RegisteredTypeNameText.ToString()));
				FNiagaraVariable NewVar = FNiagaraVariable(RegisteredType, NewVariableName);
				FNiagaraEditorUtilities::ResetVariableToDefaultValue(NewVar);
				NewMenuActionInfo.NewVariable = NewVar;

				if (NewVar.IsDataInterface())
				{
					if (const UClass* DataInterfaceClass = NewVar.GetType().GetClass())
					{
						NewMenuActionInfo.bIsExperimental = DataInterfaceClass->GetMetaData("DevelopmentStatus") == TEXT("Experimental");
					}
				}

				NewParameterMenuActionInfos.Add(NewMenuActionInfo);
			}
		}

		// Sort new action infos to add to the GraphActionMenu in alphabetical order of their display name.
		NewParameterMenuActionInfos.Sort([](const FNiagaraMenuActionInfo& A, const FNiagaraMenuActionInfo& B)
			{ return A.DisplayNameText.CompareToCaseIgnored(B.DisplayNameText) < 0; }
		);

		// Create the actual actions and mark them experimental if so.
		for (const FNiagaraMenuActionInfo& ActionInfo : NewParameterMenuActionInfos)
		{
			TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(ActionInfo.CategoryText, ActionInfo.DisplayNameText, ActionInfo.TooltipText, ActionInfo.Grouping, ActionInfo.KeywordsText,
				FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterMenu2::AddParameterSelected, ActionInfo.NewVariable)));

			Action->IsExperimental = ActionInfo.bIsExperimental;

			OutAllActions.AddAction(Action);
		}
	}


	// If used in a graph, add all existing graph parameter names to the add menu.
	if (ShowGraphParameters.Get())
	{
		for (TWeakObjectPtr<UNiagaraGraph>& Graph : Graphs)
		{
			TMap<FNiagaraVariable, UNiagaraScriptVariable*> GraphParameters = Graph.Get()->GetAllMetaData(); //@todo(ng) try an array view so we can sort it without copying
			GraphParameters.KeySort([](const FNiagaraVariable& A, const FNiagaraVariable& B) { return A.GetName().LexicalLess(B.GetName()); }); //@todo(ng) sort by scope

			for (TTuple<FNiagaraVariable, UNiagaraScriptVariable*>& GraphParameter : GraphParameters)
			{
				const FText Category = LOCTEXT("NiagaraAddExistingParameterMenu", "Add Existing Parameter");
				const FText DisplayName = FText::FromName(GraphParameter.Key.GetName());

				const FText Tooltip = GraphParameter.Value->Metadata.Description;
				FNiagaraVariable Variable = GraphParameter.Key;
				TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
					Category, DisplayName, Tooltip, 0, FText::GetEmpty(),
					FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterMenu2::AddParameterSelected, Variable)));

				OutAllActions.AddAction(Action);
			}
		}
	}
}

void SNiagaraAddParameterMenu2::AddParameterSelected(FNiagaraVariable NewVariable)
{
	OnAddParameter.ExecuteIfBound(NewVariable);
}

class SNiagaraActionMenuExpander2 : public SExpanderArrow
{
	SLATE_BEGIN_ARGS(SNiagaraActionMenuExpander2) {}
	SLATE_ATTRIBUTE(float, IndentAmount)
		SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const FCustomExpanderData& ActionMenuData)
	{
		OwnerRowPtr = ActionMenuData.TableRow;
		IndentAmount = InArgs._IndentAmount;
		if (!ActionMenuData.RowAction.IsValid())
		{
			SExpanderArrow::FArguments SuperArgs;
			SuperArgs._IndentAmount = InArgs._IndentAmount;

			SExpanderArrow::Construct(SuperArgs, ActionMenuData.TableRow);
		}
		else
		{
			ChildSlot
				.Padding(TAttribute<FMargin>(this, &SNiagaraActionMenuExpander2::GetCustomIndentPadding))
				[
					SNew(SBox)
				];
		}
	}

private:
	FMargin GetCustomIndentPadding() const
	{
		return SExpanderArrow::GetExpanderPadding();
	}
};

TSharedRef<SExpanderArrow> SNiagaraParameterPanel::CreateCustomActionExpander(const FCustomExpanderData& ActionMenuData)
{
	return SNew(SNiagaraActionMenuExpander2, ActionMenuData);
}

void SNiagaraParameterPanel::SetVariablesObjectSelection(const TSharedRef<FNiagaraObjectSelection>& InVariablesObjectSelection)
{
	SelectedVariableObjects = InVariablesObjectSelection;
}

#undef LOCTEXT_NAMESPACE // "NiagaraParameterPanel"
