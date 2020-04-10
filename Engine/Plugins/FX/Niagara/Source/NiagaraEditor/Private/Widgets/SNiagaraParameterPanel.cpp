// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SNiagaraParameterPanel.h"
#include "NiagaraEditorCommon.h"
#include "ViewModels/NiagaraParameterPanelViewModel.h"
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
#include "Widgets/SItemSelector.h"
#include "NiagaraEditorModule.h"
#include "NiagaraTypes.h"

#define LOCTEXT_NAMESPACE "NiagaraParameterPanel"

SNiagaraParameterPanel::~SNiagaraParameterPanel()
{
	ParameterPanelViewModel->GetOnRefreshed().Unbind();

	// Unregister all commands for right click on action node
// 	ToolkitCommands->UnmapAction(FNiagaraParameterPanelCommands::Get().DeleteEntry);
// 	ToolkitCommands->UnmapAction(FGenericCommands::Get().Rename);
}

void FNiagaraParameterPanelCommands::RegisterCommands()
{
	UI_COMMAND(DeleteEntry, "Delete", "Delete this parameter", EUserInterfaceActionType::Button, FInputChord(EKeys::Platform_Delete));
}

void SNiagaraParameterPanel::Construct(const FArguments& InArgs, const TSharedPtr<INiagaraParameterPanelViewModel>& InParameterPanelViewModel, const TSharedPtr<FUICommandList>& InToolkitCommands)
{
	ParameterPanelViewModel = InParameterPanelViewModel;
	ToolkitCommands = InToolkitCommands; //@todo(ng) verify

	ParameterPanelViewModel->GetOnRefreshed().BindRaw(this, &SNiagaraParameterPanel::Refresh);
	ParameterPanelViewModel->GetExternalSelectionChanged().AddRaw(this, &SNiagaraParameterPanel::HandleExternalSelectionChanged);

	bNeedsRefresh = false;

	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(300)
		[
			SAssignNew(ItemSelector, SNiagaraParameterPanelSelector)
			.Items(ParameterPanelViewModel->GetViewedParameters())
			.OnGetCategoriesForItem(this, &SNiagaraParameterPanel::OnGetCategoriesForItem)
			.OnCompareCategoriesForEquality(this, &SNiagaraParameterPanel::OnCompareCategoriesForEquality)
			.OnCompareCategoriesForSorting(this, &SNiagaraParameterPanel::OnCompareCategoriesForSorting)
			.OnCompareItemsForEquality(this, &SNiagaraParameterPanel::OnCompareItemsForEquality)
			.OnCompareItemsForSorting(this, &SNiagaraParameterPanel::OnCompareItemsForSorting)
			.OnDoesItemMatchFilterText(this, &SNiagaraParameterPanel::OnDoesItemMatchFilterText)
			.OnGenerateWidgetForCategory(this, &SNiagaraParameterPanel::OnGenerateWidgetForCategory)
			.OnGenerateWidgetForItem(this, &SNiagaraParameterPanel::OnGenerateWidgetForItem)
			.AllowMultiselect(false)
			.DefaultCategoryPaths(GetDefaultCategoryPaths())
			.ClearSelectionOnClick(true)
		]
	];

// 	// Register all commands for right click on action node
// 	FNiagaraParameterPanelCommands::Register();
// 	TSharedPtr<FUICommandList> ToolKitCommandList = ToolkitCommands;
// 	ToolKitCommandList->MapAction(FNiagaraParameterPanelCommands::Get().DeleteEntry,
// 		FExecuteAction::CreateSP(this, &SNiagaraParameterPanel::TryDeleteEntries),
// 		FCanExecuteAction::CreateSP(this, &SNiagaraParameterPanel::CanTryDeleteEntries));
// 	ToolKitCommandList->MapAction(FGenericCommands::Get().Rename,
// 		FExecuteAction::CreateSP(this, &SNiagaraParameterPanel::OnRequestRenameOnActionNode),
// 		FCanExecuteAction::CreateSP(this, &SNiagaraParameterPanel::CanRequestRenameOnActionNode));



