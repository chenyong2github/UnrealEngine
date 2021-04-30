// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraParameterMenu.h"

#include "AssetRegistryModule.h"
#include "EdGraph/EdGraphSchema.h"
#include "NiagaraActions.h"
#include "NiagaraConstants.h"
#include "NiagaraEditorSettings.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeWithDynamicPins.h"
#include "NiagaraParameterDefinitions.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraScriptSource.h"
#include "SGraphActionMenu.h"
#include "SNiagaraGraphActionWidget.h"
#include "Widgets/SNiagaraActionMenuExpander.h"

#define LOCTEXT_NAMESPACE "SNiagaraParameterMenu"

///////////////////////////////////////////////////////////////////////////////
/// Base Parameter Menu														///
///////////////////////////////////////////////////////////////////////////////

void SNiagaraParameterMenu::Construct(const FArguments& InArgs)
{
	this->bAutoExpandMenu = InArgs._AutoExpandMenu;

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
			.OnActionSelected(this, &SNiagaraParameterMenu::OnActionSelected)
			.OnCollectAllActions(this, &SNiagaraParameterMenu::CollectAllActions)
			.AutoExpandActionMenu(bAutoExpandMenu)
			.ShowFilterTextBox(true)
			.OnGetSectionTitle(InArgs._OnGetSectionTitle)
			.OnCreateCustomRowExpander_Static(&SNiagaraParameterMenu::CreateCustomActionExpander)
			.OnCreateWidgetForAction_Lambda([](const FCreateWidgetForActionData* InData)
				{
					return SNew(SNiagaraGraphActionWidget, InData);
				})
			]
		]
	];
}

TSharedPtr<SEditableTextBox> SNiagaraParameterMenu::GetSearchBox()
{
	return GraphMenu->GetFilterTextBox();
}

void SNiagaraParameterMenu::OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions, ESelectInfo::Type InSelectionType)
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

TSharedRef<SExpanderArrow> SNiagaraParameterMenu::CreateCustomActionExpander(const struct FCustomExpanderData& ActionMenuData)
{
	return SNew(SNiagaraActionMenuExpander, ActionMenuData);
}

bool SNiagaraParameterMenu::IsStaticSwitchParameter(const FNiagaraVariable& Variable, const TArray<UNiagaraGraph*>& Graphs)
{
	for (const UNiagaraGraph* Graph : Graphs)
	{
		TArray<FNiagaraVariable> SwitchInputs = Graph->FindStaticSwitchInputs();
		if (SwitchInputs.Contains(Variable))
		{
			return true;
		}
	}
	return false;
}

FText SNiagaraParameterMenu::GetNamespaceCategoryText(const FNiagaraNamespaceMetadata& NamespaceMetaData)
{
	return NamespaceMetaData.DisplayNameLong.IsEmptyOrWhitespace() == false ? NamespaceMetaData.DisplayNameLong : NamespaceMetaData.DisplayName;
}

FText SNiagaraParameterMenu::GetNamespaceCategoryText(const FGuid& NamespaceId)
{
	const FNiagaraNamespaceMetadata NamespaceMetaData = FNiagaraEditorUtilities::GetNamespaceMetaDataForId(NamespaceId);
	return GetNamespaceCategoryText(NamespaceMetaData);
}


///////////////////////////////////////////////////////////////////////////////
/// Add Parameter Menu														///
///////////////////////////////////////////////////////////////////////////////

void SNiagaraAddParameterFromPanelMenu::Construct(const FArguments& InArgs)
{
	this->OnAddParameter = InArgs._OnAddParameter;
	this->OnAddScriptVar = InArgs._OnAddScriptVar;
	this->OnAllowMakeType = InArgs._OnAllowMakeType;
	this->OnAddParameterDefinitions = InArgs._OnAddParameterDefinitions;

	this->Graphs = InArgs._Graphs;
	this->AvailableParameterDefinitions = InArgs._AvailableParameterDefinitions;
	this->SubscribedParameterDefinitions = InArgs._SubscribedParameterDefinitions;
	this->NamespaceId = InArgs._NamespaceId;
	this->bAllowCreatingNew = InArgs._AllowCreatingNew;
	this->bShowNamespaceCategory = InArgs._ShowNamespaceCategory;
	this->bShowGraphParameters = InArgs._ShowGraphParameters;
	this->bIsParameterReadNode = InArgs._IsParameterRead;
	this->bForceCollectEngineNamespaceParameterActions = InArgs._ForceCollectEngineNamespaceParameterActions;
	this->bOnlyShowParametersInNamespaceId = NamespaceId.IsValid();

	SNiagaraParameterMenu::FArguments SuperArgs;
	SuperArgs._AutoExpandMenu = InArgs._AutoExpandMenu;
	SuperArgs._OnGetSectionTitle = FGetSectionTitle::CreateStatic(&SNiagaraAddParameterFromPanelMenu::GetSectionTitle);
	SNiagaraParameterMenu::Construct(SuperArgs);
}

