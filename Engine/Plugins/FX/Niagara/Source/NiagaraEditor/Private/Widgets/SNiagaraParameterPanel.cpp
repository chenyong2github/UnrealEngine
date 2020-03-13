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
#include "Editor/GraphEditor/Private/GraphActionNode.h"
#include "NiagaraConstants.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

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
	bNeedsRefresh = false;
	bGraphActionPendingRename = false;

	ParameterPanelViewModel = InParameterPanelViewModel;
	ToolkitCommands = InToolkitCommands;

	ParameterPanelViewModel->GetOnRefreshed().BindRaw(this, &SNiagaraParameterPanel::Refresh);
	ParameterPanelViewModel->GetExternalSelectionChanged().AddRaw(this, &SNiagaraParameterPanel::HandleExternalSelectionChanged);

	AddParameterButtons.SetNum(NiagaraParameterPanelSectionID::CUSTOM + 1); //@Todo(ng) verify

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
		.OnContextMenuOpening(this, &SNiagaraParameterPanel::OnContextMenuOpening)
		.OnCanRenameSelectedAction(this, &SNiagaraParameterPanel::CanRequestRenameOnActionNode)
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

	if (bGraphActionPendingRename)
	{
		if (GraphActionMenu->CanRequestRenameOnActionNode())
		{
			GraphActionMenu->OnRequestRenameOnActionNode();
			bGraphActionPendingRename = false;
		}
	}
}

void SNiagaraParameterPanel::HandleExternalSelectionChanged(const UObject* Obj)
{
	if (Obj && Obj->IsA< UNiagaraScriptVariable>())
	{
		const UNiagaraScriptVariable* Var = Cast< UNiagaraScriptVariable>(Obj);
		if (Var)
		{
			GraphActionMenu->SelectItemByName(Var->Variable.GetName());
		}
	}
}