// 	// create the main action list piece of this widget
// 	SAssignNew(GraphActionMenu, SGraphActionMenu, false)
// 		.OnCreateWidgetForAction(this, &SNiagaraParameterPanel::OnCreateWidgetForAction)
// 		.OnCollectAllActions(this, &SNiagaraParameterPanel::CollectAllActions)
//  		.OnCollectStaticSections(this, &SNiagaraParameterPanel::CollectStaticSections)
//  		.OnActionDragged(this, &SNiagaraParameterPanel::OnActionDragged)
// 		.OnActionSelected(this, &SNiagaraParameterPanel::OnActionSelected)
// // 		.OnActionDoubleClicked(this, &SNiagaraParameterMapView::OnActionDoubleClicked)
// 		.OnContextMenuOpening(this, &SNiagaraParameterPanel::OnContextMenuOpening)
// 		.OnCanRenameSelectedAction(this, &SNiagaraParameterPanel::CanRequestRenameOnActionNode)
//  		.OnGetSectionTitle(this, &SNiagaraParameterPanel::OnGetSectionTitle)
//  		.OnGetSectionWidget(this, &SNiagaraParameterPanel::OnGetSectionWidget)
//  		.OnCreateCustomRowExpander_Static(&SNiagaraParameterPanel::CreateCustomActionExpander)
// 		.OnActionMatchesName(this, &SNiagaraParameterPanel::HandleActionMatchesName)
// 		.AutoExpandActionMenu(false)
// 		.AlphaSortItems(false)
// 		.UseSectionStyling(true)
// 		.ShowFilterTextBox(true);
// 
// 	ChildSlot
// 	[
// 		SNew(SBox)
// 		.MinDesiredWidth(300)
// 		[
// 			SNew(SVerticalBox)	
// 			+ SVerticalBox::Slot()
// 			.FillHeight(1.0f)
// 			[
// 				GraphActionMenu.ToSharedRef()
// 			]
// 		]
// 	];
}


TArray<ENiagaraParameterPanelCategory> SNiagaraParameterPanel::OnGetCategoriesForItem(const FNiagaraScriptVariableAndViewInfo& Item)
{
	return ParameterPanelViewModel->GetCategoriesForParameter(Item);
}

bool SNiagaraParameterPanel::OnCompareCategoriesForEquality(const ENiagaraParameterPanelCategory& CategoryA, const ENiagaraParameterPanelCategory& CategoryB) const
{
	return CategoryA == CategoryB;
}

bool SNiagaraParameterPanel::OnCompareCategoriesForSorting(const ENiagaraParameterPanelCategory& CategoryA, const ENiagaraParameterPanelCategory& CategoryB) const
{
	return CategoryA < CategoryB;
}

bool SNiagaraParameterPanel::OnCompareItemsForEquality(const FNiagaraScriptVariableAndViewInfo& ItemA, const FNiagaraScriptVariableAndViewInfo& ItemB) const
{
	return ItemA == ItemB;
}

bool SNiagaraParameterPanel::OnCompareItemsForSorting(const FNiagaraScriptVariableAndViewInfo& ItemA, const FNiagaraScriptVariableAndViewInfo& ItemB) const
{
	if (ItemA.MetaData.GetIsUsingLegacyNameString())
	{
		if (ItemB.MetaData.GetIsUsingLegacyNameString())
		{
			return ItemA.ScriptVariable.GetName().LexicalLess(ItemB.ScriptVariable.GetName());
		}
		return false;
	}
	else if (ItemB.MetaData.GetIsUsingLegacyNameString())
	{
		return true;
	}
	FName AName;
	FName BName;
	ItemA.MetaData.GetParameterName(AName);
	ItemB.MetaData.GetParameterName(BName);
	return AName.LexicalLess(BName);
}

bool SNiagaraParameterPanel::OnDoesItemMatchFilterText(const FText& FilterText, const FNiagaraScriptVariableAndViewInfo& Item)
{
	return Item.ScriptVariable.GetName().ToString().Contains(FilterText.ToString());
}

TSharedRef<SWidget> SNiagaraParameterPanel::OnGenerateWidgetForCategory(const ENiagaraParameterPanelCategory& Category)
{
	const UEnum* ParameterPanelCategoryEnum = FNiagaraTypeDefinition::GetParameterPanelCategoryEnum();

	TSharedRef<SHorizontalBox> ParameterPanelCategoryHorizontalBox = SNew(SHorizontalBox);
	ParameterPanelCategoryHorizontalBox->AddSlot()
		.VAlign(VAlign_Center)
		.Padding(3, 0, 0, 0)
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "DetailsView.CategoryTextStyle")
		.Text(ParameterPanelCategoryEnum->GetDisplayNameTextByValue((const int64)Category))
		];

	if (GetCanAddParametersToCategory(Category))
	{
		FText AddNewText = LOCTEXT("AddNewParameter", "Add Parameter");
		FName MetaDataTag = TEXT("AddNewParameter");

		ParameterPanelCategoryHorizontalBox->AddSlot()
			.AutoWidth()
			.Padding(0.0f, 4.0f, 3.0f, 4.0f)
			[
				CreateAddToCategoryButton(Category, AddNewText, MetaDataTag)
			];
	}

	return ParameterPanelCategoryHorizontalBox;
}