void SNiagaraAddParameterFromPanelMenu::CollectMakeNew(FGraphActionListBuilderBase& OutActions, const FGuid& InNamespaceId)
{
	if (bAllowCreatingNew == false)
	{
		return;
	}

	TArray<FNiagaraVariable> Variables;
	TConstArrayView<FNiagaraTypeDefinition> SectionTypes;
	if(InNamespaceId == FNiagaraEditorGuids::UserNamespaceMetaDataGuid) 
	{
		SectionTypes = MakeArrayView(FNiagaraTypeRegistry::GetRegisteredUserVariableTypes());
	}
	else if(InNamespaceId == FNiagaraEditorGuids::SystemNamespaceMetaDataGuid)
	{
		SectionTypes = MakeArrayView(FNiagaraTypeRegistry::GetRegisteredSystemVariableTypes());
	}
	else if (InNamespaceId == FNiagaraEditorGuids::EmitterNamespaceMetaDataGuid)
	{
		SectionTypes = MakeArrayView(FNiagaraTypeRegistry::GetRegisteredEmitterVariableTypes());
	}
	else if (InNamespaceId == FNiagaraEditorGuids::ParticleAttributeNamespaceMetaDataGuid)
	{
		SectionTypes = MakeArrayView(FNiagaraTypeRegistry::GetRegisteredParticleVariableTypes());
	}
	else
	{
		SectionTypes = MakeArrayView(FNiagaraTypeRegistry::GetRegisteredTypes());
	}

	for (const FNiagaraTypeDefinition& RegisteredType : SectionTypes)
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

	AddParameterGroup(OutActions, Variables, InNamespaceId,
		LOCTEXT("MakeNewCat", "Make New"),
		bShowNamespaceCategory ? GetNamespaceCategoryText(InNamespaceId).ToString() : FString(),
		true, true, true);
}

void SNiagaraAddParameterFromPanelMenu::AddParameterGroup(
	FGraphActionListBuilderBase& OutActions,
	TArray<FNiagaraVariable>& Variables,
	const FGuid& InNamespaceId /*= FGuid()*/,
	const FText& Category /*= FText::GetEmpty()*/,
	const FString& RootCategory /*= FString()*/,
	const bool bSort /*= true*/,
	const bool bCustomName /*= true*/,
	bool bForMakeNew /*= false*/)
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

		FText SubCategory = FNiagaraEditorUtilities::GetVariableTypeCategory(Variable);
		FText FullCategory = SubCategory.IsEmpty() ? Category : FText::Format(FText::FromString("{0}|{1}"), Category, SubCategory);
		TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(FullCategory, DisplayName, Tooltip, 0, FText(),
			FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterFromPanelMenu::ParameterSelected, Variable, bCustomName, InNamespaceId)));
		Action->SetParameterVariable(Variable);

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

void SNiagaraAddParameterFromPanelMenu::CollectParameterCollectionsActions(FGraphActionListBuilderBase& OutActions)
{
	//Create sub menus for parameter collections.
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> CollectionAssets;
	AssetRegistryModule.Get().GetAssetsByClass(UNiagaraParameterCollection::StaticClass()->GetFName(), CollectionAssets);

	const FText Category = GetNamespaceCategoryText(FNiagaraEditorGuids::ParameterCollectionNamespaceMetaDataGuid);
	for (FAssetData& CollectionAsset : CollectionAssets)
	{
		UNiagaraParameterCollection* Collection = CastChecked<UNiagaraParameterCollection>(CollectionAsset.GetAsset());
		if (Collection)
		{
			AddParameterGroup(OutActions, Collection->GetParameters(), FNiagaraEditorGuids::ParameterCollectionNamespaceMetaDataGuid, Category, FString(), true, false);
		}
	}
}

