// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraTypeCustomizations.h"
#include "CoreMinimal.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "NiagaraConstants.h"
#include "NiagaraConstants.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEmitter.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeParameterMapBase.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraPlatformSet.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "PlatformInfo.h"
#include "PropertyHandle.h"
#include "SGraphActionMenu.h"
#include "Scalability.h"
#include "ScopedTransaction.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "NiagaraSimulationStageBase.h"
#include "Widgets/Text/STextBlock.h"
#include "NiagaraDataInterfaceRW.h"

#define LOCTEXT_NAMESPACE "FNiagaraVariableAttributeBindingCustomization"


void FNiagaraNumericCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> ValueHandle = PropertyHandle->GetChildHandle(TEXT("Value"));

	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MaxDesiredWidth(ValueHandle.IsValid() ? 125.f : 200.f)
		[
			// Some Niagara numeric types have no value so in that case just display their type name
			ValueHandle.IsValid()
			? ValueHandle->CreatePropertyValueWidget()
			: SNew(STextBlock)
			  .Text(FText::FromString(FName::NameToDisplayString(CastField<FStructProperty>(PropertyHandle->GetProperty())->Struct->GetName(), false)))
			  .Font(IDetailLayoutBuilder::GetDetailFont())
		];
}


void FNiagaraBoolCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	ValueHandle = PropertyHandle->GetChildHandle(TEXT("Value"));

	static const FName DefaultForegroundName("DefaultForeground");

	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.OnCheckStateChanged(this, &FNiagaraBoolCustomization::OnCheckStateChanged)
			.IsChecked(this, &FNiagaraBoolCustomization::OnGetCheckState)
			.ForegroundColor(FEditorStyle::GetSlateColor(DefaultForegroundName))
			.Padding(0.0f)
		];
}

ECheckBoxState FNiagaraBoolCustomization::OnGetCheckState() const
{
	ECheckBoxState CheckState = ECheckBoxState::Undetermined;
	int32 Value;
	FPropertyAccess::Result Result = ValueHandle->GetValue(Value);
	if (Result == FPropertyAccess::Success)
	{
		CheckState = Value == FNiagaraBool::True ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return CheckState;
}

void FNiagaraBoolCustomization::OnCheckStateChanged(ECheckBoxState InNewState)
{
	if (InNewState == ECheckBoxState::Checked)
	{
		ValueHandle->SetValue(FNiagaraBool::True);
	}
	else
	{
		ValueHandle->SetValue(FNiagaraBool::False);
	}
}

void FNiagaraMatrixCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	for (uint32 ChildNum = 0; ChildNum < NumChildren; ++ChildNum)
	{
		ChildBuilder.AddProperty(PropertyHandle->GetChildHandle(ChildNum).ToSharedRef());
	}
}

FText FNiagaraVariableAttributeBindingCustomization::GetCurrentText() const
{
	if (BaseEmitter && TargetVariableBinding)
	{
		return FText::FromName(TargetVariableBinding->BoundVariable.GetName());
	}
	return FText::FromString(TEXT("Missing"));
}

FText FNiagaraVariableAttributeBindingCustomization::GetTooltipText() const
{
	if (BaseEmitter && TargetVariableBinding)
	{
		FString DefaultValueStr = TargetVariableBinding->DefaultValueIfNonExistent.GetName().ToString();
		
		if (!TargetVariableBinding->DefaultValueIfNonExistent.GetName().IsValid() || TargetVariableBinding->DefaultValueIfNonExistent.IsDataAllocated() == true)
		{
			DefaultValueStr = TargetVariableBinding->DefaultValueIfNonExistent.GetType().ToString(TargetVariableBinding->DefaultValueIfNonExistent.GetData());
			DefaultValueStr.TrimEndInline();
		}

		FText TooltipDesc = FText::Format(LOCTEXT("AttributeBindingTooltip", "Use the variable \"{0}\" if it exists, otherwise use the default \"{1}\" "), FText::FromName(TargetVariableBinding->BoundVariable.GetName()),
			FText::FromString(DefaultValueStr));
		return TooltipDesc;
	}
	return FText::FromString(TEXT("Missing"));
}

TSharedRef<SWidget> FNiagaraVariableAttributeBindingCustomization::OnGetMenuContent() const
{
	FGraphActionMenuBuilder MenuBuilder;

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			[
				SNew(SGraphActionMenu)
				.OnActionSelected(const_cast<FNiagaraVariableAttributeBindingCustomization*>(this), &FNiagaraVariableAttributeBindingCustomization::OnActionSelected)
				.OnCreateWidgetForAction(SGraphActionMenu::FOnCreateWidgetForAction::CreateSP(const_cast<FNiagaraVariableAttributeBindingCustomization*>(this), &FNiagaraVariableAttributeBindingCustomization::OnCreateWidgetForAction))
				.OnCollectAllActions(const_cast<FNiagaraVariableAttributeBindingCustomization*>(this), &FNiagaraVariableAttributeBindingCustomization::CollectAllActions)
				.AutoExpandActionMenu(false)
				.ShowFilterTextBox(true)
			]
		];
}

TArray<FName> FNiagaraVariableAttributeBindingCustomization::GetNames(UNiagaraEmitter* InEmitter) const
{
	TArray<FName> Names;

	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(InEmitter->GraphSource);
	if (Source)
	{
		TArray<FNiagaraParameterMapHistory> Histories =  UNiagaraNodeParameterMapBase::GetParameterMaps(Source->NodeGraph);
		for (const FNiagaraParameterMapHistory& History : Histories)
		{
			for (const FNiagaraVariable& Var : History.Variables)
			{
				if (FNiagaraParameterMapHistory::IsAttribute(Var) && Var.GetType() == TargetVariableBinding->BoundVariable.GetType())
				{
					Names.AddUnique(Var.GetName());
				}
			}
		}
		
	}
	return Names;
}

void FNiagaraVariableAttributeBindingCustomization::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	TArray<FName> EventNames = GetNames(BaseEmitter);
	FName EmitterName = BaseEmitter->GetFName();
	for (FName EventName : EventNames)
	{
		FText CategoryName = FText();
		FString DisplayNameString = FName::NameToDisplayString(EventName.ToString(), false);
		const FText NameText = FText::FromString(DisplayNameString);
		const FText TooltipDesc = FText::Format(LOCTEXT("SetFunctionPopupTooltip", "Use the variable \"{0}\" "), FText::FromString(DisplayNameString));
		TSharedPtr<FNiagaraStackAssetAction_VarBind> NewNodeAction(new FNiagaraStackAssetAction_VarBind(EventName, CategoryName, NameText,
			TooltipDesc, 0, FText()));
		OutAllActions.AddAction(NewNodeAction);
	}
}

TSharedRef<SWidget> FNiagaraVariableAttributeBindingCustomization::OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(InCreateData->Action->GetMenuDescription())
			.ToolTipText(InCreateData->Action->GetTooltipDescription())
		];
}


void FNiagaraVariableAttributeBindingCustomization::OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (int32 ActionIndex = 0; ActionIndex < SelectedActions.Num(); ActionIndex++)
		{
			TSharedPtr<FEdGraphSchemaAction> CurrentAction = SelectedActions[ActionIndex];

			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				FNiagaraStackAssetAction_VarBind* EventSourceAction = (FNiagaraStackAssetAction_VarBind*)CurrentAction.Get();
				ChangeSource(EventSourceAction->VarName);
			}
		}
	}
}

void FNiagaraVariableAttributeBindingCustomization::ChangeSource(FName InVarName)
{
	FScopedTransaction Transaction(FText::Format(LOCTEXT("ChangeVariableSource", " Change Variable Source to \"{0}\" "), FText::FromName(InVarName)));
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	for (UObject* Obj : Objects)
	{
		Obj->Modify();
	}

	PropertyHandle->NotifyPreChange();
	TargetVariableBinding->BoundVariable.SetName(InVarName);
	TargetVariableBinding->DataSetVariable = FNiagaraConstants::GetAttributeAsDataSetKey(TargetVariableBinding->BoundVariable);
	PropertyHandle->NotifyPostChange();
	PropertyHandle->NotifyFinishedChangingProperties();
}

void FNiagaraVariableAttributeBindingCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	bool bAddDefault = true;
	if (Objects.Num() == 1)
	{
		UNiagaraRendererProperties* RenderProps = Cast<UNiagaraRendererProperties>(Objects[0]);
		if (RenderProps)
		{
			BaseEmitter = Cast<UNiagaraEmitter>(RenderProps->GetOuter());

			if (BaseEmitter)
			{
				TargetVariableBinding = (FNiagaraVariableAttributeBinding*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);
				
				HeaderRow
					.NameContent()
					[
						PropertyHandle->CreatePropertyNameWidget()
					]
					.ValueContent()
					.MaxDesiredWidth(200.f)
					[
						SNew(SComboButton)
						.OnGetMenuContent(this, &FNiagaraVariableAttributeBindingCustomization::OnGetMenuContent)
						.ContentPadding(1)
						.ToolTipText(this, &FNiagaraVariableAttributeBindingCustomization::GetTooltipText)
						.ButtonContent()
						[
							SNew(STextBlock)
							.Text(this, &FNiagaraVariableAttributeBindingCustomization::GetCurrentText)
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
					];
				bAddDefault = false;
			}
		}
	}
	

	if (bAddDefault)
	{
		HeaderRow
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
		.ValueContent()
			.MaxDesiredWidth(200.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FName::NameToDisplayString(CastField<FStructProperty>(PropertyHandle->GetProperty())->Struct->GetName(), false)))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	}
}


//////////////////////////////////////////////////////////////////////////

FText FNiagaraUserParameterBindingCustomization::GetCurrentText() const
{
	if (BaseSystem && TargetUserParameterBinding)
	{
		return FText::FromName(TargetUserParameterBinding->Parameter.GetName());
	}
	return FText::FromString(TEXT("Missing"));
}

FText FNiagaraUserParameterBindingCustomization::GetTooltipText() const
{
	if (BaseSystem && TargetUserParameterBinding && TargetUserParameterBinding->Parameter.IsValid())
	{
		FText TooltipDesc = FText::Format(LOCTEXT("ParameterBindingTooltip", "Bound to the user parameter \"{0}\""), FText::FromName(TargetUserParameterBinding->Parameter.GetName()));
		return TooltipDesc;
	}
	return FText::FromString(TEXT("Missing"));
}

TSharedRef<SWidget> FNiagaraUserParameterBindingCustomization::OnGetMenuContent() const
{
	FGraphActionMenuBuilder MenuBuilder;

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			[
				SNew(SGraphActionMenu)
				.OnActionSelected(const_cast<FNiagaraUserParameterBindingCustomization*>(this), &FNiagaraUserParameterBindingCustomization::OnActionSelected)
		.OnCreateWidgetForAction(SGraphActionMenu::FOnCreateWidgetForAction::CreateSP(const_cast<FNiagaraUserParameterBindingCustomization*>(this), &FNiagaraUserParameterBindingCustomization::OnCreateWidgetForAction))
		.OnCollectAllActions(const_cast<FNiagaraUserParameterBindingCustomization*>(this), &FNiagaraUserParameterBindingCustomization::CollectAllActions)
		.AutoExpandActionMenu(false)
		.ShowFilterTextBox(true)
			]
		];
}

TArray<FName> FNiagaraUserParameterBindingCustomization::GetNames() const
{
	TArray<FName> Names;

	if (BaseSystem && TargetUserParameterBinding)
	{
		for (const FNiagaraVariable& Var : BaseSystem->GetExposedParameters().GetSortedParameterOffsets())
		{
			if (FNiagaraParameterMapHistory::IsUserParameter(Var) && Var.GetType() == TargetUserParameterBinding->Parameter.GetType())
			{
				Names.AddUnique(Var.GetName());
			}
		}
	}
	
	return Names;
}

void FNiagaraUserParameterBindingCustomization::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	TArray<FName> UserParamNames = GetNames();
	for (FName UserParamName : UserParamNames)
	{
		FText CategoryName = FText();
		FString DisplayNameString = FName::NameToDisplayString(UserParamName.ToString(), false);
		const FText NameText = FText::FromString(DisplayNameString);
		const FText TooltipDesc = FText::Format(LOCTEXT("BindToUserParameter", "Bind to the User Parameter \"{0}\" "), FText::FromString(DisplayNameString));
		TSharedPtr<FNiagaraStackAssetAction_VarBind> NewNodeAction(new FNiagaraStackAssetAction_VarBind(UserParamName, CategoryName, NameText,
			TooltipDesc, 0, FText()));
		OutAllActions.AddAction(NewNodeAction);
	}
}

TSharedRef<SWidget> FNiagaraUserParameterBindingCustomization::OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(InCreateData->Action->GetMenuDescription())
		.ToolTipText(InCreateData->Action->GetTooltipDescription())
		];
}


void FNiagaraUserParameterBindingCustomization::OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (int32 ActionIndex = 0; ActionIndex < SelectedActions.Num(); ActionIndex++)
		{
			TSharedPtr<FEdGraphSchemaAction> CurrentAction = SelectedActions[ActionIndex];

			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				FNiagaraStackAssetAction_VarBind* EventSourceAction = (FNiagaraStackAssetAction_VarBind*)CurrentAction.Get();
				ChangeSource(EventSourceAction->VarName);
			}
		}
	}
}

void FNiagaraUserParameterBindingCustomization::ChangeSource(FName InVarName)
{
	FScopedTransaction Transaction(FText::Format(LOCTEXT("ChangeUserParameterSource", " Change User Parameter Source to \"{0}\" "), FText::FromName(InVarName)));
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	for (UObject* Obj : Objects)
	{
		Obj->Modify();
	}

	PropertyHandle->NotifyPreChange();
	TargetUserParameterBinding->Parameter.SetName(InVarName);
	//TargetUserParameterBinding->Parameter.SetType(FNiagaraTypeDefinition::GetUObjectDef()); Do not override the type here!
	//TargetVariableBinding->DataSetVariable = FNiagaraConstants::GetAttributeAsDataSetKey(TargetVariableBinding->BoundVariable);
	PropertyHandle->NotifyPostChange();
	PropertyHandle->NotifyFinishedChangingProperties();
}

void FNiagaraUserParameterBindingCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	bool bAddDefault = true;
	if (Objects.Num() == 1)
	{
		BaseSystem = Objects[0]->GetTypedOuter<UNiagaraSystem>();
		if (BaseSystem)
		{
			TargetUserParameterBinding = (FNiagaraUserParameterBinding*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);

			HeaderRow
				.NameContent()
				[
					PropertyHandle->CreatePropertyNameWidget()
				]
			.ValueContent()
				.MaxDesiredWidth(200.f)
				[
					SNew(SComboButton)
					.OnGetMenuContent(this, &FNiagaraUserParameterBindingCustomization::OnGetMenuContent)
				.ContentPadding(1)
				.ToolTipText(this, &FNiagaraUserParameterBindingCustomization::GetTooltipText)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(this, &FNiagaraUserParameterBindingCustomization::GetCurrentText)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				];

			bAddDefault = false;
		}
	}

	if (bAddDefault)
	{
		HeaderRow
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
		.ValueContent()
			.MaxDesiredWidth(200.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FName::NameToDisplayString(CastField<FStructProperty>(PropertyHandle->GetProperty())->Struct->GetName(), false)))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	}
}

//////////////////////////////////////////////////////////////////////////

FText FNiagaraDataInterfaceBindingCustomization::GetCurrentText() const
{
	if (BaseStage && TargetDataInterfaceBinding)
	{
		return FText::FromName(TargetDataInterfaceBinding->BoundVariable.GetName());
	}
	return FText::FromString(TEXT("Missing"));
}

FText FNiagaraDataInterfaceBindingCustomization::GetTooltipText() const
{
	if (BaseStage && TargetDataInterfaceBinding && TargetDataInterfaceBinding->BoundVariable.IsValid())
	{
		FText TooltipDesc = FText::Format(LOCTEXT("ParameterBindingTooltip", "Bound to the user parameter \"{0}\""), FText::FromName(TargetDataInterfaceBinding->BoundVariable.GetName()));
		return TooltipDesc;
	}
	return FText::FromString(TEXT("Missing"));
}