TSharedRef<SWidget> SNiagaraParameterPanel::OnGenerateWidgetForItem(const FNiagaraScriptVariableAndViewInfo& Item)
{
	TSharedRef<SWidget> ParameterVisualWidget = ParameterPanelViewModel->GetScriptParameterVisualWidget(Item);
	return SNew(SNiagaraParameterPanelPaletteItem, ParameterVisualWidget);
}

const TArray<TArray<ENiagaraParameterPanelCategory>>& SNiagaraParameterPanel::GetDefaultCategoryPaths() const
{
	return ParameterPanelViewModel->GetDefaultCategoryPaths();
}

void SNiagaraParameterPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bNeedsRefresh)
	{
		ItemSelector->RefreshItemsAndDefaultCategories(ParameterPanelViewModel->GetViewedParameters(), ParameterPanelViewModel->GetDefaultCategoryPaths());
		bNeedsRefresh = false;
	}

// 	if (bGraphActionPendingRename) //@todo(ng) impl
// 	{
// 		if (GraphActionMenu->CanRequestRenameOnActionNode())
// 		{
// 			GraphActionMenu->OnRequestRenameOnActionNode();
// 			bGraphActionPendingRename = false;
// 		}
// 	}
}

void SNiagaraParameterPanel::HandleExternalSelectionChanged(const UObject* Obj)
{
	if (Obj && Obj->IsA< UNiagaraScriptVariable>())
	{
		const UNiagaraScriptVariable* Var = Cast< UNiagaraScriptVariable>(Obj);
		if (Var)
		{
			SelectParameterEntryByName(Var->Variable.GetName());
		}
	}
}

void SNiagaraParameterPanel::AddParameter(FNiagaraVariable NewVariable, const ENiagaraParameterPanelCategory Category)
{
	FNiagaraVariableMetaData GuessedMetaData;
	FNiagaraEditorUtilities::GetParameterMetaDataFromName(NewVariable.GetName(), GuessedMetaData);
	const UNiagaraScriptVariable* NewScriptVar = ParameterPanelViewModel->AddParameter(NewVariable, GuessedMetaData);
	
	if (NewScriptVar != nullptr)
	{
		const FName ScriptVarName = NewScriptVar->Variable.GetName();
		ensureMsgf(SelectParameterEntryByName(ScriptVarName), TEXT("Failed to find newly created UNiagaraScriptVariable to select!"));
	}
}

bool SNiagaraParameterPanel::AllowMakeType(const FNiagaraTypeDefinition& InType) const
{
	return InType != FNiagaraTypeDefinition::GetParameterMapDef() && InType != FNiagaraTypeDefinition::GetGenericNumericDef();
}

TSharedRef<SWidget> SNiagaraParameterPanel::CreateAddToCategoryButton(const ENiagaraParameterPanelCategory Category, FText AddNewText, FName MetaDataTag)
{
	TSharedPtr<SComboButton> Button;
	SAssignNew(Button, SComboButton)
	.ButtonStyle(FEditorStyle::Get(), "RoundButton")
	.ForegroundColor(FEditorStyle::GetSlateColor("DefaultForeground"))
	.ContentPadding(FMargin(2, 0))
	.OnGetMenuContent(this, &SNiagaraParameterPanel::OnGetParameterMenu, Category)
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

	//AddParameterButtons[(int32)InSection] = Button;

	return Button.ToSharedRef();
}

