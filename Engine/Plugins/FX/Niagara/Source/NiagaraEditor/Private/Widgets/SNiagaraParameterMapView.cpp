// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SNiagaraParameterMapView.h"
#include "SNiagaraParameterMapPaletteItem.h"
#include "NiagaraObjectSelection.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "NiagaraCommon.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraGraph.h"
#include "NiagaraParameterStore.h"
#include "NiagaraNodeWithDynamicPins.h"
#include "NiagaraNodeParameterMapBase.h"
#include "NiagaraActions.h"
#include "SGraphActionMenu.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailsView.h"
#include "DetailLayoutBuilder.h"
#include "NiagaraConstants.h"
#include "EdGraph/EdGraphSchema.h"
#include "Framework/Application/SlateApplication.h"
#include "IAssetTools.h"
#include "AssetRegistryModule.h"
#include "Framework/Commands/GenericCommands.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ScopedTransaction.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraScriptVariable.h"
#include "Widgets/SPanel.h"
#include "Widgets/Layout/SBox.h"
#include "NiagaraSystemEditorData.h"
#include "ViewModels/Stack/NiagaraStackSystemSettingsGroup.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "SNiagaraGraphActionWidget.h"
#include "NiagaraEditorSettings.h"
#include "Classes/EditorStyleSettings.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeEmitter.h"

#define LOCTEXT_NAMESPACE "NiagaraParameterMapView"

FText NiagaraParameterMapSectionID::OnGetSectionTitle(const NiagaraParameterMapSectionID::Type InSection)
{
	TArray<FName> SectionNamespaces;
	OnGetSectionNamespaces(InSection, SectionNamespaces);
	FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(SectionNamespaces);
	if (NamespaceMetadata.IsValid())
	{
		return NamespaceMetadata.DisplayName;
	}
	else if (SectionNamespaces.Num() == 1)
	{
		return FText::FromName(SectionNamespaces[0]);
	}
	else
	{
		return NSLOCTEXT("GraphActionNode", "Unknown", "Unknown");
	}
}

void NiagaraParameterMapSectionID::OnGetSectionNamespaces(const NiagaraParameterMapSectionID::Type InSection, TArray<FName>& OutSectionNamespaces)
{
	switch (InSection)
	{
	case NiagaraParameterMapSectionID::ENGINE:
		OutSectionNamespaces.Add(FNiagaraConstants::EngineNamespace);
		break;
	case NiagaraParameterMapSectionID::EMITTER:
		OutSectionNamespaces.Add(FNiagaraConstants::EmitterNamespace);
		break;
	case NiagaraParameterMapSectionID::MODULE_INPUT:
		OutSectionNamespaces.Add(FNiagaraConstants::ModuleNamespace);
		break;
	case NiagaraParameterMapSectionID::MODULE_OUTPUT:
		OutSectionNamespaces.Add(FNiagaraConstants::OutputNamespace);
		OutSectionNamespaces.Add(FNiagaraConstants::ModuleNamespace);
		break;
	case NiagaraParameterMapSectionID::MODULE_LOCAL:
		OutSectionNamespaces.Add(FNiagaraConstants::LocalNamespace);
		OutSectionNamespaces.Add(FNiagaraConstants::ModuleNamespace);
		break;
	case NiagaraParameterMapSectionID::TRANSIENT:
		OutSectionNamespaces.Add(FNiagaraConstants::TransientNamespace);
		break;
	case NiagaraParameterMapSectionID::DATA_INSTANCE:
		OutSectionNamespaces.Add(FNiagaraConstants::DataInstanceNamespace);
		break;
	case NiagaraParameterMapSectionID::STATIC_SWITCH:
		OutSectionNamespaces.Add(FNiagaraConstants::StaticSwitchNamespace);
		break;
	case NiagaraParameterMapSectionID::SYSTEM:
		OutSectionNamespaces.Add(FNiagaraConstants::SystemNamespace);
		break;
	case NiagaraParameterMapSectionID::PARTICLE:
		OutSectionNamespaces.Add(FNiagaraConstants::ParticleAttributeNamespace);
		break;
	case NiagaraParameterMapSectionID::USER:
		OutSectionNamespaces.Add(FNiagaraConstants::UserNamespace);
		break;
	case NiagaraParameterMapSectionID::PARAMETERCOLLECTION:
		OutSectionNamespaces.Add(FNiagaraConstants::ParameterCollectionNamespace);
		break;
	}
}

NiagaraParameterMapSectionID::Type NiagaraParameterMapSectionID::OnGetSectionFromVariable(const FNiagaraVariable& InVar, bool IsStaticSwitchVariable, FNiagaraParameterHandle& OutParameterHandle, const NiagaraParameterMapSectionID::Type DefaultType)
{
	OutParameterHandle = FNiagaraParameterHandle(InVar.GetName());
	Type SectionID = DefaultType;
	if (IsStaticSwitchVariable)
	{
		SectionID = NiagaraParameterMapSectionID::STATIC_SWITCH;
	}
	else if (OutParameterHandle.IsEmitterHandle())
	{
		SectionID = NiagaraParameterMapSectionID::EMITTER;
	}
	else if (OutParameterHandle.IsModuleHandle())
	{
		SectionID = NiagaraParameterMapSectionID::MODULE_INPUT;
	}
	else if (OutParameterHandle.IsOutputHandle())
	{
		SectionID = NiagaraParameterMapSectionID::MODULE_OUTPUT;
	}
	else if (OutParameterHandle.IsLocalHandle())
	{
		SectionID = NiagaraParameterMapSectionID::MODULE_LOCAL;
	}
	else if (OutParameterHandle.IsUserHandle())
	{
		SectionID = NiagaraParameterMapSectionID::USER;
	}
	else if (OutParameterHandle.IsEngineHandle())
	{
		SectionID = NiagaraParameterMapSectionID::ENGINE;
	}
	else if (OutParameterHandle.IsSystemHandle())
	{
		SectionID = NiagaraParameterMapSectionID::SYSTEM;
	}
	else if (OutParameterHandle.IsParticleAttributeHandle())
	{
		SectionID = NiagaraParameterMapSectionID::PARTICLE;
	}
	else if (OutParameterHandle.IsParameterCollectionHandle())
	{
		SectionID = NiagaraParameterMapSectionID::PARAMETERCOLLECTION;
	}
	else if (OutParameterHandle.IsTransientHandle())
	{
		SectionID = NiagaraParameterMapSectionID::TRANSIENT;
	}
	else if (OutParameterHandle.IsDataInstanceHandle())
	{
		SectionID = NiagaraParameterMapSectionID::DATA_INSTANCE;
	}

	return SectionID;
}

bool NiagaraParameterMapSectionID::GetSectionIsAdvanced(const Type InSection)
{
	TArray<FName> SectionNamespaces;
	OnGetSectionNamespaces(InSection, SectionNamespaces);
	FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(SectionNamespaces);
	if (NamespaceMetadata.IsValid())
	{
		bool bIsAdvanced = false;
		for (const ENiagaraNamespaceMetadataOptions& Option : NamespaceMetadata.Options)
		{
			if (Option == ENiagaraNamespaceMetadataOptions::Advanced)
			{
				return true;
			}
		}
		return false;
	}
	else
	{
		return false;
	}
}

void FNiagaraParameterMapViewCommands::RegisterCommands()
{
	UI_COMMAND(DeleteEntry, "Delete", "Delete this parameter", EUserInterfaceActionType::Button, FInputChord(EKeys::Platform_Delete));
}

SNiagaraParameterMapView::~SNiagaraParameterMapView()
{
	// Unregister all commands for right click on action node
	ToolkitCommands->UnmapAction(FNiagaraParameterMapViewCommands::Get().DeleteEntry);
	ToolkitCommands->UnmapAction(FGenericCommands::Get().Rename);

	TSet<UObject*> Objects = SelectedScriptObjects->GetSelectedObjects();
	for (UObject* Object : Objects)
	{
		if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Object))
		{
			System->GetExposedParameters().RemoveAllOnChangedHandlers(this);
			break;
		}
	}

	EmptyGraphs();
	if (CachedSystem.IsValid())
	{
		CachedSystem->GetExposedParameters().RemoveOnChangedHandler(UserParameterStoreChangedHandle);
		CachedSystem.Reset();
	}

	SelectedScriptObjects->OnSelectedObjectsChanged().RemoveAll(this);
	if (SelectedVariableObjects)
	{
		SelectedVariableObjects->OnSelectedObjectsChanged().RemoveAll(this);
	}
}