void SNiagaraAddParameterFromPanelMenu::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	auto CollectEngineNamespaceParameterActions = [this, &OutAllActions]() {
		const FNiagaraNamespaceMetadata NamespaceMetaData = FNiagaraEditorUtilities::GetNamespaceMetaDataForId(FNiagaraEditorGuids::EngineNamespaceMetaDataGuid);
		TArray<FNiagaraVariable> Variables = FNiagaraConstants::GetEngineConstants();
		const TArray<FName>& EngineNamespaces = NamespaceMetaData.Namespaces;
		Variables.RemoveAll([EngineNamespaces](const FNiagaraVariable& Variable)
			{
				FNiagaraParameterHandle VariableHandle(Variable.GetName());
				TArray<FName> VariableNameParts = VariableHandle.GetHandleParts();
				if (VariableNameParts.Num() <= EngineNamespaces.Num())
				{
					return true;
				}

				for (int32 NamespaceIndex = 0; NamespaceIndex < EngineNamespaces.Num(); NamespaceIndex++)
				{
					if (VariableNameParts[NamespaceIndex] != EngineNamespaces[NamespaceIndex])
					{
						return true;
					}
				}

				return false;
			});

		const FText CategoryText = bShowNamespaceCategory ? GetNamespaceCategoryText(NamespaceMetaData) : LOCTEXT("EngineConstantNamespaceCategory", "Add Engine Constant");
		const FString RootCategoryStr = FString();
		const bool bMakeNameUnique = false;
		const bool bCreateNewParameter = false;
		AddParameterGroup(
			OutAllActions,
			Variables,
			FNiagaraEditorGuids::EngineNamespaceMetaDataGuid,
			CategoryText,
			RootCategoryStr,
			bMakeNameUnique,
			bCreateNewParameter
		);
	};

	TSet<FGuid> ExistingGraphParameterIds;
	TSet<FName> VisitedParameterNames;
	TArray<FGuid> ExcludedNamespaceIds;
	// If this is a write node, exclude any read-only vars.
	if (!bIsParameterReadNode)
	{
		ExcludedNamespaceIds.Add(FNiagaraEditorGuids::UserNamespaceMetaDataGuid);
		ExcludedNamespaceIds.Add(FNiagaraEditorGuids::EngineNamespaceMetaDataGuid);
		ExcludedNamespaceIds.Add(FNiagaraEditorGuids::ParameterCollectionNamespaceMetaDataGuid);
	}

	// If this doesn't have particles in the script, exclude reading or writing them.
	// Also, collect the ids of all variables the graph owns to exclude them from parameters to add from libraries.
	for (const UNiagaraGraph* Graph : Graphs)
	{
		bool IsModule = Graph->FindOutputNode(ENiagaraScriptUsage::Module) != nullptr || Graph->FindOutputNode(ENiagaraScriptUsage::DynamicInput) != nullptr
			|| Graph->FindOutputNode(ENiagaraScriptUsage::Function) != nullptr;

		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Graph->GetOuter());
		if (Source && IsModule)
		{
			UNiagaraScript* Script = Cast<UNiagaraScript>(Source->GetOuter());
			if (Script)
			{
				TArray<ENiagaraScriptUsage> Usages = Script->GetLatestScriptData()->GetSupportedUsageContexts();
				if (!Usages.Contains(ENiagaraScriptUsage::ParticleEventScript) &&
					!Usages.Contains(ENiagaraScriptUsage::ParticleSpawnScript) &&
					!Usages.Contains(ENiagaraScriptUsage::ParticleUpdateScript))
				{
					ExcludedNamespaceIds.Add(FNiagaraEditorGuids::ParticleAttributeNamespaceMetaDataGuid);
				}

				if (bIsParameterReadNode)
				{
					if (!Usages.Contains(ENiagaraScriptUsage::SystemSpawnScript) &&
						!Usages.Contains(ENiagaraScriptUsage::SystemUpdateScript))
					{
						ExcludedNamespaceIds.Add(FNiagaraEditorGuids::SystemNamespaceMetaDataGuid);
					}

					if (!Usages.Contains(ENiagaraScriptUsage::EmitterSpawnScript) &&
						!Usages.Contains(ENiagaraScriptUsage::EmitterUpdateScript))
					{
						ExcludedNamespaceIds.Add(FNiagaraEditorGuids::EmitterNamespaceMetaDataGuid);
					}
				}
			}
		}

		const TMap<FNiagaraVariable, UNiagaraScriptVariable*>& VariableToScriptVariableMap = Graph->GetAllMetaData();
		for (auto It = VariableToScriptVariableMap.CreateConstIterator(); It; ++It)
		{
			ExistingGraphParameterIds.Add(It.Value()->Metadata.GetVariableGuid());
		}
	}


	// Parameter collections
	if (NamespaceId == FNiagaraEditorGuids::ParameterCollectionNamespaceMetaDataGuid)
	{
		CollectParameterCollectionsActions(OutAllActions);
	}
	// Engine intrinsic parameters
	else if (NamespaceId == FNiagaraEditorGuids::EngineNamespaceMetaDataGuid)
	{
		CollectEngineNamespaceParameterActions();
	}
	// DataInstance intrinsic parameters
	else if (NamespaceId == FNiagaraEditorGuids::DataInstanceNamespaceMetaDataGuid && (ExcludedNamespaceIds.Contains(FNiagaraEditorGuids::ParticleAttributeNamespaceMetaDataGuid) == false))
	{
		TArray<FNiagaraVariable> Variables;
		Variables.Add(SYS_PARAM_INSTANCE_ALIVE);
		AddParameterGroup(OutAllActions, Variables, FNiagaraEditorGuids::DataInstanceNamespaceMetaDataGuid, FText(), FString(), true, false);
	}
	// No NamespaceId set but still collecting engine namespace parameters (e.g. map get/set node menu.)
	else if (NamespaceId.IsValid() == false && bForceCollectEngineNamespaceParameterActions)
	{
		CollectEngineNamespaceParameterActions();
		CollectMakeNew(OutAllActions, NamespaceId);
	}

	// Any other "unreserved" namespace
	else
	{
		CollectMakeNew(OutAllActions, NamespaceId);
	}

	// Collect "add existing graph parameter" actions
	if (bShowGraphParameters)
	{
		TSet<FGuid> VisitedParameterIds;
		for (const UNiagaraGraph* Graph : Graphs)
		{
			TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection> ParameterEntries = Graph->GetParameterReferenceMap();
			ParameterEntries.KeySort([](const FNiagaraVariable& A, const FNiagaraVariable& B) { return A.GetName().LexicalLess(B.GetName()); });

			// Iterate the parameter reference map as this represents all parameters in the graph, including parameters the graph itself does not own.
			for (const auto& ParameterEntry : ParameterEntries)
			{
				const FNiagaraVariable& Parameter = ParameterEntry.Key;

				// Check if the graph owns the parameter (has a script variable for the parameter.)
				if (const UNiagaraScriptVariable* ScriptVar = Graph->GetScriptVariable(Parameter))
				{
					// The graph owns the parameter. Skip if it is a static switch.
					if (ScriptVar->GetIsStaticSwitch())
					{
						continue;
					}
					// Check that we do not add a duplicate entry.
					const FGuid& ScriptVarId = ScriptVar->Metadata.GetVariableGuid();
					if (VisitedParameterIds.Contains(ScriptVarId) == false)
					{
						// The script variable is not a duplicate, add an entry for it.
						VisitedParameterIds.Add(ScriptVarId);
						FNiagaraVariable MutableParameter = FNiagaraVariable(Parameter);
						const FText Category = bShowNamespaceCategory ? GetNamespaceCategoryText(NamespaceId) : LOCTEXT("NiagaraAddExistingParameterMenu", "Add Existing Parameter");
						const FText DisplayName = FText::FromName(Parameter.GetName());
						const FText Tooltip = FText::GetEmpty();
						TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
							Category, DisplayName, Tooltip, 0, FText::GetEmpty(),
							FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterFromPanelMenu::ParameterSelected, MutableParameter)));
						Action->SetParameterVariable(Parameter);

						OutAllActions.AddAction(Action);
						continue;
					}
				}
			
				// The graph does not own the parameter, check if it is a reserved namespace parameter.
				const FNiagaraParameterHandle ParameterHandle = FNiagaraParameterHandle(Parameter.GetName());
				if (ParameterHandle.IsParameterCollectionHandle() || ParameterHandle.IsEngineHandle() || ParameterHandle.IsDataInstanceHandle())
				{
					// Check that we do not add a duplicate entry.
					const FName& ParameterName = Parameter.GetName();
					if (VisitedParameterNames.Contains(ParameterName) == false)
					{
						// The reserved namespace parameter is not a duplicate, add an entry for it.
						VisitedParameterNames.Add(ParameterName);
						FNiagaraVariable MutableParameter = FNiagaraVariable(Parameter);
						const FText Category = bShowNamespaceCategory ? GetNamespaceCategoryText(NamespaceId) : LOCTEXT("NiagaraAddExistingParameterMenu", "Add Existing Parameter");
						const FText DisplayName = FText::FromName(Parameter.GetName());
						const FText Tooltip = FText::GetEmpty();
						TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
							Category, DisplayName, Tooltip, 0, FText::GetEmpty(),
							FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterFromPanelMenu::ParameterSelected, MutableParameter)));
						Action->SetParameterVariable(Parameter);

						OutAllActions.AddAction(Action);
						continue;
					}
				}
				ensureMsgf(false, TEXT("Encountered existing variable of unknown type when collecting graph parameters!"));
			}
		}
	}

	// Collect "add parameter from parameter definition asset" actions
	const TSet<FName> GraphParameterNames = bShowGraphParameters ? VisitedParameterNames : GetAllGraphParameterNames();
	for (UNiagaraParameterDefinitions* ParameterDefinitions : AvailableParameterDefinitions)
	{
		bool bTopLevelCategory = ParameterDefinitions->GetIsPromotedToTopInAddMenus();
		const FText TopLevelCategory = FText::FromString(*ParameterDefinitions->GetName());
		const FText Category = bTopLevelCategory ? FText() : TopLevelCategory;
		for (const UNiagaraScriptVariable* ScriptVar : ParameterDefinitions->GetParametersConst())
		{
			// Only add parameters in the same namespace as the target namespace id if bOnlyShowParametersInNamespaceId is set.
			if (bOnlyShowParametersInNamespaceId && FNiagaraEditorUtilities::GetNamespaceMetaDataForVariableName(ScriptVar->Variable.GetName()).GetGuid() != NamespaceId)
			{
				continue;
			}

			// Check that we do not add a duplicate entry.
			const FGuid& ScriptVarId = ScriptVar->Metadata.GetVariableGuid();
			if (ExistingGraphParameterIds.Contains(ScriptVarId))
			{
				continue;
			}
			else if (GraphParameterNames.Contains(ScriptVar->Variable.GetName()))
			{
				continue;
			}

			const FText DisplayName = FText::FromName(ScriptVar->Variable.GetName());
			const FText& Tooltip = ScriptVar->Metadata.Description;
			TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
				Category, DisplayName, Tooltip, 0, FText::GetEmpty(),
				FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraAddParameterFromPanelMenu::ScriptVarFromParameterDefinitionsSelected, ScriptVar, ParameterDefinitions)));
			Action->SetParameterVariable(ScriptVar->Variable);
			
			if (bTopLevelCategory)
			{
				OutAllActions.AddAction(Action, TopLevelCategory.ToString());
			}
			else
			{ 
				Action->SetSectionId(1); //Increment the default section id so parameter definitions actions are always categorized BELOW other actions.
				OutAllActions.AddAction(Action);
			}
		}
	}
}