TSharedRef<SWidget> SNiagaraParameterPanel::OnGetParameterMenu(const ENiagaraParameterPanelCategory Category)
{
	ENiagaraParameterScope NewParameterScopeForSection = GetScopeForNewParametersInCategory(Category);

	const bool bCanCreateNew = Category != ENiagaraParameterPanelCategory::Engine && Category != ENiagaraParameterPanelCategory::Owner;
	const bool bAutoExpand = Category == ENiagaraParameterPanelCategory::Local || Category == ENiagaraParameterPanelCategory::Input ||
		Category == ENiagaraParameterPanelCategory::Output || Category == ENiagaraParameterPanelCategory::User || 
		Category == ENiagaraParameterPanelCategory::Engine || Category == ENiagaraParameterPanelCategory::Owner;

	TSharedRef<SNiagaraAddParameterMenu2> MenuWidget = SNew(SNiagaraAddParameterMenu2, ParameterPanelViewModel->GetEditableGraphs())
		.OnAddParameter(this, &SNiagaraParameterPanel::AddParameter, Category)
		.OnAllowMakeType(this, &SNiagaraParameterPanel::AllowMakeType)
		.ShowGraphParameters(false)
		.ShowKnownConstantParametersFilter(Category)
		.AllowCreatingNew(bCanCreateNew)
		.AutoExpandMenu(bAutoExpand)
		.NewParameterScope(NewParameterScopeForSection);

	//AddParameterButtons[(int32)InSection]->SetMenuContentWidgetToFocus(MenuWidget->GetSearchBox()->AsShared());
	return MenuWidget;
}

ENiagaraParameterScope SNiagaraParameterPanel::GetScopeForNewParametersInCategory(const ENiagaraParameterPanelCategory Category)
{
	if (ensureMsgf(GetCanAddParametersToCategory(Category), TEXT("Cannot add parameters to category which we are trying to get scope for!")))
	{
		switch (Category) {
		case ENiagaraParameterPanelCategory::User:
			return ENiagaraParameterScope::User;
		case ENiagaraParameterPanelCategory::Engine:
			return ENiagaraParameterScope::Engine;
		case ENiagaraParameterPanelCategory::Owner:
			return ENiagaraParameterScope::Owner;
		case ENiagaraParameterPanelCategory::System:
			return ENiagaraParameterScope::System;
		case ENiagaraParameterPanelCategory::Emitter:
			return ENiagaraParameterScope::Emitter;
		case ENiagaraParameterPanelCategory::Particles:
			return ENiagaraParameterScope::Particles;
		case ENiagaraParameterPanelCategory::Input:
			return ENiagaraParameterScope::Input;
		case ENiagaraParameterPanelCategory::Local:
			return ENiagaraParameterScope::Local;
		case ENiagaraParameterPanelCategory::ScriptTransient:
			return ENiagaraParameterScope::ScriptTransient;
		default:
			ensureMsgf(false, TEXT("Unexpected category encountered when getting scope for new parameters in category!"));
		};
	}
	return ENiagaraParameterScope::None;
}

bool SNiagaraParameterPanel::GetCanAddParametersToCategory(const ENiagaraParameterPanelCategory Category)
{
	switch (Category) {
	case ENiagaraParameterPanelCategory::User:
	case ENiagaraParameterPanelCategory::Engine:
	case ENiagaraParameterPanelCategory::Owner:
	case ENiagaraParameterPanelCategory::System:
	case ENiagaraParameterPanelCategory::Emitter:
	case ENiagaraParameterPanelCategory::Particles:
	case ENiagaraParameterPanelCategory::Input:
	case ENiagaraParameterPanelCategory::Local:
	case ENiagaraParameterPanelCategory::ScriptTransient:
		return true;
	default:
		return false;
	};
}