const FSlateBrush* SNiagaraParameterMapView::GetViewOptionsBorderBrush()
{
	UNiagaraEditorSettings* Settings = GetMutableDefault<UNiagaraEditorSettings>();
	return Settings->GetDisplayAdvancedParameterPanelCategories()
		? FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Stack.DepressedHighlightedButtonBrush")
		: FEditorStyle::GetBrush("NoBrush");
}

void SNiagaraParameterMapView::Construct(const FArguments& InArgs, const TArray<TSharedRef<FNiagaraObjectSelection>>& InSelectedObjects, const EToolkitType InToolkitType, const TSharedPtr<FUICommandList>& InToolkitCommands)
{
	bNeedsRefresh = false;
	ToolkitType = InToolkitType;
	ToolkitCommands = InToolkitCommands;
	AddParameterButtons.SetNum(NiagaraParameterMapSectionID::Num);
	const FVector2D ViewOptionsShadowOffset = FNiagaraEditorStyle::Get().GetVector("NiagaraEditor.Stack.ViewOptionsShadowOffset");

	SelectedScriptObjects = InSelectedObjects[0];
	SelectedScriptObjects->OnSelectedObjectsChanged().AddSP(this, &SNiagaraParameterMapView::SelectedObjectsChanged);
	if (InSelectedObjects.Num() == 2)
	{
		//SelectedVariableObjects->OnSelectedObjectsChanged().AddSP(this, &SNiagaraParameterMapView::SelectedObjectsChanged);
		SelectedVariableObjects = InSelectedObjects[1];
	}
	
	// Register all commands for right click on action node
	{
		FNiagaraParameterMapViewCommands::Register();
		TSharedPtr<FUICommandList> ToolKitCommandList = ToolkitCommands;
		ToolKitCommandList->MapAction(FNiagaraParameterMapViewCommands::Get().DeleteEntry,
			FExecuteAction::CreateSP(this, &SNiagaraParameterMapView::OnDeleteEntry),
			FCanExecuteAction::CreateSP(this, &SNiagaraParameterMapView::CanDeleteEntry));
		ToolKitCommandList->MapAction(FGenericCommands::Get().Rename,
			FExecuteAction::CreateSP(this, &SNiagaraParameterMapView::OnRequestRenameOnActionNode),
			FCanExecuteAction::CreateSP(this, &SNiagaraParameterMapView::CanRequestRenameOnActionNode));
	}

	Refresh(false);

	SAssignNew(FilterBox, SSearchBox)
		.OnTextChanged(this, &SNiagaraParameterMapView::OnFilterTextChanged);

	// View options
	TSharedRef<SWidget> ViewOptionsWidget = SNew(SBorder)
		.Padding(0)
		.BorderImage_Static(&SNiagaraParameterMapView::GetViewOptionsBorderBrush)
		[
			SNew(SComboButton)
			.ContentPadding(0)
			.ForegroundColor(FSlateColor::UseForeground())
			.ButtonStyle(FEditorStyle::Get(), "ToggleButton")
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ViewOptions")))
			.ToolTipText(LOCTEXT("ViewOptionsToolTip", "View Options"))
			.OnGetMenuContent(this, &SNiagaraParameterMapView::GetViewOptionsMenu)
			.ButtonContent()
			[
				SNew(SOverlay)
				// drop shadow
				+ SOverlay::Slot()
				.VAlign(VAlign_Top)
				.Padding(ViewOptionsShadowOffset.X, ViewOptionsShadowOffset.Y, 0, 0)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("GenericViewButton"))
					.ColorAndOpacity(FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.Stack.ViewOptionsShadowColor"))
				]
				+ SOverlay::Slot()
				.VAlign(VAlign_Top)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("GenericViewButton"))
					.ColorAndOpacity(FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.Stack.FlatButtonColor"))
				]
			]
		];


	// create the main action list piece of this widget
	SAssignNew(GraphActionMenu, SGraphActionMenu, false)
		.OnGetFilterText(this, &SNiagaraParameterMapView::GetFilterText)
		.OnCreateWidgetForAction(this, &SNiagaraParameterMapView::OnCreateWidgetForAction)
		.OnCollectAllActions(this, &SNiagaraParameterMapView::CollectAllActions)
		.OnCollectStaticSections(this, &SNiagaraParameterMapView::CollectStaticSections)
		.OnActionDragged(this, &SNiagaraParameterMapView::OnActionDragged)
		.OnActionSelected(this, &SNiagaraParameterMapView::OnActionSelected)
		.OnActionDoubleClicked(this, &SNiagaraParameterMapView::OnActionDoubleClicked)
		.OnContextMenuOpening(this, &SNiagaraParameterMapView::OnContextMenuOpening)
		.OnCanRenameSelectedAction(this, &SNiagaraParameterMapView::CanRequestRenameOnActionNode)
		.OnGetSectionTitle(this, &SNiagaraParameterMapView::OnGetSectionTitle)
		.OnGetSectionWidget(this, &SNiagaraParameterMapView::OnGetSectionWidget)
		.OnCreateCustomRowExpander_Static(&SNiagaraParameterMapView::CreateCustomActionExpander)
		.OnActionMatchesName(this, &SNiagaraParameterMapView::HandleActionMatchesName)
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
			.AutoHeight()
			[
				SNew(SBorder)
				.Padding(4.0f)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ParameterMapPanel")))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						// Filter Box
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							FilterBox.ToSharedRef()
						]
						// Filter Box View Options
						+SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(4, 0, 0, 0)
						[
							ViewOptionsWidget
						]
					]
				]
			]
		
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				GraphActionMenu.ToSharedRef()
			]
		]
	];
}

void SNiagaraParameterMapView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bNeedsRefresh)
	{
		GraphActionMenu->RefreshAllActions(true);
		bNeedsRefresh = false;
	}
}

bool SNiagaraParameterMapView::ParameterAddEnabled() const
{
	return Graphs.Num() > 0;
}

void SNiagaraParameterMapView::AddParameter(FNiagaraVariable NewVariable)
{
	FNiagaraParameterHandle ParameterHandle;
	bool bSuccess = false;

	if (ToolkitType == SCRIPT)
	{
		if (Graphs.Num() > 0)
		{
			TSet<FName> Names;
			for (const TWeakObjectPtr<UNiagaraGraph>& GraphWeakPtr : Graphs)
			{
				if (GraphWeakPtr.IsValid())
				{
					UNiagaraGraph* Graph = GraphWeakPtr.Get();
					for (const auto& ParameterElement : Graph->GetParameterReferenceMap())
					{
						Names.Add(ParameterElement.Key.GetName());
					}
				}
			}
			const FName NewUniqueName = FNiagaraUtilities::GetUniqueName(NewVariable.GetName(), Names);
			NewVariable.SetName(NewUniqueName);

			FScopedTransaction AddTransaction(LOCTEXT("AddScriptParameterTransaction", "Add parameter to script."));
			for (const TWeakObjectPtr<UNiagaraGraph>& GraphWeakPtr : Graphs)
			{
				if (GraphWeakPtr.IsValid())
				{
					UNiagaraGraph* Graph = GraphWeakPtr.Get();
					Graph->Modify();
					Graph->AddParameter(NewVariable);
					bSuccess = true;
				}
			}
		}
	}
	else if (ToolkitType == SYSTEM)
	{
		UNiagaraSystem* System = CachedSystem.Get();
		if (System != nullptr)
		{
			FScopedTransaction AddTransaction(LOCTEXT("AddSystemParameterTransaction", "Add parameter to system."));
			System->Modify();
			if (NiagaraParameterMapSectionID::OnGetSectionFromVariable(NewVariable, IsStaticSwitchParameter(NewVariable, Graphs), ParameterHandle) == NiagaraParameterMapSectionID::USER)
			{
				UNiagaraSystemEditorData* SystemEditorData = CastChecked<UNiagaraSystemEditorData>(System->GetEditorData(), ECastCheckedType::NullChecked);
				bSuccess = FNiagaraEditorUtilities::AddParameter(NewVariable, System->GetExposedParameters(), *System, SystemEditorData->GetStackEditorData());
			}
			else
			{
				FNiagaraVariable VariableToAdd = NewVariable;
				if (System->EditorOnlyAddedParameters.IndexOf(VariableToAdd) != INDEX_NONE)
				{
					TSet<FName> ParameterNames;
					TArray<FNiagaraVariable> AddedParameterVariables;
					System->EditorOnlyAddedParameters.GetParameters(AddedParameterVariables);
					for (const FNiagaraVariable& AddedParameterVariable : AddedParameterVariables)
					{
						ParameterNames.Add(AddedParameterVariable.GetName());
					}
					FName UniqueName = FNiagaraUtilities::GetUniqueName(VariableToAdd.GetName(), ParameterNames);
					VariableToAdd.SetName(UniqueName);
				}
				bSuccess = System->EditorOnlyAddedParameters.AddParameter(VariableToAdd, false, false);
			}
		}
	}

	if (bSuccess)
	{
		GraphActionMenu->RefreshAllActions(true);
		GraphActionMenu->SelectItemByName(NewVariable.GetName());
		GraphActionMenu->OnRequestRenameOnActionNode();
	}
}