void SNiagaraParameterPanel::AddParameter(FNiagaraVariable NewVariable, const NiagaraParameterPanelSectionID::Type SectionID)
{

	FNiagaraVariableMetaData GuessedMetaData;
	FNiagaraEditorUtilities::GetParameterMetaDataFromName(NewVariable.GetName(), GuessedMetaData);
	const UNiagaraScriptVariable* NewScriptVar = ParameterPanelViewModel->AddParameter(NewVariable, GuessedMetaData);
	
	if (NewScriptVar != nullptr)
	{
		GraphActionMenu->RefreshAllActions(true);
		GraphActionMenu->SelectItemByName(NewScriptVar->Variable.GetName());

		// Delay calling the rename delegate one tick as the widget to which it is bound is not yet constructed.
		bGraphActionPendingRename = true;
	}
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

	auto SortParameterPanelEntries = [](const FNiagaraScriptVariableAndViewInfo& A, const FNiagaraScriptVariableAndViewInfo& B) {
		if (A.MetaData.GetIsUsingLegacyNameString())
		{
			if (B.MetaData.GetIsUsingLegacyNameString())
			{
				return A.ScriptVariable.GetName().LexicalLess(B.ScriptVariable.GetName());
			}
			return false;
		}
		else if (B.MetaData.GetIsUsingLegacyNameString())
		{
			return true;
		}
		FName AName;
		FName BName;
		A.MetaData.GetParameterName(AName);
		B.MetaData.GetParameterName(BName);
		return AName.LexicalLess(BName);
	};

	TArray<FNiagaraScriptVariableAndViewInfo> VarAndViewInfos = ParameterPanelViewModel->GetViewedParameters();
	VarAndViewInfos.Sort(SortParameterPanelEntries);
	//UE_LOG(LogNiagaraEditor, Log, TEXT("Adding Vars--------------------------------------------------"));
	TArray<FNiagaraVariable> AddedVars;
	for (const FNiagaraScriptVariableAndViewInfo& VarAndViewInfo : VarAndViewInfos)
	{
		int32 FoundVarIndex = AddedVars.Find(VarAndViewInfo.ScriptVariable);
		if (FoundVarIndex < 0)
		{
			const FText Name = FText::FromName(VarAndViewInfo.ScriptVariable.GetName());
			const FText Tooltip = FText::Format(TooltipFormat, FText::FromName(VarAndViewInfo.ScriptVariable.GetName()), VarAndViewInfo.ScriptVariable.GetType().GetNameText());
			const NiagaraParameterPanelSectionID::Type Section = ParameterPanelViewModel->GetSectionForVarAndViewInfo(VarAndViewInfo);
			TSharedPtr<FNiagaraScriptVarAndViewInfoAction> ScriptVarAndViewInfoAction(new FNiagaraScriptVarAndViewInfoAction(VarAndViewInfo, FText::GetEmpty(), Name, Tooltip, 0, FText(), Section /*= 0*/));
			OutAllActions.AddAction(ScriptVarAndViewInfoAction);
			AddedVars.Add(VarAndViewInfo.ScriptVariable);

			//UE_LOG(LogNiagaraEditor, Log, TEXT("%s"), *Name.ToString());
		}

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

TSharedPtr<SWidget> SNiagaraParameterPanel::OnContextMenuOpening()
{
	// Get the current selected action
	TArray<TSharedPtr<FEdGraphSchemaAction> > SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);
	ensureMsgf(SelectedActions.Num() == 1 || SelectedActions.Num() == 0, TEXT("Unexpected number of selected actions encountered when getting current selected action for parameter panel!"));
	if (SelectedActions.Num() == 1 && SelectedActions[0].IsValid())
	{
		TSharedPtr<FNiagaraScriptVarAndViewInfoAction> CurrentAction = StaticCastSharedPtr<FNiagaraScriptVarAndViewInfoAction>(SelectedActions[0]);
		if (ensureMsgf(CurrentAction.Get(), TEXT("Action pointer was null when getting selected action for parameter panel!")))
		{
			const bool bShouldCloseWindowAfterMenuSelection = true;
			FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, ToolkitCommands);
			MenuBuilder.BeginSection("BasicOperations");

			const FNiagaraVariable& CurrentParameter = CurrentAction->ScriptVariableAndViewInfo.ScriptVariable;
			const FNiagaraVariableMetaData& CurrentParameterMetaData = CurrentAction->ScriptVariableAndViewInfo.MetaData;

			FText CanRenameParameterToolTip;
			bool bCanRequestRenameParameter = ParameterPanelViewModel->GetCanRenameParameterAndToolTip(CurrentParameter, CurrentParameterMetaData, TOptional<const FText>(), CanRenameParameterToolTip);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ParameterPanelRenameParameterContextMenu", "Rename Parameter"),
				CanRenameParameterToolTip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this]()
						{
							OnRequestRenameOnActionNode();
						}),
					FCanExecuteAction::CreateLambda([bCanRequestRenameParameter]() {return bCanRequestRenameParameter; }))
			);

			FText CanDeleteParameterToolTip;
			bool bCanDeleteParameter = ParameterPanelViewModel->GetCanDeleteParameterAndToolTip(CurrentParameter, CurrentParameterMetaData, CanDeleteParameterToolTip);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ParameterPanelDeleteParameterContextMenu", "Delete Parameter"),
				CanDeleteParameterToolTip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, CurrentParameter, CurrentParameterMetaData]()
						{
							ParameterPanelViewModel->DeleteParameter(CurrentParameter, CurrentParameterMetaData);
						}),
					FCanExecuteAction::CreateLambda([bCanDeleteParameter]() {return bCanDeleteParameter; }))
			);

			MenuBuilder.EndSection();
			return MenuBuilder.MakeWidget();
		}
	}
	return SNullWidget::NullWidget;
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

	];

	AddParameterButtons[(int32)InSection] = Button;

	return Button.ToSharedRef();
}