// void SNiagaraParameterPanel::TryDeleteEntries()
// {
// 	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
// 	GraphActionMenu->GetSelectedActions(SelectedActions);
// 
// 	for (auto& Action : SelectedActions)
// 	{
// 		TSharedPtr<FNiagaraScriptVarAndViewInfoAction> ScriptVarAndViewInfoAction = StaticCastSharedPtr<FNiagaraScriptVarAndViewInfoAction>(Action);
// 		if (ScriptVarAndViewInfoAction.Get())
// 		{
// 			const FNiagaraVariable& Parameter = ScriptVarAndViewInfoAction->ScriptVariableAndViewInfo.ScriptVariable;
// 			const FNiagaraVariableMetaData& ParameterMetaData = ScriptVarAndViewInfoAction->ScriptVariableAndViewInfo.MetaData;
// 			FText ToolTipText;
// 			if (ParameterPanelViewModel->GetCanDeleteParameterAndToolTip(Parameter, ParameterMetaData, ToolTipText))
// 			{
// 				ParameterPanelViewModel->DeleteParameter(Parameter, ParameterMetaData);
// 			}
// 		}
// 	}
// }
// 
// bool SNiagaraParameterPanel::CanTryDeleteEntries() const
// {
// 	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
// 	GraphActionMenu->GetSelectedActions(SelectedActions);
// 
// 	for (auto& Action : SelectedActions)
// 	{
// 		TSharedPtr<FNiagaraScriptVarAndViewInfoAction> ScriptVarAndViewInfoAction = StaticCastSharedPtr<FNiagaraScriptVarAndViewInfoAction>(Action);
// 		if (ScriptVarAndViewInfoAction.Get())
// 		{
// 			const FNiagaraVariable& Parameter = ScriptVarAndViewInfoAction->ScriptVariableAndViewInfo.ScriptVariable;
// 			const FNiagaraVariableMetaData& ParameterMetaData = ScriptVarAndViewInfoAction->ScriptVariableAndViewInfo.MetaData;
// 			FText ToolTipText;
// 			if (ParameterPanelViewModel->GetCanDeleteParameterAndToolTip(Parameter, ParameterMetaData, ToolTipText))
// 			{
// 				return true;
// 			}
// 		}
// 	}
// 	return false;
// }
// 
// void SNiagaraParameterPanel::OnRequestRenameOnActionNode()
// {
// 	GraphActionMenu->OnRequestRenameOnActionNode();
// }
// 
// bool SNiagaraParameterPanel::CanRequestRenameOnActionNode(TWeakPtr<FGraphActionNode> InSelectedNode) const
// {
// 	if (InSelectedNode.IsValid() && InSelectedNode.Pin()->Actions.Num() == 1 && InSelectedNode.Pin()->Actions[0].IsValid())
// 	{
// 		const FNiagaraScriptVarAndViewInfoAction* SelectedAction = static_cast<FNiagaraScriptVarAndViewInfoAction*>(InSelectedNode.Pin()->Actions[0].Get());
// 		FText BlankText = FText();
// 		return ParameterPanelViewModel->GetCanRenameParameterAndToolTip(
// 			SelectedAction->ScriptVariableAndViewInfo.ScriptVariable,
// 			SelectedAction->ScriptVariableAndViewInfo.MetaData,
// 			TOptional<const FText>(),
// 			BlankText
// 		);
// 	}
// 	
// 	return false;
// }
// 
// bool SNiagaraParameterPanel::CanRequestRenameOnActionNode() const
// {
// 	TArray<TSharedPtr<FEdGraphSchemaAction> > SelectedActions;
// 	GraphActionMenu->GetSelectedActions(SelectedActions);
// 
// 	// If there is anything selected in the GraphActionMenu, check the item for if it can be renamed.
// 	if (SelectedActions.Num() > 0)
// 	{
// 		return GraphActionMenu->CanRequestRenameOnActionNode();
// 	}
// 	return false;
// }
// 
// bool SNiagaraParameterPanel::HandleActionMatchesName(FEdGraphSchemaAction* InAction, const FName& InName) const
// {
// 	return FName(*InAction->GetMenuDescription().ToString()) == InName;
// }
// 
void SNiagaraParameterPanel::Refresh()
{
	bNeedsRefresh = true;
}