TSharedRef<SWidget> SNiagaraParameterMapView::GetViewOptionsMenu()
{
	FMenuBuilder MenuBuilder(false, nullptr);

	auto ToggleShowAdvancedCategoriesActionLambda = [this]() {
		UNiagaraEditorSettings* Settings = GetMutableDefault<UNiagaraEditorSettings>();
		Settings->SetDisplayAdvancedParameterPanelCategories(!Settings->GetDisplayAdvancedParameterPanelCategories());
		Refresh();
	};

	auto GetShowAdvancedCategoriesCheckStateActionLambda = []() {
		UNiagaraEditorSettings* Settings = GetMutableDefault<UNiagaraEditorSettings>();
		return Settings->GetDisplayAdvancedParameterPanelCategories() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;;
	};

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ShowAdvancedCategoriesLabel", "Show Advanced Categories"),
		LOCTEXT("ShowAdvancedCategoriesToolTip", "Display advanced categories for the parameter panel."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda(ToggleShowAdvancedCategoriesActionLambda),
			FCanExecuteAction(),
			FGetActionCheckState::CreateLambda(GetShowAdvancedCategoriesCheckStateActionLambda)),
		NAME_None,
		EUserInterfaceActionType::Check
	);
		
	return MenuBuilder.MakeWidget();
}

bool SNiagaraParameterMapView::AllowMakeTypeGeneric(const FNiagaraTypeDefinition& InType) const
{
	return InType != FNiagaraTypeDefinition::GetParameterMapDef();
}

bool SNiagaraParameterMapView::AllowMakeTypeAttribute(const FNiagaraTypeDefinition& InType) const
{
	return InType != FNiagaraTypeDefinition::GetParameterMapDef() && InType != FNiagaraTypeDefinition::GetGenericNumericDef();
}

void SNiagaraParameterMapView::OnFilterTextChanged(const FText& InFilterText)
{
	GraphActionMenu->GenerateFilteredItems(false);
}

FText SNiagaraParameterMapView::GetFilterText() const
{
	return FilterBox->GetText();
}

TSharedRef<SWidget> SNiagaraParameterMapView::OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData)
{
	return SNew(SNiagaraParameterMapPalleteItem, InCreateData)
		.OnItemRenamed(this, &SNiagaraParameterMapView::OnPostRenameActionNode);
}

void SNiagaraParameterMapView::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	UNiagaraEditorSettings* EditorSettings = GetMutableDefault<UNiagaraEditorSettings>();
	const bool bDisplayAdvancedCategories = EditorSettings->GetDisplayAdvancedParameterPanelCategories();

	if (Graphs.Num() == 0)
	{
		return;
	}

	TMap<FNiagaraVariable, TArray<FNiagaraGraphParameterReferenceCollection>> ParameterEntries;
	if (ToolkitType == SCRIPT)
	{
		CollectAllActionsForScriptToolkit(ParameterEntries);
	}
	else if (ToolkitType == SYSTEM)
	{
		CollectAllActionsForSystemToolkit(ParameterEntries);
	}

	ParameterEntries.KeySort([](const FNiagaraVariable& A, const FNiagaraVariable& B) { return A.GetName().LexicalLess(B.GetName()); });
	const FText TooltipFormat = LOCTEXT("Parameters", "Name: {0} \nType: {1}");
	for (const auto& ParameterEntry : ParameterEntries)
	{
		const FNiagaraVariable& Parameter = ParameterEntry.Key;
		FNiagaraParameterHandle Handle;
		const NiagaraParameterMapSectionID::Type Section = NiagaraParameterMapSectionID::OnGetSectionFromVariable(Parameter, IsStaticSwitchParameter(Parameter, Graphs), Handle);
		if (Section == NiagaraParameterMapSectionID::NONE)
		{
			continue;
		}
		TArray<NiagaraParameterMapSectionID::Type> ExcludedSystemIds = {
			NiagaraParameterMapSectionID::MODULE_INPUT,
			NiagaraParameterMapSectionID::MODULE_LOCAL,
			NiagaraParameterMapSectionID::DATA_INSTANCE };

		if ((IsSystemToolkit() == false || ExcludedSystemIds.Contains(Section) == false) &&
			(bDisplayAdvancedCategories || NiagaraParameterMapSectionID::GetSectionIsAdvanced(Section) == false))
		{
			const FText Name = FText::FromName(Parameter.GetName());
			const FText Tooltip = FText::Format(TooltipFormat, FText::FromName(Parameter.GetName()), Parameter.GetType().GetNameText());
			TSharedPtr<FNiagaraParameterAction> ParameterAction(new FNiagaraParameterAction(Parameter, ParameterEntry.Value, FText::GetEmpty(), Name, Tooltip, 0, FText(), Section));
			OutAllActions.AddAction(ParameterAction);
		}
	}
}

void SNiagaraParameterMapView::CollectAllActionsForScriptToolkit(TMap<FNiagaraVariable, TArray<FNiagaraGraphParameterReferenceCollection>>& OutParameterEntries)
{
	// For scripts we use the reference maps cached in the graph to collect parameters.
	for (auto& GraphWeakPtr : Graphs)
	{
		if (!GraphWeakPtr.IsValid())
		{
			continue;
		}
		UNiagaraGraph* Graph = GraphWeakPtr.Get();
		for (const TPair<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& ParameterElement : Graph->GetParameterReferenceMap())
		{
			TArray<FNiagaraGraphParameterReferenceCollection>* Found = OutParameterEntries.Find(ParameterElement.Key);
			if (Found)
			{
				Found->Add(ParameterElement.Value);
			}
			else
			{
				TArray<FNiagaraGraphParameterReferenceCollection> Collection;
				Collection.Add(ParameterElement.Value);
				OutParameterEntries.Add(ParameterElement.Key, Collection);
			}
		}
	}
}