TSharedRef<SWidget> FNiagaraDataInterfaceBindingCustomization::OnGetMenuContent() const
{
	FGraphActionMenuBuilder MenuBuilder;

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			[
				SNew(SGraphActionMenu)
				.OnActionSelected(const_cast<FNiagaraDataInterfaceBindingCustomization*>(this), &FNiagaraDataInterfaceBindingCustomization::OnActionSelected)
		.OnCreateWidgetForAction(SGraphActionMenu::FOnCreateWidgetForAction::CreateSP(const_cast<FNiagaraDataInterfaceBindingCustomization*>(this), &FNiagaraDataInterfaceBindingCustomization::OnCreateWidgetForAction))
		.OnCollectAllActions(const_cast<FNiagaraDataInterfaceBindingCustomization*>(this), &FNiagaraDataInterfaceBindingCustomization::CollectAllActions)
		.AutoExpandActionMenu(false)
		.ShowFilterTextBox(true)
			]
		];
}

TArray<FName> FNiagaraDataInterfaceBindingCustomization::GetNames() const
{
	TArray<FName> Names;

	if (BaseStage && TargetDataInterfaceBinding)
	{
		UNiagaraEmitter* Emitter = BaseStage->GetTypedOuter<UNiagaraEmitter>();

		if (Emitter)
		{
			// Find all used emitter and particle data interface variables that can be iterated upon.
			TArray<UNiagaraScript*> AllScripts;
			Emitter->GetScripts(AllScripts, false);

			TArray<UNiagaraGraph*> Graphs;
			for (const UNiagaraScript* Script : AllScripts)
			{
				const UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetSource());
				if (Source)
				{
					Graphs.AddUnique(Source->NodeGraph);
				}
			}

			for (const UNiagaraGraph* Graph : Graphs)
			{
				const TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& ParameterReferenceMap = Graph->GetParameterReferenceMap();
				for (const auto& ParameterToReferences : ParameterReferenceMap)
				{
					const FNiagaraVariable& ParameterVariable = ParameterToReferences.Key;
					if (ParameterVariable.IsDataInterface())
					{
						const UClass* Class = ParameterVariable.GetType().GetClass();
						if (Class)
						{
							const UObject* DefaultObjDI = Class->GetDefaultObject();
							if (DefaultObjDI != nullptr && DefaultObjDI->IsA<UNiagaraDataInterfaceRWBase>())
							{
								Names.AddUnique(ParameterVariable.GetName());
							}
						}
					}
				}
			}
		}
	}

	return Names;
}

void FNiagaraDataInterfaceBindingCustomization::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	TArray<FName> UserParamNames = GetNames();
	for (FName UserParamName : UserParamNames)
	{
		FText CategoryName = FText();
		FString DisplayNameString = FName::NameToDisplayString(UserParamName.ToString(), false);
		const FText NameText = FText::FromString(DisplayNameString);
		const FText TooltipDesc = FText::Format(LOCTEXT("BindToDataInterface", "Bind to the User Parameter \"{0}\" "), FText::FromString(DisplayNameString));
		TSharedPtr<FNiagaraStackAssetAction_VarBind> NewNodeAction(new FNiagaraStackAssetAction_VarBind(UserParamName, CategoryName, NameText,
			TooltipDesc, 0, FText()));
		OutAllActions.AddAction(NewNodeAction);
	}
}

TSharedRef<SWidget> FNiagaraDataInterfaceBindingCustomization::OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(InCreateData->Action->GetMenuDescription())
			.ToolTipText(InCreateData->Action->GetTooltipDescription())
		];
}


void FNiagaraDataInterfaceBindingCustomization::OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (int32 ActionIndex = 0; ActionIndex < SelectedActions.Num(); ActionIndex++)
		{
			TSharedPtr<FEdGraphSchemaAction> CurrentAction = SelectedActions[ActionIndex];

			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				FNiagaraStackAssetAction_VarBind* EventSourceAction = (FNiagaraStackAssetAction_VarBind*)CurrentAction.Get();
				ChangeSource(EventSourceAction->VarName);
			}
		}
	}
}

void FNiagaraDataInterfaceBindingCustomization::ChangeSource(FName InVarName)
{
	FScopedTransaction Transaction(FText::Format(LOCTEXT("ChangeDataParameterSource", " Change Data Interface Source to \"{0}\" "), FText::FromName(InVarName)));
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	for (UObject* Obj : Objects)
	{
		Obj->Modify();
	}

	PropertyHandle->NotifyPreChange();
	TargetDataInterfaceBinding->BoundVariable.SetName(InVarName);
	PropertyHandle->NotifyPostChange();
	PropertyHandle->NotifyFinishedChangingProperties();
}

void FNiagaraDataInterfaceBindingCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	bool bAddDefault = true;
	if (Objects.Num() == 1)
	{
		BaseStage = Cast<UNiagaraSimulationStageBase>(Objects[0]);
		if (BaseStage)
		{
			TargetDataInterfaceBinding = (FNiagaraVariableDataInterfaceBinding*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);

			HeaderRow
				.NameContent()
				[
					PropertyHandle->CreatePropertyNameWidget()
				]
			.ValueContent()
				.MaxDesiredWidth(200.f)
				[
					SNew(SComboButton)
					.OnGetMenuContent(this, &FNiagaraDataInterfaceBindingCustomization::OnGetMenuContent)
					.ContentPadding(1)
					.ToolTipText(this, &FNiagaraDataInterfaceBindingCustomization::GetTooltipText)
					.ButtonContent()
				[
					SNew(STextBlock)
					.Text(this, &FNiagaraDataInterfaceBindingCustomization::GetCurrentText)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				];
			bAddDefault = false;
		}
	}


	if (bAddDefault)
	{
		HeaderRow
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MaxDesiredWidth(200.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FName::NameToDisplayString(CastField<FStructProperty>(PropertyHandle->GetProperty())->Struct->GetName(), false)))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	}
}

////////////////////////
FText FNiagaraScriptVariableBindingCustomization::GetCurrentText() const
{
	if (BaseGraph && TargetVariableBinding && TargetVariableBinding->IsValid())
	{
		return FText::FromName(TargetVariableBinding->Name);
	}
	return FText::FromString(TEXT("None"));
}

FText FNiagaraScriptVariableBindingCustomization::GetTooltipText() const
{
	if (BaseGraph && TargetVariableBinding && TargetVariableBinding->IsValid())
	{
		FText TooltipDesc = FText::Format(LOCTEXT("BindingTooltip", "Use the variable \"{0}\" if it is defined, otherwise use the type's default value."), FText::FromName(TargetVariableBinding->Name));
		return TooltipDesc;
	}
	return FText::FromString(TEXT("There is no default binding selected."));
}

TSharedRef<SWidget> FNiagaraScriptVariableBindingCustomization::OnGetMenuContent() const
{
	FGraphActionMenuBuilder MenuBuilder; // TODO: Is this necessary? It's included in all the other implementations above, but it's never used. Spooky

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			[
				SNew(SGraphActionMenu)
				.OnActionSelected(const_cast<FNiagaraScriptVariableBindingCustomization*>(this), &FNiagaraScriptVariableBindingCustomization::OnActionSelected)
				.OnCreateWidgetForAction(SGraphActionMenu::FOnCreateWidgetForAction::CreateSP(const_cast<FNiagaraScriptVariableBindingCustomization*>(this), &FNiagaraScriptVariableBindingCustomization::OnCreateWidgetForAction))
				.OnCollectAllActions(const_cast<FNiagaraScriptVariableBindingCustomization*>(this), &FNiagaraScriptVariableBindingCustomization::CollectAllActions)
				.AutoExpandActionMenu(false)
				.ShowFilterTextBox(true)
			]
		];
}