TSharedRef<SWidget> SNiagaraParameterPanel::OnGetParameterMenu(const NiagaraParameterPanelSectionID::Type InSection)
{
	ENiagaraParameterScope NewParameterScopeForSection = NiagaraParameterPanelSectionID::GetScopeForNewParametersInSection(InSection);

	const bool bCanCreateNew = InSection != NiagaraParameterPanelSectionID::Type::ENGINE && InSection != NiagaraParameterPanelSectionID::Type::OWNER;
	const bool bAutoExpand = InSection == NiagaraParameterPanelSectionID::Type::LOCALS || InSection == NiagaraParameterPanelSectionID::Type::INPUTS ||
		InSection == NiagaraParameterPanelSectionID::Type::OUTPUTS || InSection == NiagaraParameterPanelSectionID::Type::USER || 
		InSection == NiagaraParameterPanelSectionID::Type::ENGINE || InSection == NiagaraParameterPanelSectionID::Type::OWNER;
	TSharedRef<SNiagaraAddParameterMenu2> MenuWidget = SNew(SNiagaraAddParameterMenu2, ParameterPanelViewModel->GetEditableGraphs())
		.OnAddParameter(this, &SNiagaraParameterPanel::AddParameter, InSection)
		.OnAllowMakeType(this, &SNiagaraParameterPanel::AllowMakeType)
		.ShowGraphParameters(false)
		.ShowKnownConstantParametersFilter(InSection)
		.AllowCreatingNew(bCanCreateNew)
		.AutoExpandMenu(bAutoExpand)
		.NewParameterScope(NewParameterScopeForSection);

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
			FText ToolTipText;
			if (ParameterPanelViewModel->GetCanDeleteParameterAndToolTip(Parameter, ParameterMetaData, ToolTipText))
			{
				ParameterPanelViewModel->DeleteParameter(Parameter, ParameterMetaData);
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
			FText ToolTipText;
			if (ParameterPanelViewModel->GetCanDeleteParameterAndToolTip(Parameter, ParameterMetaData, ToolTipText))
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

bool SNiagaraParameterPanel::CanRequestRenameOnActionNode(TWeakPtr<FGraphActionNode> InSelectedNode) const
{
	if (InSelectedNode.IsValid() && InSelectedNode.Pin()->Actions.Num() == 1 && InSelectedNode.Pin()->Actions[0].IsValid())
	{
		const FNiagaraScriptVarAndViewInfoAction* SelectedAction = static_cast<FNiagaraScriptVarAndViewInfoAction*>(InSelectedNode.Pin()->Actions[0].Get());
		FText BlankText = FText();
		return ParameterPanelViewModel->GetCanRenameParameterAndToolTip(
			SelectedAction->ScriptVariableAndViewInfo.ScriptVariable,
			SelectedAction->ScriptVariableAndViewInfo.MetaData,
			TOptional<const FText>(),
			BlankText
		);
	}
	
	return false;
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
void SNiagaraAddParameterMenu2::Construct(const FArguments& InArgs, TArray<TWeakObjectPtr<UNiagaraGraph>> InGraphs)
{
	this->OnAddParameter = InArgs._OnAddParameter;
	this->OnCollectCustomActions = InArgs._OnCollectCustomActions;
	this->OnAllowMakeType = InArgs._OnAllowMakeType;
	this->AllowCreatingNew = InArgs._AllowCreatingNew;
	this->ShowGraphParameters = InArgs._ShowGraphParameters;
	this->ShowKnownConstantParametersFilter = InArgs._ShowKnownConstantParametersFilter;
	this->AutoExpandMenu = InArgs._AutoExpandMenu;
	this->IsParameterRead = InArgs._IsParameterRead;
	this->NewParameterScope = InArgs._NewParameterScope;
	this->NewParameterNamespace = InArgs._NewParameterNamespace;

	Graphs = InGraphs;
	if (NewParameterNamespace.IsSet())
	{
		NewParameterScopeText = FText::FromString(NewParameterNamespace.Get());
	}
	else
	{
		NewParameterScopeText = FText::FromString(FNiagaraTypeUtilities::GetNamespaceStringForScriptParameterScope(NewParameterScope));
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			.MinDesiredWidth(300)
			.MaxDesiredHeight(700) // Set max desired height to prevent flickering bug for menu larger than screen
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

				FName NewVariableName = FName(*(NewParameterScopeText.ToString() + TEXT("New ") + RegisteredTypeNameText.ToString()));
				NewVariableName = UNiagaraGraph::MakeUniqueParameterNameAcrossGraphs(NewVariableName, Graphs);
			
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

	if (ShowKnownConstantParametersFilter.IsSet())
	{
		NiagaraParameterPanelSectionID::Type Type = ShowKnownConstantParametersFilter.Get();
		if (Type != NiagaraParameterPanelSectionID::Type::NONE)
		{
			// Handle engine parameters...
			if (Type == NiagaraParameterPanelSectionID::Type::ENGINE ||
				Type == NiagaraParameterPanelSectionID::Type::OWNER ||
				Type == NiagaraParameterPanelSectionID::Type::REFERENCES ||
				Type == NiagaraParameterPanelSectionID::Type::SYSTEM ||
				Type == NiagaraParameterPanelSectionID::Type::EMITTER)
			{
				const TArray<FNiagaraVariable>& EngineConstants = FNiagaraConstants::GetEngineConstants();
				for (const FNiagaraVariable& Var : EngineConstants)
				{
					const FText Category = LOCTEXT("NiagaraAddEngineParameterMenu", "Engine Constant");
					const FText DisplayName = FText::FromName(Var.GetName());
					const FText Tooltip = FNiagaraConstants::GetEngineConstantDescription(Var);


					FNiagaraVariableMetaData GuessedMetaData;
					FNiagaraEditorUtilities::GetParameterMetaDataFromName(Var.GetName(), GuessedMetaData);
					bool bAdd = Type == NiagaraParameterPanelSectionID::Type::REFERENCES; // References has all of these!
					if (Type == NiagaraParameterPanelSectionID::Type::ENGINE && GuessedMetaData.GetScopeName() == FNiagaraConstants::EngineNamespace)
						bAdd = true;
					else if (Type == NiagaraParameterPanelSectionID::Type::OWNER && GuessedMetaData.GetScopeName() == FNiagaraConstants::EngineOwnerScopeName)
						bAdd = true;
					else if (Type == NiagaraParameterPanelSectionID::Type::SYSTEM && GuessedMetaData.GetScopeName() == FNiagaraConstants::EngineSystemScopeName)
						bAdd = true;
					else if (Type == NiagaraParameterPanelSectionID::Type::EMITTER && GuessedMetaData.GetScopeName() == FNiagaraConstants::EngineEmitterScopeName)
						bAdd = true;

					if (!bAdd)
						continue;

					TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
						Category, DisplayName, Tooltip, 0, FText::GetEmpty(),
						FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterMenu2::AddParameterSelected, Var)));

					OutAllActions.AddAction(Action);
				}
			}

			// Handle particles 
			if (Type == NiagaraParameterPanelSectionID::Type::PARTICLES ||
				Type == NiagaraParameterPanelSectionID::Type::REFERENCES )
			{
				const TArray<FNiagaraVariable>& Vars = FNiagaraConstants::GetCommonParticleAttributes();

				for (const FNiagaraVariable& Var : Vars)
				{
					const FText Category = LOCTEXT("NiagaraAddCommParticleParameterMenu", "Common Particle Attributes");
					const FText DisplayName = FText::FromName(Var.GetName());
					const FText Tooltip = FNiagaraConstants::GetAttributeDescription(Var);
					
					TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
						Category, DisplayName, Tooltip, 0, FText::GetEmpty(),
						FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterMenu2::AddParameterSelected, Var)));

					OutAllActions.AddAction(Action);

				}
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

#undef LOCTEXT_NAMESPACE // "NiagaraParameterPanel"