void SNiagaraParameterMapView::CollectAllActionsForSystemToolkit(TMap<FNiagaraVariable, TArray<FNiagaraGraphParameterReferenceCollection>>& OutParameterEntries)
{
	// For systems we need to collect the user parameters if a system is selected, and then we use parameter map traversal
	// to find the compile time parameters.
	UNiagaraSystem* System = CachedSystem.Get();
	if (System != nullptr)
	{
		// Collect user parameters.
		TArray<FNiagaraVariable> ExposedVars;
		System->GetExposedParameters().GetParameters(ExposedVars);
		for (const FNiagaraVariable& ExposedVar : ExposedVars)
		{
			TArray<FNiagaraGraphParameterReferenceCollection>* Found = OutParameterEntries.Find(ExposedVar);
			if (!Found)
			{
				TArray<FNiagaraGraphParameterReferenceCollection> Collection = { FNiagaraGraphParameterReferenceCollection(true) };
				OutParameterEntries.Add(ExposedVar, Collection);
			}
		}

		// Collect manually added parameters.
		TArray<FNiagaraVariable> AddedVars;
		System->EditorOnlyAddedParameters.GetParameters(AddedVars);
		for (const FNiagaraVariable& AddedVar : AddedVars)
		{
			TArray<FNiagaraGraphParameterReferenceCollection>* Found = OutParameterEntries.Find(AddedVar);
			if (!Found)
			{
				TArray<FNiagaraGraphParameterReferenceCollection> Collection = { FNiagaraGraphParameterReferenceCollection(true) };
				OutParameterEntries.Add(AddedVar, Collection);
			}
		}
	}

	for (TWeakObjectPtr<UNiagaraGraph> GraphWeak : Graphs)
	{
		UNiagaraGraph* Graph = GraphWeak.Get();
		if (Graph == nullptr)
		{
			continue;
		}

		TArray<UNiagaraNodeOutput*> OutputNodes;
		Graph->GetNodesOfClass<UNiagaraNodeOutput>(OutputNodes);
		for (UNiagaraNodeOutput* OutputNode : OutputNodes)
		{
			UNiagaraNode* NodeToTraverse = OutputNode;
			if (OutputNode->GetUsage() == ENiagaraScriptUsage::SystemSpawnScript || OutputNode->GetUsage() == ENiagaraScriptUsage::SystemUpdateScript)
			{
				// Traverse past the emitter nodes, otherwise the system scripts will pick up all of the emitter and particle script parameters.
				UEdGraphPin* InputPin = FNiagaraStackGraphUtilities::GetParameterMapInputPin(*NodeToTraverse);
				while (NodeToTraverse != nullptr && InputPin != nullptr && InputPin->LinkedTo.Num() == 1 &&
					(NodeToTraverse->IsA<UNiagaraNodeOutput>() || NodeToTraverse->IsA<UNiagaraNodeEmitter>()))
				{
					NodeToTraverse = Cast<UNiagaraNode>(InputPin->LinkedTo[0]->GetOwningNode());
					InputPin = NodeToTraverse != nullptr ? FNiagaraStackGraphUtilities::GetParameterMapInputPin(*NodeToTraverse) : nullptr;
				}
			}

			if (NodeToTraverse == nullptr)
			{
				continue;
			}

			bool bIgnoreDisabled = true;
			FNiagaraParameterMapHistoryBuilder Builder;
			UNiagaraEmitter* GraphOwningEmitter = Graph->GetTypedOuter<UNiagaraEmitter>();
			FCompileConstantResolver ConstantResolver = GraphOwningEmitter != nullptr
				? FCompileConstantResolver(GraphOwningEmitter)
				: FCompileConstantResolver();

			Builder.SetIgnoreDisabled(bIgnoreDisabled);
			Builder.ConstantResolver = ConstantResolver;
			NodeToTraverse->BuildParameterMapHistory(Builder, true, false);
			
			TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection> ReferenceCollectionsForTraversedNode;
			if (Builder.Histories.Num() == 1)
			{
				for (int32 VariableIndex = 0; VariableIndex < Builder.Histories[0].Variables.Num(); VariableIndex++)
				{
					FNiagaraVariable& HistoryVariable = Builder.Histories[0].Variables[VariableIndex];
					FNiagaraGraphParameterReferenceCollection* ReferenceCollection = ReferenceCollectionsForTraversedNode.Find(HistoryVariable);
					if (ReferenceCollection == nullptr)
					{
						FNiagaraGraphParameterReferenceCollection NewReferenceCollection(false);
						NewReferenceCollection.Graph = Graph;
						ReferenceCollection = &ReferenceCollectionsForTraversedNode.Add(HistoryVariable, NewReferenceCollection);
					}

					TArray<TTuple<const UEdGraphPin*, const UEdGraphPin*>>& ReadHistory = Builder.Histories[0].PerVariableReadHistory[VariableIndex];
					for (const TTuple<const UEdGraphPin*, const UEdGraphPin*>& Read : ReadHistory)
					{
						if (Read.Key->GetOwningNode() != nullptr)
						{
							ReferenceCollection->ParameterReferences.Add(FNiagaraGraphParameterReference(Read.Key->PersistentGuid, Read.Key->GetOwningNode()));
						}
					}

					TArray<const UEdGraphPin*>& WriteHistory = Builder.Histories[0].PerVariableWriteHistory[VariableIndex];
					for (const UEdGraphPin* Write : WriteHistory)
					{
						if (Write->GetOwningNode() != nullptr)
						{
							ReferenceCollection->ParameterReferences.Add(FNiagaraGraphParameterReference(Write->PersistentGuid, Write->GetOwningNode()));
						}
					}
				}
			}

			for (const TPair<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& ReferenceCollectionForOutputNode : ReferenceCollectionsForTraversedNode)
			{
				OutParameterEntries.FindOrAdd(ReferenceCollectionForOutputNode.Key).Add(ReferenceCollectionForOutputNode.Value);
			}
		}
	}
}

void SNiagaraParameterMapView::CollectStaticSections(TArray<int32>& StaticSectionIDs)
{
	if (IsSystemToolkit())
	{
		StaticSectionIDs.Add(NiagaraParameterMapSectionID::USER);
	}
	else
	{
		StaticSectionIDs.Add(NiagaraParameterMapSectionID::MODULE_INPUT);
		StaticSectionIDs.Add(NiagaraParameterMapSectionID::MODULE_LOCAL);
		StaticSectionIDs.Add(NiagaraParameterMapSectionID::STATIC_SWITCH);
		StaticSectionIDs.Add(NiagaraParameterMapSectionID::DATA_INSTANCE);
	}
	StaticSectionIDs.Add(NiagaraParameterMapSectionID::MODULE_OUTPUT);
	StaticSectionIDs.Add(NiagaraParameterMapSectionID::ENGINE);
	StaticSectionIDs.Add(NiagaraParameterMapSectionID::PARAMETERCOLLECTION);
	StaticSectionIDs.Add(NiagaraParameterMapSectionID::SYSTEM);
	StaticSectionIDs.Add(NiagaraParameterMapSectionID::EMITTER);
	StaticSectionIDs.Add(NiagaraParameterMapSectionID::PARTICLE);
	StaticSectionIDs.Add(NiagaraParameterMapSectionID::TRANSIENT);

	UNiagaraEditorSettings* EditorSettings = GetMutableDefault<UNiagaraEditorSettings>();
	if (EditorSettings->GetDisplayAdvancedParameterPanelCategories() == false)
	{
		for (int i = StaticSectionIDs.Num() - 1; i > 0; --i)
		{
			if (NiagaraParameterMapSectionID::GetSectionIsAdvanced(NiagaraParameterMapSectionID::Type(StaticSectionIDs[i])))
			{
				StaticSectionIDs.RemoveAt(i);
			}
		}
	}
}

FReply SNiagaraParameterMapView::OnActionDragged(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, const FPointerEvent& MouseEvent)
{
	TSharedPtr<FEdGraphSchemaAction> InAction(InActions.Num() > 0 ? InActions[0] : NULL);
	if (InAction.IsValid())
	{
		FNiagaraParameterAction* ParameterAction = (FNiagaraParameterAction*)InAction.Get();
		if (ParameterAction)
		{
			if (IsScriptToolkit())
			{
				TSharedRef<FNiagaraParameterGraphDragOperation> DragOperation = FNiagaraParameterGraphDragOperation::New(InAction);
				DragOperation->SetAltDrag(MouseEvent.IsAltDown());
				DragOperation->SetCtrlDrag(MouseEvent.IsLeftControlDown() || MouseEvent.IsRightControlDown());
				return FReply::Handled().BeginDragDrop(DragOperation);
			}
			else if (IsSystemToolkit())
			{
				TSharedRef<FNiagaraParameterDragOperation> DragOperation = MakeShared<FNiagaraParameterDragOperation>(InAction);
				DragOperation->CurrentHoverText = InAction->GetMenuDescription();
				DragOperation->SetupDefaults();
				DragOperation->Construct();
				return FReply::Handled().BeginDragDrop(DragOperation);
			}
		}
	}

	return FReply::Handled();
}