bool SNiagaraParameterPanel::SelectParameterEntryByName(const FName& ParameterName) const
{
	const TArray<FNiagaraScriptVariableAndViewInfo>& ViewedParameters = ParameterPanelViewModel->GetViewedParameters();
	const FNiagaraScriptVariableAndViewInfo* ScriptVarToSelect = ViewedParameters.FindByPredicate([ParameterName](const FNiagaraScriptVariableAndViewInfo& ScriptVar) {return ScriptVar.ScriptVariable.GetName() == ParameterName; });
	if (ScriptVarToSelect != nullptr)
	{
		const TArray<FNiagaraScriptVariableAndViewInfo> ItemsToSelect = { *ScriptVarToSelect };
		ItemSelector->SetSelectedItems(ItemsToSelect);
		return true;
	}
	return false;
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
	TArray<TSharedPtr<FNiagaraMenuAction>> TempOutAllActions;

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

		// Create the actual actions and mark them experimental if so.
		for (const FNiagaraMenuActionInfo& ActionInfo : NewParameterMenuActionInfos)
		{
			TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(ActionInfo.CategoryText, ActionInfo.DisplayNameText, ActionInfo.TooltipText, ActionInfo.Grouping, ActionInfo.KeywordsText,
				FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterMenu2::AddParameterSelected, ActionInfo.NewVariable)));

			Action->IsExperimental = ActionInfo.bIsExperimental;
			Action->SetParamterHandle(FNiagaraParameterHandle(ActionInfo.NewVariable.GetName()));

			TempOutAllActions.Add(Action);
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
				Action->SetParamterHandle(FNiagaraParameterHandle(Variable.GetName()));

				TempOutAllActions.Add(Action);
			}
		}
	}

	if (ShowKnownConstantParametersFilter.IsSet())
	{
		const ENiagaraParameterPanelCategory Category = ShowKnownConstantParametersFilter.Get();
		if (Category != ENiagaraParameterPanelCategory::None)
		{
			// Handle engine parameters, but only in read-only contexts.
			if ((!IsParameterRead.IsSet() || (IsParameterRead.IsSet() && IsParameterRead.Get())) &&
				(Category == ENiagaraParameterPanelCategory::Engine ||
				Category == ENiagaraParameterPanelCategory::Owner ||
				Category == ENiagaraParameterPanelCategory::Attributes ||
				Category == ENiagaraParameterPanelCategory::System ||
				Category == ENiagaraParameterPanelCategory::Emitter ))
			{
				const TArray<FNiagaraVariable>& EngineConstants = FNiagaraConstants::GetEngineConstants();
				for (const FNiagaraVariable& Var : EngineConstants)
				{
					const FText CategoryText = LOCTEXT("NiagaraAddEngineParameterMenu", "Engine Constant");
					const FText DisplayName = FText::FromName(Var.GetName());
					const FText Tooltip = FNiagaraConstants::GetEngineConstantDescription(Var);


					FNiagaraVariableMetaData GuessedMetaData;
					FNiagaraEditorUtilities::GetParameterMetaDataFromName(Var.GetName(), GuessedMetaData);
					bool bAdd = Category == ENiagaraParameterPanelCategory::Attributes; // Attributes has all of these!
					if (Category == ENiagaraParameterPanelCategory::Engine && GuessedMetaData.GetScopeName() == FNiagaraConstants::EngineNamespace)
						bAdd = true;
					else if (Category == ENiagaraParameterPanelCategory::Owner && GuessedMetaData.GetScopeName() == FNiagaraConstants::EngineOwnerScopeName)
						bAdd = true;
					else if (Category == ENiagaraParameterPanelCategory::System && GuessedMetaData.GetScopeName() == FNiagaraConstants::EngineSystemScopeName)
						bAdd = true;
					else if (Category == ENiagaraParameterPanelCategory::Emitter && GuessedMetaData.GetScopeName() == FNiagaraConstants::EngineEmitterScopeName)
						bAdd = true;

					if (!bAdd)
						continue;

					TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
						CategoryText, DisplayName, Tooltip, 0, FText::GetEmpty(),
						FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterMenu2::AddParameterSelected, Var)));
					Action->SetParamterHandle(FNiagaraParameterHandle(Var.GetName()));

					TempOutAllActions.Add(Action);
				}
			}

			// Handle particles 
			if (Category == ENiagaraParameterPanelCategory::Particles ||
				Category == ENiagaraParameterPanelCategory::Attributes )
			{
				const TArray<FNiagaraVariable>& Vars = FNiagaraConstants::GetCommonParticleAttributes();

				for (const FNiagaraVariable& Var : Vars)
				{
					const FText CategoryText = LOCTEXT("NiagaraAddCommParticleParameterMenu", "Common Particle Attributes");
					const FText DisplayName = FText::FromName(Var.GetName());
					const FText Tooltip = FNiagaraConstants::GetAttributeDescription(Var);
					
					TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
						CategoryText, DisplayName, Tooltip, 0, FText::GetEmpty(),
						FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterMenu2::AddParameterSelected, Var)));
					Action->SetParamterHandle(FNiagaraParameterHandle(Var.GetName()));

					TempOutAllActions.Add(Action);

				}
			}

		}
	}
	auto SortOutAllActionsPred = [](const TSharedPtr<FNiagaraMenuAction>& A, const TSharedPtr<FNiagaraMenuAction>& B)->bool {
		if (A->GetParameterHandle().IsSet() == false)
		{
			return false;
		}
		else if (B->GetParameterHandle().IsSet() == false)
		{
			return true;
		}
		return FNiagaraEditorUtilities::GetVariableSortPriority(A->GetParameterHandle()->GetParameterHandleString(), B->GetParameterHandle()->GetParameterHandleString());
	};
	TempOutAllActions.Sort(SortOutAllActionsPred);
	for (const TSharedPtr<FNiagaraMenuAction>& Action : TempOutAllActions)
	{
		OutAllActions.AddAction(Action);
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