TArray<FName> FNiagaraScriptVariableBindingCustomization::GetNames() const
{
	// TODO: Only show Particles attributes for valid graphs,
	//       i.e. only show Particles attributes for Particle scripts
	//       and only show Emitter attributes for Emitter and Particle scripts.
	TArray<FName> Names;

	for (const FNiagaraParameterMapHistory& History : UNiagaraNodeParameterMapBase::GetParameterMaps(BaseGraph))
	{
		for (const FNiagaraVariable& Var : History.Variables)
		{
			FString Namespace = FNiagaraParameterMapHistory::GetNamespace(Var);
			if (Namespace == TEXT("Module."))
			{
				// TODO: Skip module inputs for now. Does it make sense to bind module inputs to module inputs?
				continue;
			}
			if (Var.GetType() == BaseScriptVariable->Variable.GetType())
			{
				Names.AddUnique(Var.GetName());
			}
		}
	}

	for (const auto& Var : BaseGraph->GetParameterReferenceMap())
	{
		FString Namespace = FNiagaraParameterMapHistory::GetNamespace(Var.Key);
		if (Namespace == TEXT("Module."))
		{
			// TODO: Skip module inputs for now. Does it make sense to bind module inputs to module inputs?
			continue;
		}
		if (Var.Key.GetType() == BaseScriptVariable->Variable.GetType())
		{
			Names.AddUnique(Var.Key.GetName());
		}
	}

	for (const FNiagaraVariable& Var : FNiagaraConstants::GetEngineConstants())
	{
		if (Var.GetType() == BaseScriptVariable->Variable.GetType())
		{
			Names.AddUnique(Var.GetName());
		}
	}

	for (const FNiagaraVariable& Var : FNiagaraConstants::GetCommonParticleAttributes())
	{
		if (Var.GetType() == BaseScriptVariable->Variable.GetType())
		{
			Names.AddUnique(Var.GetName());
		}
	}

	return Names;
}

void FNiagaraScriptVariableBindingCustomization::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	if (BaseGraph)
	{
		for (FName Name : GetNames())
		{
			const FText NameText = FText::FromName(Name);
			const FText TooltipDesc = FText::Format(LOCTEXT("SetFunctionPopupTooltip", "Use the variable \"{0}\" "), NameText);
			TSharedPtr<FNiagaraStackAssetAction_VarBind> NewNodeAction(
				new FNiagaraStackAssetAction_VarBind(Name, FText(), NameText, TooltipDesc, 0, FText())
			);
			OutAllActions.AddAction(NewNodeAction);
		}
	}
}

TSharedRef<SWidget> FNiagaraScriptVariableBindingCustomization::OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(InCreateData->Action->GetMenuDescription())
			.ToolTipText(InCreateData->Action->GetTooltipDescription())
		];
}

void FNiagaraScriptVariableBindingCustomization::OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (auto& CurrentAction : SelectedActions)
		{
			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				FNiagaraStackAssetAction_VarBind* EventSourceAction = (FNiagaraStackAssetAction_VarBind*)CurrentAction.Get();
				ChangeSource(EventSourceAction->VarName);
			}
		}
	}
}

void FNiagaraScriptVariableBindingCustomization::ChangeSource(FName InVarName)
{
	FScopedTransaction Transaction(FText::Format(LOCTEXT("ChangeBinding", " Change default binding to \"{0}\" "), FText::FromName(InVarName)));
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	for (UObject* Obj : Objects)
	{
		Obj->Modify();
	}

	PropertyHandle->NotifyPreChange();
	TargetVariableBinding->Name = InVarName;
	PropertyHandle->NotifyPostChange();
	PropertyHandle->NotifyFinishedChangingProperties();
}

void FNiagaraScriptVariableBindingCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	bool bAddDefault = true;
	if (Objects.Num() == 1)
	{
		BaseScriptVariable = Cast<UNiagaraScriptVariable>(Objects[0]);
		if (BaseScriptVariable)
		{
		    BaseGraph = Cast<UNiagaraGraph>(BaseScriptVariable->GetOuter());
			if (BaseGraph)
			{
				TargetVariableBinding = (FNiagaraScriptVariableBinding*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);

				HeaderRow
					.NameContent()
					[
						PropertyHandle->CreatePropertyNameWidget()
					]
				.ValueContent()
					.MaxDesiredWidth(200.f)
					[
						SNew(SComboButton)
						.OnGetMenuContent(this, &FNiagaraScriptVariableBindingCustomization::OnGetMenuContent)
						.ContentPadding(1)
						.ToolTipText(this, &FNiagaraScriptVariableBindingCustomization::GetTooltipText)
						.ButtonContent()
						[
							SNew(STextBlock)
							.Text(this, &FNiagaraScriptVariableBindingCustomization::GetCurrentText)
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
					];
				bAddDefault = false;
			}
			else
			{
				BaseScriptVariable = nullptr;
			}
		}
		else
		{
			BaseGraph = nullptr;
		}
	}
	
	if (bAddDefault)
	{
		HeaderRow
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
		.ValueContent()
			.MaxDesiredWidth(200.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FName::NameToDisplayString(CastField<FStructProperty>(PropertyHandle->GetProperty())->Struct->GetName(), false)))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	}
}
//////////////////////////////////////////////////////////////////////////

void FNiagaraPlatformSetTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;
	TArray<UObject*> Objects;
	PropertyHandle->GetOuterObjects(Objects);
	TargetPlatformSet = (FNiagaraPlatformSet*)PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);

	if (PlatformSelectionStates.Num() == 0)
	{
		PlatformSelectionStates.Add(MakeShared<ENiagaraPlatformSelectionState>(ENiagaraPlatformSelectionState::Enabled));
		PlatformSelectionStates.Add(MakeShared<ENiagaraPlatformSelectionState>(ENiagaraPlatformSelectionState::Disabled));
		PlatformSelectionStates.Add(MakeShared<ENiagaraPlatformSelectionState>(ENiagaraPlatformSelectionState::Default));
	}

	//Look for outer types for which we need to ensure there are no conflicting settings.
	SystemScalabilitySettings = nullptr;
	EmitterScalabilitySettings = nullptr;
	SystemScalabilityOverrides = nullptr;
	EmitterScalabilityOverrides = nullptr;
	PlatformSetArray.Reset();
	PlatformSetArrayIndex = INDEX_NONE;

	//Look whether this platform set belongs to a class which must keep orthogonal platform sets.
	//We then interrogate the other sets via these ptrs to look for conflicts.
	TSharedPtr<IPropertyHandle> CurrHandle = PropertyHandle->GetParentHandle();
	while (CurrHandle)
	{
		int32 ThisIndex = CurrHandle->GetIndexInArray();
		if (ThisIndex != INDEX_NONE)
		{
			PlatformSetArray = CurrHandle->GetParentHandle()->AsArray();
			PlatformSetArrayIndex = ThisIndex;
		}

		if (FProperty* CurrProperty = CurrHandle->GetProperty())
		{
			if (UStruct* CurrStruct = CurrProperty->GetOwnerStruct())
			{
				TSharedPtr<IPropertyHandle> ParentHandle = CurrHandle->GetParentHandle();
				if (CurrStruct == FNiagaraSystemScalabilitySettingsArray::StaticStruct())
				{
					SystemScalabilitySettings = (FNiagaraSystemScalabilitySettingsArray*)ParentHandle->GetValueBaseAddress((uint8*)Objects[0]);
					break;
				}
				else if (CurrStruct == FNiagaraEmitterScalabilitySettingsArray::StaticStruct())
				{
					EmitterScalabilitySettings = (FNiagaraEmitterScalabilitySettingsArray*)ParentHandle->GetValueBaseAddress((uint8*)Objects[0]);
					break;
				}
				else if (CurrStruct == FNiagaraSystemScalabilityOverrides::StaticStruct())
				{
					SystemScalabilityOverrides = (FNiagaraSystemScalabilityOverrides*)ParentHandle->GetValueBaseAddress((uint8*)Objects[0]);
					break;
				}
				else if (CurrStruct == FNiagaraEmitterScalabilityOverrides::StaticStruct())
				{
					EmitterScalabilityOverrides = (FNiagaraEmitterScalabilityOverrides*)ParentHandle->GetValueBaseAddress((uint8*)Objects[0]);
					break;
				}
			}
		}

		CurrHandle = CurrHandle->GetParentHandle();
	}

	UpdateCachedConflicts();
	
	BaseSystem = Objects[0]->GetTypedOuter<UNiagaraSystem>();
	PropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FNiagaraPlatformSetTypeCustomization::OnPropertyValueChanged));

	HeaderRow
		.WholeRowContent()
		[ 	
			SAssignNew(EffectsQualityWidgetBox, SWrapBox)
			.UseAllottedWidth(true)
		];

	GenerateEffectsQualitySelectionWidgets();
}

void FNiagaraPlatformSetTypeCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
}

FText FNiagaraPlatformSetTypeCustomization::GetCurrentText() const
{
	return LOCTEXT("Platforms", "Platforms");
}

FText FNiagaraPlatformSetTypeCustomization::GetTooltipText() const
{
	return LOCTEXT("Platforms", "Platforms");
}

void FNiagaraPlatformSetTypeCustomization::GenerateEffectsQualitySelectionWidgets()
{
	EffectsQualityWidgetBox->ClearChildren();

	Scalability::FQualityLevels Counts = Scalability::GetQualityLevelCounts();
	EffectsQualityMenus.SetNum(Counts.EffectsQuality);

	for (int32 EffectsQuality = 0; EffectsQuality < Counts.EffectsQuality; ++EffectsQuality)
	{
		bool First = EffectsQuality == 0;
		bool Last = EffectsQuality == (Counts.EffectsQuality - 1);

		EffectsQualityWidgetBox->AddSlot()
			.Padding(0, 0, 1, 0)
			[
				SNew(SBox)
				.WidthOverride(100)
				.VAlign(VAlign_Top)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SCheckBox)
						.Style(FNiagaraEditorStyle::Get(), First ? "NiagaraEditor.PlatformSet.StartButton" : 
							(Last ? "NiagaraEditor.PlatformSet.EndButton" : "NiagaraEditor.PlatformSet.MiddleButton"))
						.IsChecked(this, &FNiagaraPlatformSetTypeCustomization::IsEQChecked, EffectsQuality)
						.OnCheckStateChanged(this, &FNiagaraPlatformSetTypeCustomization::EQCheckStateChanged, EffectsQuality)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.HAlign(HAlign_Fill)
							.Padding(6,2,2,4)
							[
								SNew(STextBlock)
								.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.PlatformSet.ButtonText")
								.Text(FNiagaraPlatformSet::GetEffectsQualityText(EffectsQuality))
								.ColorAndOpacity(this, &FNiagaraPlatformSetTypeCustomization::GetEffectsQualityButtonTextColor, EffectsQuality)
								.ShadowOffset(FVector2D(1, 1))
							]
							// error icon
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Center)
							.Padding(0,0,2,0)
							[
								SNew(SBox)
								.WidthOverride(12)
								.HeightOverride(12)
								[
									SNew(SImage)
									.ToolTipText(this, &FNiagaraPlatformSetTypeCustomization::GetEffectsQualityErrorToolTip, EffectsQuality)
									.Visibility(this, &FNiagaraPlatformSetTypeCustomization::GetEffectsQualityErrorVisibility, EffectsQuality)
									.Image(FEditorStyle::GetBrush("Icons.Error"))
								]
							]
							// dropdown button
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Center)
							.Padding(0,0,2,0)
							[
								// dropdown button
								SAssignNew(EffectsQualityMenus[EffectsQuality], SMenuAnchor)
								.ToolTipText(LOCTEXT("AddPlatformOverride", "Add an override for a specific platform."))
								.OnGetMenuContent(this, &FNiagaraPlatformSetTypeCustomization::GenerateDeviceProfileTreeWidget, EffectsQuality)
								[
									SNew(SButton)
									.Visibility_Lambda([this, EffectsQuality]()
									{
										return EffectsQualityWidgetBox->GetChildren()->GetChildAt(EffectsQuality)->IsHovered() ? EVisibility::Visible : EVisibility::Hidden;
									})
									.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
									.ForegroundColor(FSlateColor::UseForeground())
									.OnClicked(this, &FNiagaraPlatformSetTypeCustomization::ToggleMenuOpenForEffectsQuality, EffectsQuality)
									[
										SNew(SBox)
										.WidthOverride(8)
										.HeightOverride(8)
										.VAlign(VAlign_Center)
										[
											SNew(SImage)
											.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.DropdownButton"))
										]
									]
								]
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						GenerateAdditionalDevicesWidgetForEQ(EffectsQuality)
					]
				]
			];
	}
}

void FNiagaraPlatformSetTypeCustomization::UpdateCachedConflicts()
{
	TArray<const FNiagaraPlatformSet*> PlatformSets;
	if (SystemScalabilityOverrides != nullptr)
	{
		for (FNiagaraSystemScalabilityOverride& Override : SystemScalabilityOverrides->Overrides)
		{
			PlatformSets.Add(&Override.Platforms);
		}
	}
	else if (EmitterScalabilityOverrides != nullptr)
	{
		for (FNiagaraEmitterScalabilityOverride& Override : EmitterScalabilityOverrides->Overrides)
		{
			PlatformSets.Add(&Override.Platforms);
		}
	}
	else if (SystemScalabilitySettings != nullptr)
	{
		for (FNiagaraSystemScalabilitySettings& Settings : SystemScalabilitySettings->Settings)
		{
			PlatformSets.Add(&Settings.Platforms);
		}
	}
	else if (EmitterScalabilitySettings != nullptr)
	{
		for (FNiagaraEmitterScalabilitySettings& Settings : EmitterScalabilitySettings->Settings)
		{
			PlatformSets.Add(&Settings.Platforms);
		}
	}

	CachedConflicts.Reset();
	FNiagaraPlatformSet::GatherConflicts(PlatformSets, CachedConflicts);
}

static TSharedPtr<IPropertyHandle> FindChildPlatformSet(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	if (FStructProperty* Property = CastField<FStructProperty>(PropertyHandle->GetProperty()))
	{
		if (Property->Struct == FNiagaraPlatformSet::StaticStruct())
		{
			return PropertyHandle;
		}
	}

	// recurse
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	for (int32 Idx = 0; Idx < (int32) NumChildren; ++Idx)
	{
		TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(Idx);

		TSharedPtr<IPropertyHandle> ChildResult = FindChildPlatformSet(Child);
		if (ChildResult.IsValid())
		{
			return ChildResult;
		}
	}

	return TSharedPtr<IPropertyHandle>();
}

void FNiagaraPlatformSetTypeCustomization::InvalidateSiblingConflicts() const
{
	if (!PlatformSetArray.IsValid())
	{
		return;
	}

	uint32 ArrayCount = 0;
	PlatformSetArray->GetNumElements(ArrayCount);
	for (int32 Idx = 0; Idx < (int32) ArrayCount; ++Idx)
	{
		if (Idx == PlatformSetArrayIndex)
		{
			continue; // skip self
		}

		TSharedRef<IPropertyHandle> Sibling = PlatformSetArray->GetElement(Idx);

		TSharedPtr<IPropertyHandle> SiblingPlatformSet = FindChildPlatformSet(Sibling);
		if (SiblingPlatformSet.IsValid())
		{
			SiblingPlatformSet->NotifyPostChange();
		}
	}
}

EVisibility FNiagaraPlatformSetTypeCustomization::GetEffectsQualityErrorVisibility(int32 EffectsQuality) const
{
	// not part of an array
	if (PlatformSetArrayIndex == INDEX_NONE)
	{
		return EVisibility::Collapsed;
	}

	for (const FNiagaraPlatformSetConflictInfo& ConflictInfo : CachedConflicts)
	{
		if (ConflictInfo.SetAIndex == PlatformSetArrayIndex || 
			ConflictInfo.SetBIndex == PlatformSetArrayIndex)
		{
			// this conflict applies to this platform set, check if it applies to this effects quality button
			const int32 EQMask = FNiagaraPlatformSet::CreateEQMask(EffectsQuality);

			for (const FNiagaraPlatformSetConflictEntry& Conflict : ConflictInfo.Conflicts)
			{
				if ((EQMask & Conflict.EffectsQualityMask) != 0)
				{
					return EVisibility::Visible;
				}
			}
		}
	}

	return EVisibility::Collapsed;
}