void SNiagaraAddParameterFromPanelMenu::ParameterSelected(FNiagaraVariable NewVariable, const bool bCreateUniqueName /*= false*/, const FGuid InNamespaceId /*= FGuid()*/)
{
	auto GetNamespaceMetaData = [this, &InNamespaceId]()->const FNiagaraNamespaceMetadata {
		if (InNamespaceId.IsValid() == false)
		{
			if (bIsParameterReadNode) // Map Get
			{
				return FNiagaraEditorUtilities::GetNamespaceMetaDataForId(FNiagaraEditorGuids::ModuleNamespaceMetaDataGuid);
			}
			else // Map Set
			{
				return FNiagaraEditorUtilities::GetNamespaceMetaDataForId(FNiagaraEditorGuids::ModuleLocalNamespaceMetaDataGuid);
			}
		}
		return FNiagaraEditorUtilities::GetNamespaceMetaDataForId(InNamespaceId);
	};

	if (bCreateUniqueName)
	{
		FString TypeDisplayName;
		if (NewVariable.GetType().GetEnum() != nullptr)
		{
			TypeDisplayName = ((UField*)NewVariable.GetType().GetEnum())->GetDisplayNameText().ToString();
		}
		else if (NewVariable.GetType().GetStruct() != nullptr)
		{
			TypeDisplayName = NewVariable.GetType().GetStruct()->GetDisplayNameText().ToString();
		}
		else if (NewVariable.GetType().GetClass() != nullptr)
		{
			TypeDisplayName = NewVariable.GetType().GetClass()->GetDisplayNameText().ToString();
		}
		const FString NewVariableDefaultName = TypeDisplayName.IsEmpty()
			? TEXT("New Variable")
			: TEXT("New ") + TypeDisplayName;

		TArray<FString> NameParts;

		const FNiagaraNamespaceMetadata NamespaceMetaData = GetNamespaceMetaData();
		checkf(NamespaceMetaData.IsValid(), TEXT("Failed to get valid namespace metadata when creating unique name for parameter menu add parameter action!"));
		for (const FName Namespace : NamespaceMetaData.Namespaces)
		{
			NameParts.Add(Namespace.ToString());
		}

		if (NamespaceMetaData.RequiredNamespaceModifier != NAME_None)
		{
			NameParts.Add(NamespaceMetaData.RequiredNamespaceModifier.ToString());
		}

		NameParts.Add(NewVariableDefaultName);
		const FString ResultName = FString::Join(NameParts, TEXT("."));
		NewVariable.SetName(FName(*ResultName));
	}

	OnAddParameter.ExecuteIfBound(NewVariable);
}