void SNiagaraParameterMapView::OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& InActions, ESelectInfo::Type InSelectionType)
{
	if (!IsScriptToolkit())
	{
		// Don't accept any input for SystemToolkits, as there's no parameters panel there
		return;
	}
	
	// TODO: Can there be multiple actions and graphs? 
	if (InActions.Num() == 1 && InActions[0].IsValid() && Graphs.Num() > 0 && Graphs[0].IsValid()) 
	{
		if (FNiagaraParameterAction* Action = (FNiagaraParameterAction*)InActions[0].Get())
		{
			if (UNiagaraScriptVariable* Variable = Graphs[0]->GetScriptVariable(Action->Parameter))
			{
				SelectedVariableObjects->SetSelectedObject(Variable);
				return;
			}
		}
	} 
	
	// If a variable wasn't selected just clear the current selection
	// TODO: Get proper clearing to work. Current there's no way to clear while clicking on an empty location in the graph area
	if (SelectedVariableObjects.IsValid())
	{
		SelectedVariableObjects->ClearSelectedObjects();
	}
}

void SNiagaraParameterMapView::OnActionDoubleClicked(const TArray< TSharedPtr<FEdGraphSchemaAction> >& InActions)
{

}

TSharedPtr<SWidget> SNiagaraParameterMapView::OnContextMenuOpening()
{
	// Check if the selected action is valid for a context menu
	if (SelectionHasContextMenu())
	{
		const bool bShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, ToolkitCommands);
		MenuBuilder.BeginSection("Edit", LOCTEXT("EditMenuHeader", "Edit"));
		{
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename, NAME_None, LOCTEXT("Rename", "Rename"), LOCTEXT("Rename_Tooltip", "Renames this parameter"));
			MenuBuilder.AddMenuEntry(FNiagaraParameterMapViewCommands::Get().DeleteEntry);
		}
		MenuBuilder.EndSection();

		TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
		GraphActionMenu->GetSelectedActions(SelectedActions);

		if (SelectedActions.Num() == 1)
		{
			TSharedPtr<FNiagaraParameterAction> SelectedNiagaraAction = StaticCastSharedPtr<FNiagaraParameterAction>(SelectedActions[0]);
			if (SelectedNiagaraAction.IsValid())
			{
				MenuBuilder.BeginSection("NamespaceModifier", LOCTEXT("NamespaceModifierMenuSection", "Parameter Modifiers"));
				{
					MenuBuilder.AddMenuEntry(
						LOCTEXT("AddNamespaceModifier", "Add a modifier to this parameter."),
						FText(),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateSP(this, &SNiagaraParameterMapView::OnAddNamespaceModifier),
							FCanExecuteAction()));
					MenuBuilder.AddMenuEntry(
						LOCTEXT("RemoveNamespaceModifier", "Remove the modifier from this parameter."),
						FText(),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateSP(this, &SNiagaraParameterMapView::OnRemoveNamespaceModifier),
							FCanExecuteAction()));
					MenuBuilder.AddMenuEntry(
						LOCTEXT("EditNamespaceModifier", "Edit the modifier for this parameter."),
						FText(),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateSP(this, &SNiagaraParameterMapView::OnEditNamespaceModifier),
							FCanExecuteAction()));
				}
				MenuBuilder.EndSection();
			}
		}
		return MenuBuilder.MakeWidget();
	}

	return SNullWidget::NullWidget;
}

FText SNiagaraParameterMapView::OnGetSectionTitle(int32 InSectionID)
{
	return NiagaraParameterMapSectionID::OnGetSectionTitle((NiagaraParameterMapSectionID::Type)InSectionID);
}

TSharedRef<SWidget> SNiagaraParameterMapView::OnGetSectionWidget(TSharedRef<SWidget> RowWidget, int32 InSectionID)
{
	if (InSectionID == NiagaraParameterMapSectionID::STATIC_SWITCH)
	{
		TSharedPtr<SBox> Empty;
		SAssignNew(Empty, SBox);
		return Empty.ToSharedRef();
	}

	TWeakPtr<SWidget> WeakRowWidget = RowWidget;
	FText AddNewText = LOCTEXT("AddNewParameter", "Add Parameter");
	FName MetaDataTag = TEXT("AddNewParameter");
	return CreateAddToSectionButton((NiagaraParameterMapSectionID::Type) InSectionID, WeakRowWidget, AddNewText, MetaDataTag);
}

TSharedRef<SWidget> SNiagaraParameterMapView::CreateAddToSectionButton(const NiagaraParameterMapSectionID::Type InSection, TWeakPtr<SWidget> WeakRowWidget, FText AddNewText, FName MetaDataTag)
{
	TSharedPtr<SComboButton> Button;
	SAssignNew(Button, SComboButton)
	.ButtonStyle(FEditorStyle::Get(), "RoundButton")
	.ForegroundColor(FEditorStyle::GetSlateColor("DefaultForeground"))
	.ContentPadding(FMargin(2, 0))
	.OnGetMenuContent(this, &SNiagaraParameterMapView::OnGetParameterMenu, InSection)
	.IsEnabled(this, &SNiagaraParameterMapView::ParameterAddEnabled)
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
		.Padding(FMargin(2,0,0,0))
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
			.Text(AddNewText)
			.Visibility(this, &SNiagaraParameterMapView::OnAddButtonTextVisibility, WeakRowWidget, InSection)
			.ShadowOffset(FVector2D(1,1))
		]
	];
	AddParameterButtons[InSection] = Button;

	return Button.ToSharedRef();
}

bool SNiagaraParameterMapView::SelectionHasContextMenu() const
{
	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);
	for (TSharedPtr<FEdGraphSchemaAction> Action : SelectedActions)
	{
		TSharedPtr<FNiagaraParameterAction> NiagaraAction = StaticCastSharedPtr<FNiagaraParameterAction>(Action);
		if (NiagaraAction && IsStaticSwitchParameter(NiagaraAction->GetParameter(), Graphs))
		{
			return false;
		}
	}
	return SelectedActions.Num() > 0;
}

TSharedRef<SWidget> SNiagaraParameterMapView::OnGetParameterMenu(const NiagaraParameterMapSectionID::Type InSection)
{
	bool bTypeIsAttribute = true; 	// Leaving around the old generic path in case it is needed in the future.

	TSharedRef<SNiagaraAddParameterMenu> MenuWidget = SNew(SNiagaraAddParameterMenu, Graphs)
		.OnAddParameter(this, &SNiagaraParameterMapView::AddParameter)
		.OnAllowMakeType(this, bTypeIsAttribute ? &SNiagaraParameterMapView::AllowMakeTypeAttribute : &SNiagaraParameterMapView::AllowMakeTypeGeneric)
		.Section(InSection)
		.ShowNamespaceCategory(false)
		.ShowGraphParameters(false)
		.AutoExpandMenu(true);

	AddParameterButtons[InSection]->SetMenuContentWidgetToFocus(MenuWidget->GetSearchBox()->AsShared());
	return MenuWidget;
}