FText FNiagaraPlatformSetTypeCustomization::GetEffectsQualityErrorToolTip(int32 EffectsQuality) const
{
	if (PlatformSetArrayIndex == INDEX_NONE)
	{
		return FText::GetEmpty();
	}

	for (const FNiagaraPlatformSetConflictInfo& ConflictInfo : CachedConflicts)
	{
		int32 OtherIndex = INDEX_NONE;

		if (ConflictInfo.SetAIndex == PlatformSetArrayIndex)
		{
			OtherIndex = ConflictInfo.SetBIndex;
		}

		if (ConflictInfo.SetBIndex == PlatformSetArrayIndex)
		{
			OtherIndex = ConflictInfo.SetAIndex;
		}

		if (OtherIndex != INDEX_NONE)
		{
			const int32 EQMask = FNiagaraPlatformSet::CreateEQMask(EffectsQuality);

			for (const FNiagaraPlatformSetConflictEntry& Conflict : ConflictInfo.Conflicts)
			{
				if ((EQMask & Conflict.EffectsQualityMask) != 0)
				{
					FText FormatString = LOCTEXT("EffectsQualityConflictToolTip", "This effect quality conflicts with the set at index {Index} in this array."); 

					FFormatNamedArguments Args;
					Args.Add(TEXT("Index"), OtherIndex);

					return FText::Format(FormatString, Args);
				}
			}
		}
	}

	return FText::GetEmpty();
}

FSlateColor FNiagaraPlatformSetTypeCustomization::GetEffectsQualityButtonTextColor(int32 EffectsQuality) const
{
	return TargetPlatformSet->IsEffectQualityEnabled(EffectsQuality) ? 
		FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f)) :
		FSlateColor::UseForeground();
}

TSharedRef<SWidget> FNiagaraPlatformSetTypeCustomization::GenerateAdditionalDevicesWidgetForEQ(int32 EffectsQuality)
{
	TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	auto AddDeviceProfileOverrideWidget = [&](UDeviceProfile* Profile, bool bEnabled)
	{
		TSharedPtr<SHorizontalBox> DeviceBox;

		Container->AddSlot()
			.AutoHeight()
			[
				SAssignNew(DeviceBox, SHorizontalBox)
			];

		const FText DeviceNameText = FText::FromName(Profile->GetFName());

		DeviceBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(3, 0, 3, 0)
			[
				SNew(SBox)
				.WidthOverride(8)
				.HeightOverride(8)
				.ToolTipText(bEnabled ?
					LOCTEXT("DeviceIncludedToolTip", "This device is included.") :
					LOCTEXT("DeviceExcludedToolTip", "This device is excluded."))
				[
					SNew(SImage)
					.Image(bEnabled ?
						FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.Include") :
						FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.Exclude"))
					.ColorAndOpacity(bEnabled ?
						FLinearColor(0, 1, 0) :
						FLinearColor(1, 0, 0))
					]
				];

		DeviceBox->AddSlot()
			.HAlign(HAlign_Fill)
			.Padding(0, 0, 2, 0)
			[
				SNew(STextBlock)
				.Text(DeviceNameText)
				.ToolTipText(DeviceNameText)
			];

		DeviceBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(0, 0, 2, 0)
			[
				SNew(SBox)
				.WidthOverride(12)
				.HeightOverride(12)
				[
					SNew(SImage)
					.ToolTipText(this, &FNiagaraPlatformSetTypeCustomization::GetDeviceProfileErrorToolTip, Profile, EffectsQuality)
					.Visibility(this, &FNiagaraPlatformSetTypeCustomization::GetDeviceProfileErrorVisibility, Profile, EffectsQuality)
					.Image(FEditorStyle::GetBrush("Icons.Error"))
				]
			];

		DeviceBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ForegroundColor(FSlateColor::UseForeground())
				.OnClicked(this, &FNiagaraPlatformSetTypeCustomization::RemoveDeviceProfile, Profile, EffectsQuality)
				.ToolTipText(LOCTEXT("RemoveDevice", "Remove this device override."))
				[
					SNew(SBox)
					.WidthOverride(8)
					.HeightOverride(8)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.Remove"))
					]
				]
			];
		};

	TArray<UDeviceProfile*> EnabledProfiles;
	TArray<UDeviceProfile*> DisabledProfiles;
	TargetPlatformSet->GetOverridenDeviceProfiles(EffectsQuality, EnabledProfiles, DisabledProfiles);

	for (UDeviceProfile* Profile : EnabledProfiles)
	{
		AddDeviceProfileOverrideWidget(Profile, true);
	}

	for (UDeviceProfile* Profile : DisabledProfiles)
	{
		AddDeviceProfileOverrideWidget(Profile, false);
	}

	return Container;
}

FReply FNiagaraPlatformSetTypeCustomization::RemoveDeviceProfile(UDeviceProfile* Profile, int32 EffectsQuality)
{
	PropertyHandle->NotifyPreChange();
	TargetPlatformSet->SetDeviceProfileState(Profile, EffectsQuality, ENiagaraPlatformSelectionState::Default);
	PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	InvalidateSiblingConflicts();

	return FReply::Handled();
}

EVisibility FNiagaraPlatformSetTypeCustomization::GetDeviceProfileErrorVisibility(UDeviceProfile* Profile, int32 EffectsQuality) const
{
	// not part of an array
	if (PlatformSetArrayIndex == INDEX_NONE)
	{
		return EVisibility::Collapsed;
	}

	for (const FNiagaraPlatformSetConflictInfo& ConflictInfo : CachedConflicts)
	{
		if (ConflictInfo.SetAIndex == PlatformSetArrayIndex || 
			ConflictInfo.SetBIndex == PlatformSetArrayIndex)
		{
			const int32 EQMask = FNiagaraPlatformSet::CreateEQMask(EffectsQuality);

			for (const FNiagaraPlatformSetConflictEntry& Entry : ConflictInfo.Conflicts)
			{
				if ((Entry.EffectsQualityMask & EQMask) != 0 &&
					Entry.ProfileName == Profile->GetFName())
				{
					return EVisibility::Visible;
				}
			}
		}
	}

	return EVisibility::Collapsed;
}

FText FNiagaraPlatformSetTypeCustomization::GetDeviceProfileErrorToolTip(UDeviceProfile* Profile, int32 EffectsQuality) const
{
	for (const FNiagaraPlatformSetConflictInfo& ConflictInfo : CachedConflicts)
	{
		int32 OtherIndex = INDEX_NONE;

		if (ConflictInfo.SetAIndex == PlatformSetArrayIndex)
		{
			OtherIndex = ConflictInfo.SetBIndex;
		}

		if (ConflictInfo.SetBIndex == PlatformSetArrayIndex)
		{
			OtherIndex = ConflictInfo.SetAIndex;
		}

		if (OtherIndex != INDEX_NONE)
		{
			const int32 EQMask = FNiagaraPlatformSet::CreateEQMask(EffectsQuality);

			for (const FNiagaraPlatformSetConflictEntry& Entry : ConflictInfo.Conflicts)
			{
				if ((Entry.EffectsQualityMask & EQMask) != 0 &&
					Entry.ProfileName == Profile->GetFName())
				{
					FText FormatString = LOCTEXT("PlatformOverrideConflictToolTip", "This platform override conflicts with the set at index {Index} in this array."); 

					FFormatNamedArguments Args;
					Args.Add(TEXT("Index"), OtherIndex);

					return FText::Format(FormatString, Args);
				}
			}
		}
	}

	return FText::GetEmpty();
}

FReply FNiagaraPlatformSetTypeCustomization::ToggleMenuOpenForEffectsQuality(int32 EffectsQuality)
{
	check(EffectsQualityMenus.IsValidIndex(EffectsQuality));
	
	TSharedPtr<SMenuAnchor> MenuAnchor = EffectsQualityMenus[EffectsQuality];
	MenuAnchor->SetIsOpen(!MenuAnchor->IsOpen());

	return FReply::Handled();
}