void SNiagaraAddParameterFromPanelMenu::ParameterSelected(FNiagaraVariable NewVariable)
{
	ParameterSelected(NewVariable, false, FGuid());
}

void SNiagaraAddParameterFromPanelMenu::ScriptVarFromParameterDefinitionsSelected(const UNiagaraScriptVariable* NewScriptVar, UNiagaraParameterDefinitions* SourceParameterDefinitions)
{
	// If the parameter definitions the script var belongs to is not subscribed to, add it.
	const FGuid& SourceParameterDefinitionsId = SourceParameterDefinitions->GetDefinitionsUniqueId();
	if (SubscribedParameterDefinitions.ContainsByPredicate([SourceParameterDefinitionsId](const UNiagaraParameterDefinitions* ParameterDefinitions){ return ParameterDefinitions->GetDefinitionsUniqueId() == SourceParameterDefinitionsId; }) == false)
	{
		OnAddParameterDefinitions.ExecuteIfBound(SourceParameterDefinitions);
	}

	// Add the script var.
	OnAddScriptVar.ExecuteIfBound(NewScriptVar);
}

TSet<FName> SNiagaraAddParameterFromPanelMenu::GetAllGraphParameterNames() const
{
	TSet<FName> VisitedParameterNames;
	for (const UNiagaraGraph* Graph : Graphs)
	{
		const TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection> ParameterEntries = Graph->GetParameterReferenceMap();

		// Iterate the parameter reference map as this represents all parameters in the graph, including parameters the graph itself does not own.
		for (const auto& ParameterEntry : ParameterEntries)
		{
			VisitedParameterNames.Add(ParameterEntry.Key.GetName());
		}
	}
	return VisitedParameterNames;
}