EVisibility SNiagaraParameterMapView::OnAddButtonTextVisibility(TWeakPtr<SWidget> RowWidget, const NiagaraParameterMapSectionID::Type InSection) const
{
	return EVisibility::Collapsed; // RowWidget.Pin()->IsHovered() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

void SNiagaraParameterMapView::Refresh(bool bRefreshMenu/* = true*/)
{
	EmptyGraphs();
	if (CachedSystem.IsValid())
	{
		CachedSystem->GetExposedParameters().RemoveOnChangedHandler(UserParameterStoreChangedHandle);
		CachedSystem.Reset();
	}

	TSet<UObject*> Objects = SelectedScriptObjects->GetSelectedObjects();
	for (UObject* Object : Objects)
	{
		if (UNiagaraScript* Script = Cast<UNiagaraScript>(Object))
		{
			AddGraph(Script->GetSource());
			break;
		}
		else if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Object))
		{
			AddGraph(Emitter->GraphSource);
		}
		else if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Object))
		{
			CachedSystem = System;
			AddGraph(System->GetSystemSpawnScript()->GetSource());
			UserParameterStoreChangedHandle = System->GetExposedParameters().AddOnChangedHandler(
				FNiagaraParameterStore::FOnChanged::FDelegate::CreateSP(this, &SNiagaraParameterMapView::OnUserParameterStoreChanged));
		}
	}

	if (bRefreshMenu)
	{
		GraphActionMenu->RefreshAllActions(true);
	}
}

void SNiagaraParameterMapView::SelectedObjectsChanged()
{
	Refresh(true);
}

void SNiagaraParameterMapView::EmptyGraphs()
{
	checkf(Graphs.Num() == OnGraphChangedHandles.Num() && Graphs.Num() == OnRecompileHandles.Num(), TEXT("Graphs and change delegates out of sync!"));
	for (int GraphIndex = 0; GraphIndex < Graphs.Num(); ++GraphIndex)
	{
		if (Graphs[GraphIndex].IsValid())
		{
			Graphs[GraphIndex]->RemoveOnGraphChangedHandler(OnGraphChangedHandles[GraphIndex]);
			Graphs[GraphIndex]->RemoveOnGraphNeedsRecompileHandler(OnRecompileHandles[GraphIndex]);
		}
	}
	Graphs.Empty();
	OnGraphChangedHandles.Empty();
	OnRecompileHandles.Empty();
}

void SNiagaraParameterMapView::AddGraph(UNiagaraGraph* Graph)
{
	if (Graph && Graphs.Contains(Graph) == false)
	{
		Graphs.Add(Graph);
		FDelegateHandle OnGraphChangedHandle = Graph->AddOnGraphChangedHandler(
			FOnGraphChanged::FDelegate::CreateRaw(this, &SNiagaraParameterMapView::OnGraphChanged));
		FDelegateHandle OnRecompileHandle = Graph->AddOnGraphNeedsRecompileHandler(
			FOnGraphChanged::FDelegate::CreateRaw(this, &SNiagaraParameterMapView::OnGraphChanged));

		OnGraphChangedHandles.Add(OnGraphChangedHandle);
		OnRecompileHandles.Add(OnRecompileHandle);
	}
}

void SNiagaraParameterMapView::AddGraph(UNiagaraScriptSourceBase* SourceBase)
{
	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(SourceBase);
	if (Source)
	{
		AddGraph(Source->NodeGraph);
	}
}

void SNiagaraParameterMapView::OnGraphChanged(const FEdGraphEditAction& InAction)
{
	RefreshActions();
}

void SNiagaraParameterMapView::OnUserParameterStoreChanged()
{
	if (CachedSystem.IsValid())
	{
		RefreshActions();
	}
}

void SNiagaraParameterMapView::OnDeleteEntry()
{
	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);
	
	for (auto& Action : SelectedActions)
	{
		TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(Action);
		if (ParameterAction.Get())
		{
			FNiagaraParameterHandle ParameterHandle;
			FNiagaraVariable Parameter = ParameterAction->GetParameter();
			if (NiagaraParameterMapSectionID::OnGetSectionFromVariable(Parameter, IsStaticSwitchParameter(Parameter, Graphs), ParameterHandle) == NiagaraParameterMapSectionID::USER)
			{
				for (UObject* Object : SelectedScriptObjects->GetSelectedObjects())
				{
					if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Object))
					{
						System->GetExposedParameters().RemoveParameter(Parameter);
					}
				}
			}

			FScopedTransaction RemoveParametersWithPins(LOCTEXT("RemoveParametersWithPins", "Remove parameter and referenced pins"));
			for (const TWeakObjectPtr<UNiagaraGraph>& GraphWeakPtr : Graphs)
			{
				if (GraphWeakPtr.IsValid())
				{
					UNiagaraGraph* Graph = GraphWeakPtr.Get();
					Graph->RemoveParameter(Parameter);
				}
			}
		}
	}
}

bool SNiagaraParameterMapView::CanDeleteEntry() const
{
	return true;
}

void SNiagaraParameterMapView::OnRequestRenameOnActionNode()
{
	// Attempt to rename in both menus, only one of them will have anything selected
	GraphActionMenu->OnRequestRenameOnActionNode();
}


bool SNiagaraParameterMapView::CanRequestRenameOnActionNode(TWeakPtr<struct FGraphActionNode> InSelectedNode) const
{
	return true;
}

bool SNiagaraParameterMapView::CanRequestRenameOnActionNode() const
{
	TArray<TSharedPtr<FEdGraphSchemaAction> > SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);

	// If there is anything selected in the GraphActionMenu, check the item for if it can be renamed.
	if (SelectedActions.Num())
	{
		return GraphActionMenu->CanRequestRenameOnActionNode();
	}
	return false;
}

void SNiagaraParameterMapView::OnPostRenameActionNode(const FText& InText, FNiagaraParameterAction& InAction)
{
	const FName NewName = FName(*InText.ToString());
	if (!InAction.Parameter.GetName().IsEqual(NewName, ENameCase::CaseSensitive))
	{
		FNiagaraParameterHandle ParameterHandle;
		if (NiagaraParameterMapSectionID::OnGetSectionFromVariable(InAction.Parameter, IsStaticSwitchParameter(InAction.Parameter, Graphs), ParameterHandle) == NiagaraParameterMapSectionID::USER)
		{
			// Check if the new name is also an user variable.
			const FNiagaraVariable NewParameterValidTest = FNiagaraVariable(InAction.Parameter.GetType(), NewName);
			if (NiagaraParameterMapSectionID::OnGetSectionFromVariable(NewParameterValidTest, IsStaticSwitchParameter(NewParameterValidTest, Graphs), ParameterHandle) == NiagaraParameterMapSectionID::USER)
			{
				for (UObject* Object : SelectedScriptObjects->GetSelectedObjects())
				{
					if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Object))
					{
						System->GetExposedParameters().RenameParameter(InAction.Parameter, NewName);
					}
				}
			}
		}

		if (Graphs.Num() > 0)
		{
			for (const TWeakObjectPtr<UNiagaraGraph>& Graph : Graphs)
			{
				if (Graph.IsValid())
				{
					Graph.Get()->RenameParameter(InAction.Parameter, NewName);
				}
			}
		}
	}
}

void SNiagaraParameterMapView::OnAddNamespaceModifier()
{
	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);
	if (SelectedActions.Num() == 1)
	{
		TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(SelectedActions[0]);
		if (ParameterAction.IsValid())
		{
			FString ParameterNameString = ParameterAction->Parameter.GetName().ToString();
			TArray<FString> NameParts;
			ParameterNameString.ParseIntoArray(NameParts, TEXT("."));
			if (NameParts.Num() == 2)
			{
				NameParts.Insert(FNiagaraConstants::ModuleNamespace.ToString(), 1);
				FName NewName = *FString::Join(NameParts, TEXT("."));
				if (Graphs.Num() > 0)
				{
					for (const TWeakObjectPtr<UNiagaraGraph>& Graph : Graphs)
					{
						if (Graph.IsValid())
						{
							Graph.Get()->RenameParameter(ParameterAction->Parameter, NewName);
						}
					}
				}
			}
		}
	}
}

void SNiagaraParameterMapView::OnRemoveNamespaceModifier()
{
	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);
	if (SelectedActions.Num() == 1)
	{
		TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(SelectedActions[0]);
		if (ParameterAction.IsValid())
		{
			FString ParameterNameString = ParameterAction->Parameter.GetName().ToString();
			TArray<FString> NameParts;
			ParameterNameString.ParseIntoArray(NameParts, TEXT("."));
			if (NameParts.Num() == 3)
			{
				NameParts.RemoveAt(1);
				FName NewName = *FString::Join(NameParts, TEXT("."));
				if (Graphs.Num() > 0)
				{
					for (const TWeakObjectPtr<UNiagaraGraph>& Graph : Graphs)
					{
						if (Graph.IsValid())
						{
							Graph.Get()->RenameParameter(ParameterAction->Parameter, NewName);
						}
					}
				}
			}
		}
	}
}