// Is or does a viewmodel contain any children active at the given quality?
bool FNiagaraPlatformSetTypeCustomization::IsTreeActiveForEQ(const TSharedPtr<FNiagaraDeviceProfileViewModel>& Tree, int32 EffectsQualityMask) const
{
	int32 Mask = TargetPlatformSet->GetEffectQualityMaskForDeviceProfile(Tree->Profile);
	if ((Mask & EffectsQualityMask) != 0)
	{
		return true;
	}

	for (const TSharedPtr<FNiagaraDeviceProfileViewModel>& Child : Tree->Children)
	{
		if (IsTreeActiveForEQ(Child, EffectsQualityMask))
		{
			return true;
		}
	}

	return false;
}

void FNiagaraPlatformSetTypeCustomization::FilterTreeForEQ(const TSharedPtr<FNiagaraDeviceProfileViewModel>& SourceTree, TSharedPtr<FNiagaraDeviceProfileViewModel>& FilteredTree, int32 EffectsQualityMask)
{
	for (const TSharedPtr<FNiagaraDeviceProfileViewModel>& SourceChild : SourceTree->Children)
	{
		if (IsTreeActiveForEQ(SourceChild, EffectsQualityMask))
		{
			TSharedPtr<FNiagaraDeviceProfileViewModel>& FilteredChild = FilteredTree->Children.Add_GetRef(MakeShared<FNiagaraDeviceProfileViewModel>());
			FilteredChild->Profile = SourceChild->Profile;

			FilterTreeForEQ(SourceChild, FilteredChild, EffectsQualityMask);
		}
	}
}

void FNiagaraPlatformSetTypeCustomization::CreateDeviceProfileTree()
{
	//Pull device profiles out by their hierarchy depth.
	TArray<TArray<UDeviceProfile*>> DeviceProfilesByHierarchyLevel;
	for (UObject* Profile : UDeviceProfileManager::Get().Profiles)
	{
		UDeviceProfile* DeviceProfile = CastChecked<UDeviceProfile>(Profile);

		TFunction<void(int32&, UDeviceProfile*)> FindDepth;
		FindDepth = [&](int32& Depth, UDeviceProfile* CurrProfile)
		{
			if (CurrProfile->Parent)
			{
				FindDepth(++Depth, Cast<UDeviceProfile>(CurrProfile->Parent));
			}
		};

		int32 ProfileDepth = 0;
		FindDepth(ProfileDepth, DeviceProfile);
		DeviceProfilesByHierarchyLevel.SetNum(FMath::Max(ProfileDepth+1, DeviceProfilesByHierarchyLevel.Num()));
		DeviceProfilesByHierarchyLevel[ProfileDepth].Add(DeviceProfile);
	}
	
	FullDeviceProfileTree.Reset(DeviceProfilesByHierarchyLevel[0].Num());
	for (int32 RootProfileIdx = 0; RootProfileIdx < DeviceProfilesByHierarchyLevel[0].Num(); ++RootProfileIdx)
	{
		TFunction<void(FNiagaraDeviceProfileViewModel*, int32)> BuildProfileTree;
		BuildProfileTree = [&](FNiagaraDeviceProfileViewModel* CurrRoot, int32 CurrLevel)
		{
			int32 NextLevel = CurrLevel + 1;
			if (NextLevel < DeviceProfilesByHierarchyLevel.Num())
			{
				for (UDeviceProfile* PossibleChild : DeviceProfilesByHierarchyLevel[NextLevel])
				{
					if (PossibleChild->Parent == CurrRoot->Profile)
					{
						TSharedPtr<FNiagaraDeviceProfileViewModel>& NewChild = CurrRoot->Children.Add_GetRef(MakeShared<FNiagaraDeviceProfileViewModel>());
						NewChild->Profile = PossibleChild;
						BuildProfileTree(NewChild.Get(), NextLevel);
					}
				}
			}
		};

		//Add all root nodes and build their trees.
		TSharedPtr<FNiagaraDeviceProfileViewModel> CurrRoot = MakeShared<FNiagaraDeviceProfileViewModel>();
		CurrRoot->Profile = DeviceProfilesByHierarchyLevel[0][RootProfileIdx];
		BuildProfileTree(CurrRoot.Get(), 0);
		FullDeviceProfileTree.Add(CurrRoot);
	}

	Scalability::FQualityLevels Counts = Scalability::GetQualityLevelCounts();
	FilteredDeviceProfileTrees.SetNum(Counts.EffectsQuality);
	
	for (TSharedPtr<FNiagaraDeviceProfileViewModel>& FullDeviceRoot : FullDeviceProfileTree)
	{
		for (int32 EffectsQuality = 0; EffectsQuality < Counts.EffectsQuality; ++EffectsQuality)
		{
			int32 EffectsQualityMask = FNiagaraPlatformSet::CreateEQMask(EffectsQuality);

			if (IsTreeActiveForEQ(FullDeviceRoot, EffectsQualityMask))
			{
				TArray<TSharedPtr<FNiagaraDeviceProfileViewModel>>& FilteredRoots = FilteredDeviceProfileTrees[EffectsQuality];

				TSharedPtr<FNiagaraDeviceProfileViewModel>& FilteredRoot = FilteredRoots.Add_GetRef(MakeShared<FNiagaraDeviceProfileViewModel>());
				FilteredRoot->Profile = FullDeviceRoot->Profile;

				FilterTreeForEQ(FullDeviceRoot, FilteredRoot, EffectsQualityMask);
			}
		}
	}
}

TSharedRef<SWidget> FNiagaraPlatformSetTypeCustomization::GenerateDeviceProfileTreeWidget(int32 EffectsQuality)
{
	if (FullDeviceProfileTree.Num() == 0)
	{
		CreateDeviceProfileTree();	
	}

	TArray<TSharedPtr<FNiagaraDeviceProfileViewModel>>* TreeToUse = &FullDeviceProfileTree;
	if (EffectsQuality != INDEX_NONE)
	{
		check(EffectsQuality < FilteredDeviceProfileTrees.Num());
		TreeToUse = &FilteredDeviceProfileTrees[EffectsQuality];
	}

	return SNew(SBorder)
		.BorderImage(FEditorStyle::Get().GetBrush("Menu.Background"))
		[
			SAssignNew(DeviceProfileTreeWidget, STreeView<TSharedPtr<FNiagaraDeviceProfileViewModel>>)
			.TreeItemsSource(TreeToUse)
			.OnGenerateRow(this, &FNiagaraPlatformSetTypeCustomization::OnGenerateDeviceProfileTreeRow, EffectsQuality)
			.OnGetChildren(this, &FNiagaraPlatformSetTypeCustomization::OnGetDeviceProfileTreeChildren, EffectsQuality)
			.SelectionMode(ESelectionMode::None)
		];
}