FText SNiagaraAddParameterFromPanelMenu::GetSectionTitle(int32 SectionId)
{
	if (SectionId != 1)
	{
		ensureMsgf(false, TEXT("Encountered SectionId that was not \"1\"! Update formatting rules!"));
		return FText();
	}
	return LOCTEXT("ParameterDefinitionsSection", "Parameter Definitions");
}

///////////////////////////////////////////////////////////////////////////////
/// Add Parameter From Pin Menu												///
///////////////////////////////////////////////////////////////////////////////

void SNiagaraAddParameterFromPinMenu::Construct(const FArguments& InArgs)
{
	this->NiagaraNode = InArgs._NiagaraNode;
	this->AddPin = InArgs._AddPin;
	this->bIsParameterReadNode = AddPin->Direction == EEdGraphPinDirection::EGPD_Input ? false : true;

	SNiagaraParameterMenu::FArguments SuperArgs;
	SuperArgs._AutoExpandMenu = InArgs._AutoExpandMenu;
	SNiagaraParameterMenu::Construct(SuperArgs);
}


void SNiagaraAddParameterFromPinMenu::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	TArray<FNiagaraTypeDefinition> Types(FNiagaraTypeRegistry::GetRegisteredTypes());
	Types.Sort([](const FNiagaraTypeDefinition& A, const FNiagaraTypeDefinition& B) { return (A.GetNameText().ToLower().ToString() < B.GetNameText().ToLower().ToString()); });

	for (const FNiagaraTypeDefinition& RegisteredType : Types)
	{
		bool bAllowType = false;
		bAllowType = NiagaraNode->AllowNiagaraTypeForAddPin(RegisteredType);

		if (bAllowType)
		{
			FNiagaraVariable Var(RegisteredType, FName(*RegisteredType.GetName()));
			FNiagaraEditorUtilities::ResetVariableToDefaultValue(Var);

			FText Category = FNiagaraEditorUtilities::GetVariableTypeCategory(Var);
			const FText DisplayName = RegisteredType.GetNameText();
			const FText Tooltip = FText::Format(LOCTEXT("AddButtonTypeEntryToolTipFormat", "Add a new {0} pin"), RegisteredType.GetNameText());
			const UEdGraphPin* ConstAddPin = AddPin;
			TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
				Category, DisplayName, Tooltip, 0, FText::GetEmpty(),
				FNiagaraMenuAction::FOnExecuteStackAction::CreateUObject(NiagaraNode, &UNiagaraNodeWithDynamicPins::AddParameter, Var, ConstAddPin)));

			OutAllActions.AddAction(Action);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