void SNiagaraParameterMapView::OnEditNamespaceModifier()
{
	TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
	GraphActionMenu->GetSelectedActions(SelectedActions);
	if (SelectedActions.Num() == 1)
	{
		TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(SelectedActions[0]);
		if (ParameterAction.IsValid())
		{
			ParameterAction->bNamespaceModifierRenamePending = true;
		}
	}
}

bool SNiagaraParameterMapView::IsSystemToolkit()
{
	return ToolkitType == EToolkitType::SYSTEM;
}

bool SNiagaraParameterMapView::IsScriptToolkit()
{
	return ToolkitType == EToolkitType::SCRIPT;
}

bool SNiagaraParameterMapView::HandleActionMatchesName(FEdGraphSchemaAction* InAction, const FName& InName) const
{
	return FName(*InAction->GetMenuDescription().ToString()) == InName;
}

void SNiagaraParameterMapView::RefreshActions()
{
	bNeedsRefresh = true;
}

bool SNiagaraParameterMapView::IsStaticSwitchParameter(const FNiagaraVariable& Variable, const TArray<TWeakObjectPtr<UNiagaraGraph>>& Graphs)
{
	for (auto& GraphWeakPtr : Graphs)
	{
		if (UNiagaraGraph* Graph = GraphWeakPtr.Get())
		{
			TArray<FNiagaraVariable> SwitchInputs = Graph->FindStaticSwitchInputs();
			if (SwitchInputs.Contains(Variable))
			{
				return true;
			}
		}
	}
	return false;
}

/************************************************************************/
/* SNiagaraAddParameterMenu                                             */
/************************************************************************/
void SNiagaraAddParameterMenu::Construct(const FArguments& InArgs, TArray<TWeakObjectPtr<UNiagaraGraph>> InGraphs)
{
	this->OnAddParameter = InArgs._OnAddParameter;
	this->OnCollectCustomActions = InArgs._OnCollectCustomActions;
	this->OnAllowMakeType = InArgs._OnAllowMakeType;
	this->Section = InArgs._Section;
	this->AllowCreatingNew = InArgs._AllowCreatingNew;
	this->ShowNamespaceCategory = InArgs._ShowNamespaceCategory;
	this->ShowGraphParameters = InArgs._ShowGraphParameters;
	this->AutoExpandMenu = InArgs._AutoExpandMenu;
	this->IsParameterRead = InArgs._IsParameterRead;

	Graphs = InGraphs;

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
				.OnActionSelected(this, &SNiagaraAddParameterMenu::OnActionSelected)
				.OnCollectAllActions(this, &SNiagaraAddParameterMenu::CollectAllActions)
				.AutoExpandActionMenu(AutoExpandMenu.Get())
				.ShowFilterTextBox(true)
				.OnCreateCustomRowExpander_Static(&SNiagaraParameterMapView::CreateCustomActionExpander)
				.OnCreateWidgetForAction_Lambda([](const FCreateWidgetForActionData* InData)
				{
					return SNew(SNiagaraGraphActionWidget, InData);
				})
			]
		]
	];
}

TSharedRef<SEditableTextBox> SNiagaraAddParameterMenu::GetSearchBox()
{
	return GraphMenu->GetFilterTextBox();
}

void SNiagaraAddParameterMenu::OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions, ESelectInfo::Type InSelectionType)
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

void SNiagaraAddParameterMenu::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
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

	auto CanCollectSection = [&](const NiagaraParameterMapSectionID::Type GivenSectionID)
	{
		NiagaraParameterMapSectionID::Type ID = Section.Get();
		return ID == NiagaraParameterMapSectionID::NONE || (ID != NiagaraParameterMapSectionID::NONE && ID == GivenSectionID);
	};

	TArray<NiagaraParameterMapSectionID::Type> IDsExcluded;
	// If this is a write node, exclude any read-only vars.
	if (!IsParameterRead.Get())
	{
		IDsExcluded.Add(NiagaraParameterMapSectionID::USER);
		IDsExcluded.Add(NiagaraParameterMapSectionID::ENGINE);
		IDsExcluded.Add(NiagaraParameterMapSectionID::PARAMETERCOLLECTION);
	}

	// If this doesn't have particles in the script, exclude reading or writing them.
	for (TWeakObjectPtr<UNiagaraGraph>& GraphWeakPtr : Graphs)
	{
		UNiagaraGraph* Graph = GraphWeakPtr.Get();
		bool IsModule = Graph->FindOutputNode(ENiagaraScriptUsage::Module) != nullptr || Graph->FindOutputNode(ENiagaraScriptUsage::DynamicInput) != nullptr
			|| Graph->FindOutputNode(ENiagaraScriptUsage::Function) != nullptr;

		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Graph->GetOuter());
		if (Source && IsModule)
		{
			UNiagaraScript* Script = Cast<UNiagaraScript>(Source->GetOuter());
			if (Script)
			{
				TArray<ENiagaraScriptUsage> Usages = Script->GetSupportedUsageContexts();
				if (!Usages.Contains(ENiagaraScriptUsage::ParticleEventScript) && 
					!Usages.Contains(ENiagaraScriptUsage::ParticleSpawnScript) && 
					!Usages.Contains(ENiagaraScriptUsage::ParticleUpdateScript))
				{
					IDsExcluded.Add(NiagaraParameterMapSectionID::PARTICLE);
				}

				if (!IsParameterRead.Get())
				{
					if (!Usages.Contains(ENiagaraScriptUsage::SystemSpawnScript) &&
						!Usages.Contains(ENiagaraScriptUsage::SystemUpdateScript))
					{
						IDsExcluded.Add(NiagaraParameterMapSectionID::SYSTEM);
					}

					if (!Usages.Contains(ENiagaraScriptUsage::EmitterSpawnScript) &&
						!Usages.Contains(ENiagaraScriptUsage::EmitterUpdateScript))
					{
						IDsExcluded.Add(NiagaraParameterMapSectionID::EMITTER);
					}
				}
			}
		}
	}
	// Particle
	if (CanCollectSection(NiagaraParameterMapSectionID::PARTICLE) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::PARTICLE))
	{
		const FText Category = ShowNamespaceCategory.Get() ? NiagaraParameterMapSectionID::OnGetSectionTitle(NiagaraParameterMapSectionID::PARTICLE) : LOCTEXT("UseExistingParticleAttribute", "Use Existing");
		TArray<FNiagaraVariable> Variables;
		Variables = FNiagaraConstants::GetCommonParticleAttributes();
		AddParameterGroup(OutAllActions, Variables, NiagaraParameterMapSectionID::PARTICLE, Category, FString(), true, false);
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::PARTICLE);
	}

	// Emitter
	if (CanCollectSection(NiagaraParameterMapSectionID::EMITTER) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::EMITTER))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::EMITTER);
	}

	// Module
	if (CanCollectSection(NiagaraParameterMapSectionID::MODULE_INPUT) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::MODULE_INPUT))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::MODULE_INPUT);
	}

	// Module Output
	if (CanCollectSection(NiagaraParameterMapSectionID::MODULE_OUTPUT) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::MODULE_OUTPUT))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::MODULE_OUTPUT);
	}

	// Module Local
	if (CanCollectSection(NiagaraParameterMapSectionID::MODULE_LOCAL) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::MODULE_LOCAL))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::MODULE_LOCAL);
	}

	// Transient
	if (CanCollectSection(NiagaraParameterMapSectionID::TRANSIENT) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::TRANSIENT))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::TRANSIENT);
	}

	// System
	if (CanCollectSection(NiagaraParameterMapSectionID::SYSTEM) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::SYSTEM))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::SYSTEM);
	}

	// User
	if (CanCollectSection(NiagaraParameterMapSectionID::USER) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::USER))
	{
		CollectMakeNew(OutAllActions, NiagaraParameterMapSectionID::USER);
	}

	// Parameter collections
	if (CanCollectSection(NiagaraParameterMapSectionID::PARAMETERCOLLECTION) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::PARAMETERCOLLECTION))
	{
		CollectParameterCollectionsActions(OutAllActions);
	}

	// Engine
	if (CanCollectSection(NiagaraParameterMapSectionID::ENGINE) && !IDsExcluded.Contains(NiagaraParameterMapSectionID::ENGINE))
	{
		const FText Category = NiagaraParameterMapSectionID::OnGetSectionTitle(NiagaraParameterMapSectionID::ENGINE);
		TArray<FNiagaraVariable> Variables = FNiagaraConstants::GetEngineConstants();
		AddParameterGroup(OutAllActions,
			Variables,
			NiagaraParameterMapSectionID::ENGINE,
			ShowNamespaceCategory.Get() ? NiagaraParameterMapSectionID::OnGetSectionTitle(NiagaraParameterMapSectionID::ENGINE) : FText::GetEmpty(),
			FString(), true, false);
	}

	// Collect parameter actions
	if (ShowGraphParameters.Get())
	{
		for (TWeakObjectPtr<UNiagaraGraph>& Graph : Graphs)
		{
			TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection> ParameterEntries = Graph.Get()->GetParameterReferenceMap();
			ParameterEntries.KeySort([](const FNiagaraVariable& A, const FNiagaraVariable& B) { return A.GetName().LexicalLess(B.GetName()); });

			for (const auto& ParameterEntry : ParameterEntries)
			{
				const FNiagaraVariable& Parameter = ParameterEntry.Key;
				FNiagaraParameterHandle Handle;
				bool IsStaticSwitch = SNiagaraParameterMapView::IsStaticSwitchParameter(Parameter, Graphs);
				const NiagaraParameterMapSectionID::Type ParameterSectionID = NiagaraParameterMapSectionID::OnGetSectionFromVariable(Parameter, IsStaticSwitch, Handle);
				if (CanCollectSection(ParameterSectionID))
				{
					if (IDsExcluded.Contains(ParameterSectionID))
					{
						continue;
					}

					const FText Category = ShowNamespaceCategory.Get() ? NiagaraParameterMapSectionID::OnGetSectionTitle(ParameterSectionID) : FText::GetEmpty();
					const FText DisplayName = FText::FromName(Parameter.GetName());

					// Only add this action if it isn't already in the list.
					bool bUnique = true;
					for (int32 Index = 0; Index < OutAllActions.GetNumActions(); Index++)
					{
						const FGraphActionListBuilderBase::ActionGroup& ActionGroup = OutAllActions.GetAction(Index);
						for (const TSharedPtr<FEdGraphSchemaAction>& SchemaAction : ActionGroup.Actions)
						{
							if (SchemaAction->GetMenuDescription().EqualTo(DisplayName))
							{
								bUnique = false;
								break;
							}
						}

						if (!bUnique)
						{
							break;
						}
					}

					if (bUnique)
					{
						const FText Tooltip = FText::GetEmpty();
						TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
							Category, DisplayName, Tooltip, 0, FText::GetEmpty(),
							FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterMenu::AddParameterSelected, Parameter, false, ParameterSectionID)));

						OutAllActions.AddAction(Action);
					}
				}
			}
		}
	}
}