TSharedRef<ITableRow> FNiagaraPlatformSetTypeCustomization::OnGenerateDeviceProfileTreeRow(TSharedPtr<FNiagaraDeviceProfileViewModel> InItem, const TSharedRef<STableViewBase>& OwnerTable, int32 EffectsQuality)
{
	TSharedPtr<SHorizontalBox> RowContainer;
	SAssignNew(RowContainer, SHorizontalBox);

	int32 ProfileMask = TargetPlatformSet->GetEffectQualityMaskForDeviceProfile(InItem->Profile);
	FText NameTooltip = FText::Format(LOCTEXT("ProfileEQTooltipFmt", "Effects Quality: {0}"), FNiagaraPlatformSet::GetEffectsQualityMaskText(ProfileMask));
	
	//Top level profile. Look for a platform icon.
	if (InItem->Profile->Parent == nullptr)
	{
		if (const PlatformInfo::FPlatformInfo* Info = PlatformInfo::FindPlatformInfo(*InItem->Profile->DeviceType))
		{
			const FSlateBrush* DeviceProfileTypeIcon = FEditorStyle::GetBrush(Info->GetIconStyleName(PlatformInfo::EPlatformIconSize::Normal));
			if (DeviceProfileTypeIcon != FEditorStyle::Get().GetDefaultBrush())
			{
				RowContainer->AddSlot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					.Padding(4, 0, 0, 0)
					[
						SNew(SBox)
						.WidthOverride(16)
						.HeightOverride(16)
						[
							SNew(SImage)
							.Image(DeviceProfileTypeIcon)
						]
					];
			}
		}
	}

	FName TextStyleName("NormalText");
	FSlateColor TextColor(FSlateColor::UseForeground());
	ENiagaraPlatformSelectionState CurrentState = TargetPlatformSet->GetDeviceProfileState(InItem->Profile, EffectsQuality);
	
	if (CurrentState == ENiagaraPlatformSelectionState::Enabled)
	{
		TextStyleName = "RichTextBlock.Bold";
		TextColor = FSlateColor(FLinearColor(FVector4(0,1,0,1)));
	}
	else if (CurrentState == ENiagaraPlatformSelectionState::Disabled)
	{
		TextStyleName = "RichTextBlock.Italic";
		TextColor = FSlateColor(FLinearColor(FVector4(1,0,0,1)));
	}


	int32 EffectsQualityMask = FNiagaraPlatformSet::CreateEQMask(EffectsQuality);
	if ((ProfileMask & EffectsQualityMask) == 0)
	{
		TextColor = FSlateColor::UseSubduedForeground();
	}

	RowContainer->AddSlot()
		.Padding(4, 2, 0, 2)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), TextStyleName)
			.Text(FText::FromString(InItem->Profile->GetName()))
			.ToolTipText(NameTooltip)
			.ColorAndOpacity(TextColor)
		];

	RowContainer->AddSlot()
		.AutoWidth()
		.Padding(12, 2, 4, 4)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Fill)
			.Visibility(this, &FNiagaraPlatformSetTypeCustomization::GetProfileMenuButtonVisibility, InItem, EffectsQuality)
			.OnClicked(this, &FNiagaraPlatformSetTypeCustomization::OnProfileMenuButtonClicked, InItem, EffectsQuality)
			.ToolTipText(this, &FNiagaraPlatformSetTypeCustomization::GetProfileMenuButtonToolTip, InItem, EffectsQuality)
			[
				SNew(SBox)
				.WidthOverride(8)
				.HeightOverride(8)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(SImage)
					.Image(this, &FNiagaraPlatformSetTypeCustomization::GetProfileMenuButtonImage, InItem, EffectsQuality)
				]
			]
		];

	return SNew(STableRow<TSharedPtr<FNiagaraDeviceProfileViewModel>>, OwnerTable)
		.Style(FNiagaraEditorStyle::Get(), "NiagaraEditor.PlatformSet.TreeView")
		[
			RowContainer.ToSharedRef()
		];
}

enum class EProfileButtonMode
{
	None,
	Include,
	Exclude,
	Remove
};

static EProfileButtonMode GetProfileMenuButtonMode(FNiagaraPlatformSet* PlatformSet, TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 EffectsQuality)
{
	int32 Mask = PlatformSet->GetEffectQualityMaskForDeviceProfile(Item->Profile);
	int32 EQMask = FNiagaraPlatformSet::CreateEQMask(EffectsQuality);

	if ((Mask & EQMask) == 0)
	{
		return EProfileButtonMode::None;
	}

	ENiagaraPlatformSelectionState CurrentState = PlatformSet->GetDeviceProfileState(Item->Profile, EffectsQuality);

	bool bQualityEnabled = PlatformSet->IsEffectQualityEnabled(EffectsQuality);
	bool bIsDefault = CurrentState == ENiagaraPlatformSelectionState::Default;

	if (bIsDefault && bQualityEnabled)
	{
		return EProfileButtonMode::Exclude;
	}

	if (bIsDefault && !bQualityEnabled)
	{
		return EProfileButtonMode::Include;
	}

	return EProfileButtonMode::Remove;
}


FText FNiagaraPlatformSetTypeCustomization::GetProfileMenuButtonToolTip(TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 EffectsQuality) const
{
	EProfileButtonMode Mode = GetProfileMenuButtonMode(TargetPlatformSet, Item, EffectsQuality);
	switch(Mode)
	{
		case EProfileButtonMode::Include:
			return LOCTEXT("IncludePlatform", "Include this platform.");
		case EProfileButtonMode::Exclude:
			return LOCTEXT("ExcludePlatform", "Exclude this platform.");
		case EProfileButtonMode::Remove:
			return LOCTEXT("RemovePlatform", "Remove this platform override.");
	}

	return FText::GetEmpty();
}

EVisibility FNiagaraPlatformSetTypeCustomization::GetProfileMenuButtonVisibility(TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 EffectsQuality) const
{
	EProfileButtonMode Mode = GetProfileMenuButtonMode(TargetPlatformSet, Item, EffectsQuality);
	if (Mode == EProfileButtonMode::None)
	{
		return EVisibility::Collapsed;
	}

	return EVisibility::Visible;
}

const FSlateBrush* FNiagaraPlatformSetTypeCustomization::GetProfileMenuButtonImage(TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 EffectsQuality) const
{
	EProfileButtonMode Mode = GetProfileMenuButtonMode(TargetPlatformSet, Item, EffectsQuality);
	switch(Mode)
	{
		case EProfileButtonMode::Include:
			return FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.Include");
		case EProfileButtonMode::Exclude:
			return FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.Exclude");
		case EProfileButtonMode::Remove:
			return FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.PlatformSet.Remove");
	}

	return FEditorStyle::GetBrush("NoBrush");
}

FReply FNiagaraPlatformSetTypeCustomization::OnProfileMenuButtonClicked(TSharedPtr<FNiagaraDeviceProfileViewModel> Item, int32 EffectsQuality)
{
	ENiagaraPlatformSelectionState TargetState;

	EProfileButtonMode Mode = GetProfileMenuButtonMode(TargetPlatformSet, Item, EffectsQuality);
	switch(Mode)
	{
		case EProfileButtonMode::Include:
			TargetState = ENiagaraPlatformSelectionState::Enabled;
			break;
		case EProfileButtonMode::Exclude:
			TargetState = ENiagaraPlatformSelectionState::Disabled;
			break;
		case EProfileButtonMode::Remove:
			TargetState = ENiagaraPlatformSelectionState::Default;
			break;
		default:
			return FReply::Handled(); // shouldn't happen, button should be collapsed
	}

	PropertyHandle->NotifyPreChange();
	TargetPlatformSet->SetDeviceProfileState(Item->Profile, EffectsQuality, TargetState);
	PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	InvalidateSiblingConflicts();

	return FReply::Handled();
}

void FNiagaraPlatformSetTypeCustomization::OnGetDeviceProfileTreeChildren(TSharedPtr<FNiagaraDeviceProfileViewModel> InItem, TArray< TSharedPtr<FNiagaraDeviceProfileViewModel> >& OutChildren, int32 EffectsQuality)
{
	if (TargetPlatformSet->GetDeviceProfileState(InItem->Profile, EffectsQuality) == ENiagaraPlatformSelectionState::Default)
	{
		OutChildren = InItem->Children;
	}
}

ECheckBoxState FNiagaraPlatformSetTypeCustomization::IsEQChecked(int32 EffectsQuality)const
{
	if (TargetPlatformSet)
	{
		return TargetPlatformSet->IsEffectQualityEnabled(EffectsQuality) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Undetermined;
}

void FNiagaraPlatformSetTypeCustomization::EQCheckStateChanged(ECheckBoxState CheckState, int32 EffectsQuality)
{
	PropertyHandle->NotifyPreChange();
	TargetPlatformSet->SetEnabledForEffectQuality(EffectsQuality, CheckState == ECheckBoxState::Checked);
	PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	InvalidateSiblingConflicts();
}

void FNiagaraPlatformSetTypeCustomization::OnPropertyValueChanged()
{
	GenerateEffectsQualitySelectionWidgets();
	UpdateCachedConflicts();
}

#undef LOCTEXT_NAMESPACE