/// Change Pin Type Menu													///
///////////////////////////////////////////////////////////////////////////////

void SNiagaraChangePinTypeMenu::Construct(const FArguments& InArgs)
{
	checkf(InArgs._PinToModify != nullptr, TEXT("Tried to construct change pin type menu without valid pin ptr!"));
	this->PinToModify = InArgs._PinToModify;

	SNiagaraParameterMenu::FArguments SuperArgs;
	SuperArgs._AutoExpandMenu = InArgs._AutoExpandMenu;
	SNiagaraParameterMenu::Construct(SuperArgs);
}

void SNiagaraChangePinTypeMenu::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	UNiagaraNode* Node = Cast<UNiagaraNode>(PinToModify->GetOwningNode());
	checkf(Node, TEXT("Niagara node pin did not have a valid outer node!"));

	TArray<FNiagaraTypeDefinition> Types(FNiagaraTypeRegistry::GetRegisteredTypes());
	Types.Sort([](const FNiagaraTypeDefinition& A, const FNiagaraTypeDefinition& B) { return (A.GetNameText().ToLower().ToString() < B.GetNameText().ToLower().ToString()); });

	for (const FNiagaraTypeDefinition& RegisteredType : Types)
	{
		const bool bAllowType = Node->AllowNiagaraTypeForPinTypeChange(RegisteredType, PinToModify);

		if (bAllowType)
		{
			FNiagaraVariable Var(RegisteredType, FName(*RegisteredType.GetName()));
			FNiagaraEditorUtilities::ResetVariableToDefaultValue(Var);

			FText Category = FNiagaraEditorUtilities::GetVariableTypeCategory(Var);
			const FText DisplayName = RegisteredType.GetNameText();
			const FText Tooltip = FText::Format(LOCTEXT("ChangeSelectorTypeEntryToolTipFormat", "Change to {0} pin"), RegisteredType.GetNameText());
			TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
				Category, DisplayName, Tooltip, 0, FText::GetEmpty(),
				FNiagaraMenuAction::FOnExecuteStackAction::CreateUObject(Node, &UNiagaraNode::RequestNewPinType, PinToModify, RegisteredType)));

			OutAllActions.AddAction(Action);
		}
	}
}

#undef LOCTEXT_NAMESPACE /*"SNiagaraParameterMenu"*/