void SNiagaraAddParameterMenu::AddParameterGroup(
	FGraphActionListBuilderBase& OutActions,
	TArray<FNiagaraVariable>& Variables,
	const NiagaraParameterMapSectionID::Type InSection,
	const FText& Category,
	const FString& RootCategory,
	const bool bSort,
	const bool bCustomName,
	bool bForMakeNew)
{
	if (bSort)
	{
		Variables.Sort([](const FNiagaraVariable& A, const FNiagaraVariable& B) { return A.GetName().LexicalLess(B.GetName()); });
	}

	for (const FNiagaraVariable& Variable : Variables)
	{
		const FText DisplayName = FText::FromName(Variable.GetName());
		FText Tooltip = FText::GetEmpty();

		if (const UStruct* VariableStruct = Variable.GetType().GetStruct())
		{
			Tooltip = VariableStruct->GetToolTipText(true);
		}

		TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(Category, DisplayName, Tooltip, 0, FText(),
			FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterMenu::AddParameterSelected, Variable, bCustomName, InSection)));
		if (bForMakeNew == false)
		{
			Action->SetParamterHandle(FNiagaraParameterHandle(Variable.GetName()));
		}

		if (Variable.IsDataInterface())
		{
			if (const UClass* DataInterfaceClass = Variable.GetType().GetClass())
			{
				Action->IsExperimental = DataInterfaceClass->GetMetaData("DevelopmentStatus") == TEXT("Experimental");
			}
		}

		OutActions.AddAction(Action, RootCategory);
	}
}

void SNiagaraAddParameterMenu::CollectParameterCollectionsActions(FGraphActionListBuilderBase& OutActions)
{
	//Create sub menus for parameter collections.
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> CollectionAssets;
	AssetRegistryModule.Get().GetAssetsByClass(UNiagaraParameterCollection::StaticClass()->GetFName(), CollectionAssets);

	const FText Category = NiagaraParameterMapSectionID::OnGetSectionTitle(NiagaraParameterMapSectionID::PARAMETERCOLLECTION);
	for (FAssetData& CollectionAsset : CollectionAssets)
	{
		UNiagaraParameterCollection* Collection = CastChecked<UNiagaraParameterCollection>(CollectionAsset.GetAsset());
		if (Collection)
		{
			AddParameterGroup(OutActions, Collection->GetParameters(), NiagaraParameterMapSectionID::PARAMETERCOLLECTION, Category, FString(), true, false);
		}
	}
}

void SNiagaraAddParameterMenu::CollectMakeNew(FGraphActionListBuilderBase& OutActions, const NiagaraParameterMapSectionID::Type InSection)
{
	if (!AllowCreatingNew.Get())
	{
		return;
	}

	TArray<FNiagaraVariable> Variables;
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
			FNiagaraVariable Var(RegisteredType, FName(*RegisteredType.GetNameText().ToString()));
			FNiagaraEditorUtilities::ResetVariableToDefaultValue(Var);
			Variables.Add(Var);
		}
	}

	AddParameterGroup(OutActions, Variables, InSection,
		LOCTEXT("MakeNewCat", "Make New"), 
		ShowNamespaceCategory.Get() ? NiagaraParameterMapSectionID::OnGetSectionTitle(InSection).ToString() : FString(), 
		true, true, true);
}

void SNiagaraAddParameterMenu::AddParameterSelected(FNiagaraVariable NewVariable, const bool bCreateCustomName, const NiagaraParameterMapSectionID::Type InSection)
{
	if (bCreateCustomName)
	{
		const static FString NewVariableDefaultName = FString("NewVariable");
		TArray<FName> SectionNamespaces;
		NiagaraParameterMapSectionID::OnGetSectionNamespaces(InSection, SectionNamespaces);
		TArray<FString> NameParts;
		for (FName SectionNamespace : SectionNamespaces)
		{
			NameParts.Add(SectionNamespace.ToString());
		}
		NameParts.Add(NewVariableDefaultName);
		const FString ResultName = FString::Join(NameParts, TEXT("."));
		NewVariable.SetName(FName(*ResultName));
	}

	OnAddParameter.ExecuteIfBound(NewVariable);
}

class SNiagaraActionMenuExpander : public SExpanderArrow
{
	SLATE_BEGIN_ARGS(SNiagaraActionMenuExpander) {}
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
			.Padding(TAttribute<FMargin>(this, &SNiagaraActionMenuExpander::GetCustomIndentPadding))
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


TSharedRef<SExpanderArrow> SNiagaraParameterMapView::CreateCustomActionExpander(const FCustomExpanderData& ActionMenuData)
{
	return SNew(SNiagaraActionMenuExpander, ActionMenuData);
}

#undef LOCTEXT_NAMESPACE // "NiagaraParameterMapView"