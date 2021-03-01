// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigElementDetails.h"
#include "Widgets/SWidget.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SRotatorInputBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SComboBox.h"
#include "SSearchableComboBox.h"
#include "Editor/SControlRigGizmoNameList.h"
#include "ControlRigBlueprint.h"
#include "Graph/ControlRigGraph.h"
#include "PropertyCustomizationHelpers.h"
#include "SEnumCombo.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"

#define LOCTEXT_NAMESPACE "ControlRigElementDetails"

void RigElementDetails_GetCustomizedInfo(TSharedRef<IPropertyHandle> InStructPropertyHandle, UControlRigBlueprint*& OutBlueprint)
{
	TArray<UObject*> Objects;
	InStructPropertyHandle->GetOuterObjects(Objects);
	for (UObject* Object : Objects)
	{
		if (Object->IsA<UControlRigBlueprint>())
		{
			OutBlueprint = Cast<UControlRigBlueprint>(Object);
			if (OutBlueprint)
			{
				break;
			}
		}
	}

	if (OutBlueprint == nullptr)
	{
		TArray<UPackage*> Packages;
		InStructPropertyHandle->GetOuterPackages(Packages);
		for (UPackage* Package : Packages)
		{
			if (Package == nullptr)
			{
				continue;
			}

			TArray<UObject*> SubObjects;
			Package->GetDefaultSubobjects(SubObjects);
			for (UObject* SubObject : SubObjects)
			{
				if (UControlRig* Rig = Cast<UControlRig>(SubObject))
				{
					OutBlueprint = Cast<UControlRigBlueprint>(Rig->GetClass()->ClassGeneratedBy);
					if (OutBlueprint)
					{
						break;
					}
				}
			}

			if (OutBlueprint)
			{
				break;
			}
		}
	}
}

void FRigElementKeyDetails::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	BlueprintBeingCustomized = nullptr;
	RigElementDetails_GetCustomizedInfo(InStructPropertyHandle, BlueprintBeingCustomized);

	UControlRigGraph* RigGraph = nullptr;
	if(BlueprintBeingCustomized)
	{
		for (UEdGraph* Graph : BlueprintBeingCustomized->UbergraphPages)
		{
			RigGraph = Cast<UControlRigGraph>(Graph);
			if (RigGraph)
			{
				break;
			}
		}
	}

	// only allow blueprints with at least one rig graph
	if (RigGraph == nullptr)
	{
		BlueprintBeingCustomized = nullptr;
	}

	if (BlueprintBeingCustomized == nullptr)
	{
		HeaderRow
		.NameContent()
		[
			InStructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			InStructPropertyHandle->CreatePropertyValueWidget()
		];
	}
	else
	{
		TypeHandle = InStructPropertyHandle->GetChildHandle(TEXT("Type"));
		NameHandle = InStructPropertyHandle->GetChildHandle(TEXT("Name"));

		TypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda(
			[this]()
			{
				this->UpdateElementNameList();
				SetElementName(FString());
			}
		));

		UpdateElementNameList();

		HeaderRow
		.NameContent()
		[
			InStructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				TypeHandle->CreatePropertyValueWidget()
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SSearchableComboBox)
				.OptionsSource(&ElementNameList)
				.OnSelectionChanged(this, &FRigElementKeyDetails::OnElementNameChanged)
				.OnGenerateWidget(this, &FRigElementKeyDetails::OnGetElementNameWidget)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &FRigElementKeyDetails::GetElementNameAsText)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]
		];
	}
}

void FRigElementKeyDetails::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	if (InStructPropertyHandle->IsValidHandle())
	{
		// only fill the children if the blueprint cannot be found
		if (BlueprintBeingCustomized == nullptr)
		{
			uint32 NumChildren = 0;
			InStructPropertyHandle->GetNumChildren(NumChildren);

			for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ChildIndex++)
			{
				StructBuilder.AddProperty(InStructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef());
			}
		}
	}
}

ERigElementType FRigElementKeyDetails::GetElementType() const
{
	ERigElementType ElementType = ERigElementType::None;
	if (TypeHandle.IsValid())
	{
		uint8 Index = 0;
		TypeHandle->GetValue(Index);
		ElementType = (ERigElementType)Index;
	}
	return ElementType;
}

FString FRigElementKeyDetails::GetElementName() const
{
	FString ElementNameStr;
	if (NameHandle.IsValid())
	{
		FName ElementName;
		NameHandle->GetValue(ElementName);
		ElementNameStr = ElementName.ToString();
	}
	return ElementNameStr;
}

void FRigElementKeyDetails::SetElementName(FString InName)
{
	if (NameHandle.IsValid())
	{
		NameHandle->SetValue(InName);
	}
}

void FRigElementKeyDetails::UpdateElementNameList()
{
	if (!TypeHandle.IsValid())
	{
		return;
	}

	ElementNameList.Reset();

	if (BlueprintBeingCustomized)
	{
		for (UEdGraph* Graph : BlueprintBeingCustomized->UbergraphPages)
		{
			if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph))
			{
				ElementNameList = RigGraph->GetElementNameList(GetElementType());
				return;
			}
		}
	}
}

void FRigElementKeyDetails::OnElementNameChanged(TSharedPtr<FString> InItem, ESelectInfo::Type InSelectionInfo)
{
	if (InItem.IsValid())
	{
		SetElementName(*InItem);
	}
	else
	{
		SetElementName(FString());
	}
}

TSharedRef<SWidget> FRigElementKeyDetails::OnGetElementNameWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock)
		.Text(FText::FromString(InItem.IsValid() ? *InItem : FString()))
		.Font(IDetailLayoutBuilder::GetDetailFont());
}

FText FRigElementKeyDetails::GetElementNameAsText() const
{
	return FText::FromString(GetElementName());
}

void FRigUnitDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TSharedPtr<FStructOnScope>> StructsBeingCustomized;
	DetailBuilder.GetStructsBeingCustomized(StructsBeingCustomized);
	if (StructsBeingCustomized.Num() == 0)
	{
		return;
	}

	TSharedPtr<FStructOnScope> StructBeingCustomized = StructsBeingCustomized[0];

	BlueprintBeingCustomized = nullptr;
	if (UPackage* Package = StructBeingCustomized->GetPackage())
	{
		TArray<UObject*> SubObjects;
		Package->GetDefaultSubobjects(SubObjects);

		for (UObject* SubObject : SubObjects)
		{
			if (UControlRig* Rig = Cast<UControlRig>(SubObject))
			{
				BlueprintBeingCustomized = Cast<UControlRigBlueprint>(Rig->GetClass()->ClassGeneratedBy);
				if (BlueprintBeingCustomized)
				{
					break;
				}
			}
		}
	}

	if (BlueprintBeingCustomized == nullptr)
	{
		return;
	}

	GraphBeingCustomized = nullptr;
	for (UEdGraph* Graph : BlueprintBeingCustomized->UbergraphPages)
	{
		GraphBeingCustomized = Cast<UControlRigGraph>(Graph);
		if (GraphBeingCustomized)
		{
			break;
		}
	}

	if (GraphBeingCustomized == nullptr)
	{
		return;
	}

	UScriptStruct* ScriptStruct = Cast<UScriptStruct>((UStruct*)StructBeingCustomized->GetStruct());
	check(ScriptStruct);

	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(*ScriptStruct->GetDisplayNameText().ToString());

	for (TFieldIterator<FProperty> PropertyIt(ScriptStruct); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		TSharedPtr<IPropertyHandle> PropertyHandle = DetailBuilder.GetProperty(Property->GetFName(), ScriptStruct);
		if (!PropertyHandle->IsValidHandle())
		{
			continue;
		}
		DetailBuilder.HideProperty(PropertyHandle);

		if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			FString CustomWidgetName = NameProperty->GetMetaData(TEXT("CustomWidget"));
			if (!CustomWidgetName.IsEmpty())
			{
				const TArray<TSharedPtr<FString>>* NameList = nullptr;
				if (CustomWidgetName == TEXT("BoneName"))
				{
					NameList = &GraphBeingCustomized->GetBoneNameList();
				}
				else if (CustomWidgetName == TEXT("ControlName"))
				{
					NameList = &GraphBeingCustomized->GetControlNameList();
				}
				else if (CustomWidgetName == TEXT("SpaceName"))
				{
					NameList = &GraphBeingCustomized->GetSpaceNameList();
				}
				else if (CustomWidgetName == TEXT("CurveName"))
				{
					NameList = &GraphBeingCustomized->GetCurveNameList();
				}

				if (NameList)
				{
					TSharedPtr<SControlRigGraphPinNameListValueWidget> NameListWidget;

					CategoryBuilder.AddCustomRow(FText::FromString(Property->GetName()))
						.NameContent()
						[
							PropertyHandle->CreatePropertyNameWidget()
						]
						.ValueContent()
						[
							SAssignNew(NameListWidget, SControlRigGraphPinNameListValueWidget)
							.OptionsSource(NameList)
							.OnGenerateWidget(this, &FRigUnitDetails::MakeNameListItemWidget)
							.OnSelectionChanged(this, &FRigUnitDetails::OnNameListChanged, StructBeingCustomized, NameProperty, DetailBuilder.GetPropertyUtilities())
							.OnComboBoxOpening(this, &FRigUnitDetails::OnNameListComboBox, StructBeingCustomized, NameProperty, NameList)
							.InitiallySelectedItem(GetCurrentlySelectedItem(StructBeingCustomized, NameProperty, NameList))
							.Content()
							[
								SNew(STextBlock)
								.Text(this, &FRigUnitDetails::GetNameListText, StructBeingCustomized, NameProperty)
							]
						];

					NameListWidgets.Add(Property->GetFName(), NameListWidget);
				}
				else
				{
					CategoryBuilder.AddCustomRow(FText::FromString(Property->GetName()))
						.NameContent()
						[
							PropertyHandle->CreatePropertyNameWidget()
						];
				}
				continue;
			}
		}
		CategoryBuilder.AddProperty(PropertyHandle);
	}
}

TSharedRef<SWidget> FRigUnitDetails::MakeNameListItemWidget(TSharedPtr<FString> InItem)
{
	return 	SNew(STextBlock).Text(FText::FromString(*InItem));// .Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));
}

FText FRigUnitDetails::GetNameListText(TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty) const
{
	if (FName* Value = InProperty->ContainerPtrToValuePtr<FName>(InStructOnScope->GetStructMemory()))
	{
		return FText::FromName(*Value);
	}
	return FText();
}

TSharedPtr<FString> FRigUnitDetails::GetCurrentlySelectedItem(TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty, const TArray<TSharedPtr<FString>>* InNameList) const
{
	FString CurrentItem = GetNameListText(InStructOnScope, InProperty).ToString();
	for (const TSharedPtr<FString>& Item : *InNameList)
	{
		if (Item->Equals(CurrentItem))
		{
			return Item;
		}
	}

	return TSharedPtr<FString>();
}


void FRigUnitDetails::SetNameListText(const FText& NewTypeInValue, ETextCommit::Type /*CommitInfo*/, TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty, TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	if (FName* Value = InProperty->ContainerPtrToValuePtr<FName>(InStructOnScope->GetStructMemory()))
	{
		*Value = *NewTypeInValue.ToString();

		FPropertyChangedEvent ChangeEvent(InProperty, EPropertyChangeType::ValueSet);
		PropertyUtilities->NotifyFinishedChangingProperties(ChangeEvent);
	}
}

void FRigUnitDetails::OnNameListChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo, TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty, TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		FString NewValue = *NewSelection.Get();
		SetNameListText(FText::FromString(NewValue), ETextCommit::OnEnter, InStructOnScope, InProperty, PropertyUtilities);
	}
}

void FRigUnitDetails::OnNameListComboBox(TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty, const TArray<TSharedPtr<FString>>* InNameList)
{
	TSharedPtr<SControlRigGraphPinNameListValueWidget> Widget = NameListWidgets.FindChecked(InProperty->GetFName());
	const TSharedPtr<FString> CurrentlySelected = GetCurrentlySelectedItem(InStructOnScope, InProperty, InNameList);
	Widget->SetSelectedItem(CurrentlySelected);
}

void FRigComputedTransformDetails::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	BlueprintBeingCustomized = nullptr;
	RigElementDetails_GetCustomizedInfo(InStructPropertyHandle, BlueprintBeingCustomized);
}

void FRigComputedTransformDetails::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TransformHandle = InStructPropertyHandle->GetChildHandle(TEXT("Transform"));

	StructBuilder
	.AddProperty(TransformHandle.ToSharedRef())
	.DisplayName(InStructPropertyHandle->GetPropertyDisplayName());

    FString PropertyPath = TransformHandle->GeneratePathToProperty();

	if(PropertyPath.StartsWith(TEXT("Struct.")))
	{
		PropertyPath.RightChopInline(7);
	}

	if(PropertyPath.StartsWith(TEXT("Pose.")))
	{
		PropertyPath.RightChopInline(5);
		PropertyChain.AddTail(FRigTransformElement::StaticStruct()->FindPropertyByName(TEXT("Pose")));
	}
	else if(PropertyPath.StartsWith(TEXT("Offset.")))
	{
		PropertyPath.RightChopInline(7);
		PropertyChain.AddTail(FRigControlElement::StaticStruct()->FindPropertyByName(TEXT("Offset")));
	}
	else if(PropertyPath.StartsWith(TEXT("Gizmo.")))
	{
		PropertyPath.RightChopInline(6);
		PropertyChain.AddTail(FRigControlElement::StaticStruct()->FindPropertyByName(TEXT("Gizmo")));
	}

	if(PropertyPath.StartsWith(TEXT("Current.")))
	{
		PropertyPath.RightChopInline(8);
		PropertyChain.AddTail(FRigCurrentAndInitialTransform::StaticStruct()->FindPropertyByName(TEXT("Current")));
	}
	else if(PropertyPath.StartsWith(TEXT("Initial.")))
	{
		PropertyPath.RightChopInline(8);
		PropertyChain.AddTail(FRigCurrentAndInitialTransform::StaticStruct()->FindPropertyByName(TEXT("Initial")));
	}

	if(PropertyPath.StartsWith(TEXT("Local.")))
	{
		PropertyPath.RightChopInline(6);
		PropertyChain.AddTail(FRigLocalAndGlobalTransform::StaticStruct()->FindPropertyByName(TEXT("Local")));
	}
	else if(PropertyPath.StartsWith(TEXT("Global.")))
	{
		PropertyPath.RightChopInline(7);
		PropertyChain.AddTail(FRigLocalAndGlobalTransform::StaticStruct()->FindPropertyByName(TEXT("Global")));
	}

	PropertyChain.AddTail(TransformHandle->GetProperty());
	PropertyChain.SetActiveMemberPropertyNode(PropertyChain.GetTail()->GetValue());

	const FSimpleDelegate OnTransformChangedDelegate = FSimpleDelegate::CreateSP(this, &FRigComputedTransformDetails::OnTransformChanged, &PropertyChain);
	TransformHandle->SetOnPropertyValueChanged(OnTransformChangedDelegate);
	TransformHandle->SetOnChildPropertyValueChanged(OnTransformChangedDelegate);
}

void FRigComputedTransformDetails::OnTransformChanged(FEditPropertyChain* InPropertyChain)
{
	if(BlueprintBeingCustomized && InPropertyChain)
	{
		if(InPropertyChain->Num() > 1)
		{
			FPropertyChangedEvent ChangeEvent(InPropertyChain->GetHead()->GetValue(), EPropertyChangeType::ValueSet);
			ChangeEvent.SetActiveMemberProperty(InPropertyChain->GetTail()->GetValue());
			FPropertyChangedChainEvent ChainEvent(*InPropertyChain, ChangeEvent);
			BlueprintBeingCustomized->BroadcastPostEditChangeChainProperty(ChainEvent);
		}
	}
}

void FRigBaseElementDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.HideProperty(TEXT("Key"), FRigBaseElement::StaticStruct());
	DetailBuilder.HideProperty(TEXT("Index"), FRigBaseElement::StaticStruct());
	DetailBuilder.HideProperty(TEXT("SubIndex"), FRigBaseElement::StaticStruct());

	TArray<TSharedPtr<FStructOnScope>> StructsBeingCustomized;
	DetailBuilder.GetStructsBeingCustomized(StructsBeingCustomized);
	for (TSharedPtr<FStructOnScope> StructBeingCustomized : StructsBeingCustomized)
	{
		if (UPackage* Package = StructBeingCustomized->GetPackage())
		{
			TArray<UObject*> SubObjects;
			Package->GetDefaultSubobjects(SubObjects);

			for (UObject* SubObject : SubObjects)
			{
				if (UControlRig* Rig = Cast<UControlRig>(SubObject))
				{
					BlueprintBeingCustomized = Cast<UControlRigBlueprint>(Rig->GetClass()->ClassGeneratedBy);
					if(BlueprintBeingCustomized)
					{
						HierarchyBeingCustomized = BlueprintBeingCustomized->Hierarchy;
						if (UControlRig* DebuggedControlRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
						{
							if (!DebuggedControlRig->IsSetupModeEnabled())
							{
								HierarchyBeingCustomized = DebuggedControlRig->GetHierarchy();
							}
						}
						break;
					}
				}
			}

			if (HierarchyBeingCustomized)
			{
				ElementKeyBeingCustomized = ((const FRigBaseElement*)StructBeingCustomized->GetStructMemory())->GetKey();
				break;
			}
		}
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("RigElement"));
	Category.InitiallyCollapsed(false);

	Category.AddCustomRow(FText::FromString(TEXT("Name")))
    .NameContent()
    [
        SNew(STextBlock)
        .Text(FText::FromString(TEXT("Name")))
        .Font(IDetailLayoutBuilder::GetDetailFont())
    ]
    .ValueContent()
    [
        SNew(SEditableTextBox)
        .Font(IDetailLayoutBuilder::GetDetailFont())
        .Text(this, &FRigBaseElementDetails::GetName)
        .OnTextCommitted(this, &FRigBaseElementDetails::SetName)
    ];
}

void FRigBaseElementDetails::SetName(const FText& InNewText, ETextCommit::Type InCommitType)
{
	URigHierarchy* Hierarchy = nullptr;
	if (BlueprintBeingCustomized)
	{
		Hierarchy = BlueprintBeingCustomized->Hierarchy;
	}
	else
	{
		Hierarchy = GetHierarchy();
	}

	if (Hierarchy)
	{
		URigHierarchyController* Controller = Hierarchy->GetController(true);
		check(Controller);
		Controller->RenameElement(ElementKeyBeingCustomized, *InNewText.ToString(), true);
	}
}

void FRigBaseElementDetails::OnStructContentsChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	const FPropertyChangedEvent ChangeEvent(InProperty, EPropertyChangeType::ValueSet);
	PropertyUtilities->NotifyFinishedChangingProperties(ChangeEvent);
}

void FRigTransformElementDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FRigBaseElementDetails::CustomizeDetails(DetailBuilder);

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Pose"));

	Category.InitiallyCollapsed(ElementKeyBeingCustomized.Type != ERigElementType::Control);
}

void FRigBoneElementDetails::CustomizeDetails( IDetailLayoutBuilder& DetailBuilder )
{
	FRigTransformElementDetails::CustomizeDetails(DetailBuilder);
}

void FRigControlElementDetails_SetupBoolValueWidget(IDetailCategoryBuilder& InCategory, ERigControlValueType InValueType, FRigControlElement* InControlElement, URigHierarchy* InHierarchy)
{
	UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
	UEnum* ValueTypeEnum = StaticEnum<ERigControlValueType>();

	const FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue((int64)InValueType).ToString();
	const FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s Value"), *ValueTypeName));
	TWeakObjectPtr<URigHierarchy> HierarchyPtr = InHierarchy;
	const FRigElementKey Key = InControlElement->GetKey();

	InCategory.AddCustomRow(PropertyLabel)
	.NameContent()
	[
	    SNew(STextBlock)
		.Text(PropertyLabel)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
	    SNew(SVerticalBox)
		+ SVerticalBox::Slot()
	    [
	        SNew(SCheckBox)
	        .IsChecked_Lambda([HierarchyPtr, Key, InValueType]() -> ECheckBoxState
	        {
	        	if(HierarchyPtr.IsValid())
	        	{
	        		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
	        		{
                        bool Value = HierarchyPtr->GetControlValue(ControlElement, InValueType).Get<bool>();
						return Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                    }
	        	}
	        	return ECheckBoxState::Unchecked;
	        })
	        .OnCheckStateChanged_Lambda([HierarchyPtr, Key, InValueType](ECheckBoxState NewState)
	        {
	        	if(HierarchyPtr.IsValid())
	        	{
	        		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
	        		{
                        const FRigControlValue Value = FRigControlValue::Make<bool>(NewState == ECheckBoxState::Checked);
						HierarchyPtr->SetControlValue(ControlElement->GetKey(), Value, InValueType, true);
                    }
	        	}
	        })
	    ]
	]
	.IsEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([HierarchyPtr, Key, InValueType]()->bool
    {
		if(HierarchyPtr.IsValid())
		{
			if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
			{
				return ControlElement->Settings.IsValueTypeEnabled(InValueType);
			}
		}
		return false;
    })));
}

void FRigControlElementDetails_SetupIntegerValueWidget(IDetailCategoryBuilder& InCategory, ERigControlValueType InValueType, FRigControlElement* InControlElement, URigHierarchy* InHierarchy)
{
	UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
	UEnum* ValueTypeEnum = StaticEnum<ERigControlValueType>();

	const FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue((int64)InValueType).ToString();
	const FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s Value"), *ValueTypeName));
	TWeakObjectPtr<URigHierarchy> HierarchyPtr = InHierarchy;
	const FRigElementKey Key = InControlElement->GetKey();

	const TAttribute<bool> EnabledAttribute = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([HierarchyPtr, Key, InValueType]()->bool
    {
		if(HierarchyPtr.IsValid())
		{
            if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
            {
                return ControlElement->Settings.IsValueTypeEnabled(InValueType);
            }
        }
		return false;
    }));

	const TAttribute<EVisibility> VisibilityAttribute = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([EnabledAttribute]()->EVisibility
	{
		return EnabledAttribute.Get() ? EVisibility::Visible : EVisibility::Hidden;
	}));

	if (InControlElement->Settings.ControlEnum)
	{
		InCategory.AddCustomRow(PropertyLabel)
		.Visibility(VisibilityAttribute)
        .NameContent()
        [
            SNew(STextBlock)
            .Text(PropertyLabel)
            .Font(IDetailLayoutBuilder::GetDetailFont())
        ]
        .ValueContent()
        .MinDesiredWidth(125.0f * 3.0f) // copied from FComponentTransformDetails
        .MaxDesiredWidth(125.0f * 3.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            [
                SNew(SEnumComboBox, InControlElement->Settings.ControlEnum)
                .CurrentValue_Lambda([HierarchyPtr, Key, InValueType]() -> int32
                {
                	if(HierarchyPtr.IsValid())
                	{
                		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
                		{
                            return HierarchyPtr->GetControlValue(ControlElement, InValueType).Get<int32>();
                        }
                	}
                	return 0;
                })
                .OnEnumSelectionChanged_Lambda([HierarchyPtr, Key, InValueType](int32 NewSelection, ESelectInfo::Type)
                {
                	if(HierarchyPtr.IsValid())
                	{
                		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
                		{
                            const FRigControlValue Value = FRigControlValue::Make<int32>(NewSelection);
                            HierarchyPtr->SetControlValue(ControlElement->GetKey(), Value, InValueType, true);
                        }
                	}
                })
                .Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
            ]
        ]
		.IsEnabled(EnabledAttribute);
	}
	else
	{
		InCategory.AddCustomRow(PropertyLabel)
		.Visibility(VisibilityAttribute)
        .NameContent()
        [
            SNew(STextBlock)
            .Text(PropertyLabel)
            .Font(IDetailLayoutBuilder::GetDetailFont())
        ]
        .ValueContent()
        .MinDesiredWidth(125.0f * 3.0f) // copied from FComponentTransformDetails
        .MaxDesiredWidth(125.0f * 3.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            [
                SNew(SNumericEntryBox<int32>)
                .Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
                .AllowSpin(true)
                .MinSliderValue_Lambda([HierarchyPtr, Key, InValueType]() -> TOptional<int32>
				{
				    if(InValueType == ERigControlValueType::Current || InValueType == ERigControlValueType::Initial)
				    {
				    	if(HierarchyPtr.IsValid())
				    	{
				    		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
				    		{
                                return ControlElement->Settings.MinimumValue.Get<int32>();
                            }
				    	}
				    }
				    return TOptional<int32>();
				})
				.MaxSliderValue_Lambda([HierarchyPtr, Key, InValueType]() -> TOptional<int32>
				{
				    if(InValueType == ERigControlValueType::Current || InValueType == ERigControlValueType::Initial)
				    {
				    	if(HierarchyPtr.IsValid())
				    	{
				    		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
				    		{
                                return ControlElement->Settings.MaximumValue.Get<int32>();
                            }
				    	}
				    }
				    return TOptional<int32>();
				})
                .Value_Lambda([HierarchyPtr, Key, InValueType]() -> int32
                {
                	if(HierarchyPtr.IsValid())
                	{
                		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
                		{
                            return HierarchyPtr->GetControlValue(ControlElement, InValueType).Get<int32>();
                        }
                	}
                	return 0;
                })
                .OnValueChanged_Lambda([HierarchyPtr, Key, InValueType](TOptional<int32> InNewSelection)
                {
                	if(InNewSelection.IsSet())
                	{
                		if(HierarchyPtr.IsValid())
                		{
                			if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
                			{
                                const FRigControlValue Value = FRigControlValue::Make<int32>(InNewSelection.GetValue());
								HierarchyPtr->SetControlValue(ControlElement->GetKey(), Value, InValueType, true);
                            }
                		}
                	}
                })
            ]
        ]
		.IsEnabled(EnabledAttribute);
	}
}

void FRigControlElementDetails_SetupFloatValueWidget(IDetailCategoryBuilder& InCategory, ERigControlValueType InValueType, FRigControlElement* InControlElement, URigHierarchy* InHierarchy)
{
	UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
	UEnum* ValueTypeEnum = StaticEnum<ERigControlValueType>();

	const FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue((int64)InValueType).ToString();
	const FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s Value"), *ValueTypeName));
	TWeakObjectPtr<URigHierarchy> HierarchyPtr = InHierarchy;
	const FRigElementKey Key = InControlElement->GetKey();

	const TAttribute<bool> EnabledAttribute = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([HierarchyPtr, Key, InValueType]()->bool
    {
        if(HierarchyPtr.IsValid())
        {
            if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
            {
                return ControlElement->Settings.IsValueTypeEnabled(InValueType);
            }
        }
        return false;
    }));

	const TAttribute<EVisibility> VisibilityAttribute = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([EnabledAttribute]()->EVisibility
    {
        return EnabledAttribute.Get() ? EVisibility::Visible : EVisibility::Hidden;
    }));

	InCategory.AddCustomRow(PropertyLabel)
	.Visibility(VisibilityAttribute)
    .NameContent()
    [
        SNew(STextBlock)
        .Text(PropertyLabel)
        .Font(IDetailLayoutBuilder::GetDetailFont())
    ]
    .ValueContent()
    .MinDesiredWidth(125.0f * 3.0f) // copied from FComponentTransformDetails
    .MaxDesiredWidth(125.0f * 3.0f)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        [
            SNew(SNumericEntryBox<float>)
            .Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
            .AllowSpin(true)
            .Value_Lambda([HierarchyPtr, Key, InValueType]() -> float
            {
            	if(HierarchyPtr.IsValid())
            	{
            		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
            		{
                        return HierarchyPtr->GetControlValue(ControlElement, InValueType).Get<float>();
                    }
            	}
            	return 0.f;
            })
            .MinSliderValue_Lambda([HierarchyPtr, Key, InValueType]() -> TOptional<float>
            {
                if(InValueType == ERigControlValueType::Current || InValueType == ERigControlValueType::Initial)
                {
                	if(HierarchyPtr.IsValid())
                	{
                		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
                		{
                            return ControlElement->Settings.MinimumValue.Get<float>();
                        }
                	}
                }
                return TOptional<float>();
            })
            .MaxSliderValue_Lambda([HierarchyPtr, Key, InValueType]() -> TOptional<float>
            {
                if(InValueType == ERigControlValueType::Current || InValueType == ERigControlValueType::Initial)
                {
                	if(HierarchyPtr.IsValid())
                	{
                		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
                		{
                            return ControlElement->Settings.MaximumValue.Get<float>();
                        }
                	}
                }
                return TOptional<float>();
            })
            .OnValueChanged_Lambda([HierarchyPtr, Key, InValueType](TOptional<float> InNewSelection)
            {
            	if(InNewSelection.IsSet())
            	{
            		if(HierarchyPtr.IsValid())
            		{
            			if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
            			{
                            const FRigControlValue Value = FRigControlValue::Make<float>(InNewSelection.GetValue());
                            HierarchyPtr->SetControlValue(ControlElement->GetKey(), Value, InValueType, true);
                        }
            		}
            	}
            })
        ]
    ]
	.IsEnabled(EnabledAttribute);
}

template<typename T>
void FRigControlElementDetails_SetupStructValueWidget(IDetailCategoryBuilder& InCategory, ERigControlValueType InValueType, FRigControlElement* InControlElement, URigHierarchy* InHierarchy)
{
	UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
	UEnum* ValueTypeEnum = StaticEnum<ERigControlValueType>();

	const FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue((int64)InValueType).ToString();
	const FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s Value"), *ValueTypeName));
	const UStruct* ValueStruct = TBaseStructure<T>::Get();

	const TSharedPtr<FStructOnScope> StructToDisplay = MakeShareable(new FStructOnScope(ValueStruct));

	TWeakObjectPtr<URigHierarchy> HierarchyPtr = InHierarchy;
	const FRigElementKey Key = InControlElement->GetKey();

	const TAttribute<bool> EnabledAttribute = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([HierarchyPtr, Key, InValueType]()->bool
    {
        if(HierarchyPtr.IsValid())
        {
            if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
            {
                return ControlElement->Settings.IsValueTypeEnabled(InValueType);
            }
        }
        return false;
    }));

	const TAttribute<EVisibility> VisibilityAttribute = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([HierarchyPtr, Key, InValueType]()->EVisibility
    {
		if(HierarchyPtr.IsValid())
		{
			if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
			{
                if(ControlElement->Settings.IsValueTypeEnabled(InValueType))
                {
                    return EVisibility::Visible;
                }
            }
		}
		return EVisibility::Hidden;
    }));

	IDetailPropertyRow* Row = InCategory.AddExternalStructure(StructToDisplay);
	Row->DisplayName(PropertyLabel);
	Row->ShouldAutoExpand(true);
	Row->IsEnabled(EnabledAttribute);
	Row->Visibility(VisibilityAttribute);

	TSharedPtr<SWidget> NameWidget, ValueWidget;
	Row->GetDefaultWidgets(NameWidget, ValueWidget);
	
	const FSimpleDelegate OnStructContentsChangedDelegate = FSimpleDelegate::CreateLambda([HierarchyPtr, Key, StructToDisplay, InValueType]()
	{
		if(HierarchyPtr.IsValid())
		{
            const FRigControlValue Value = FRigControlValue::Make(*(T*)StructToDisplay->GetStructMemory());
			HierarchyPtr->SetControlValue(Key, Value, InValueType, true);
		}
	});

	TSharedPtr<IPropertyHandle> Handle = Row->GetPropertyHandle();
	Handle->SetOnPropertyValueChanged(OnStructContentsChangedDelegate);
	Handle->SetOnChildPropertyValueChanged(OnStructContentsChangedDelegate);
}

void FRigControlElementDetails_SetupValueWidget(IDetailCategoryBuilder& InCategory, ERigControlValueType InValueType, FRigControlElement* InControlElement, URigHierarchy* InHierarchy)
{
	switch(InControlElement->Settings.ControlType)
	{
		case ERigControlType::Bool:
		{
			if((InValueType == ERigControlValueType::Minimum) || (InValueType == ERigControlValueType::Maximum))
			{
				return;
			}
			FRigControlElementDetails_SetupBoolValueWidget(InCategory, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::Integer:
		{
			FRigControlElementDetails_SetupIntegerValueWidget(InCategory, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::Float:
		{
			FRigControlElementDetails_SetupFloatValueWidget(InCategory, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::Vector2D:
		{
			FRigControlElementDetails_SetupStructValueWidget<FVector2D>(InCategory, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::Position:
		case ERigControlType::Scale:
		{
			FRigControlElementDetails_SetupStructValueWidget<FVector>(InCategory, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::Rotator:
		{
			FRigControlElementDetails_SetupStructValueWidget<FRotator>(InCategory, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::TransformNoScale:
		{
			FRigControlElementDetails_SetupStructValueWidget<FTransformNoScale>(InCategory, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::EulerTransform:
		{
			FRigControlElementDetails_SetupStructValueWidget<FEulerTransform>(InCategory, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::Transform:
		{
			FRigControlElementDetails_SetupStructValueWidget<FTransform>(InCategory, InValueType, InControlElement, InHierarchy);
			break;
		}
		default:
		{
			ensure(false);
			break;
		}
	}
}

TArray<TSharedPtr<FString>> FRigControlElementDetails::ControlTypeList;

void FRigControlElementDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FRigTransformElementDetails::CustomizeDetails(DetailBuilder);

	GizmoNameList.Reset();
	if (BlueprintBeingCustomized)
	{
		if (!BlueprintBeingCustomized->GizmoLibrary.IsValid())
		{
			BlueprintBeingCustomized->GizmoLibrary.LoadSynchronous();
		}
		if (BlueprintBeingCustomized->GizmoLibrary.IsValid())
		{
			GizmoNameList.Add(MakeShared<FString>(BlueprintBeingCustomized->GizmoLibrary->DefaultGizmo.GizmoName.ToString()));
			for (const FControlRigGizmoDefinition& Gizmo : BlueprintBeingCustomized->GizmoLibrary->Gizmos)
			{
				GizmoNameList.Add(MakeShared<FString>(Gizmo.GizmoName.ToString()));
			}
		}
	}

	if (HierarchyBeingCustomized == nullptr || !ElementKeyBeingCustomized)
	{
		return;
	}

	IDetailCategoryBuilder& ControlCategory = DetailBuilder.EditCategory(TEXT("Control"), LOCTEXT("ControlCategory", "Control"));
	IDetailCategoryBuilder& LimitsCategory = DetailBuilder.EditCategory(TEXT("Limits"), LOCTEXT("LimitsCategory", "Limits"));
	IDetailCategoryBuilder& GizmoCategory = DetailBuilder.EditCategory(TEXT("Gizmo"), LOCTEXT("GizmoCategory", "Gizmo"));

	ControlCategory.InitiallyCollapsed(false);
	LimitsCategory.InitiallyCollapsed(false);
	GizmoCategory.InitiallyCollapsed(false);

	const TSharedRef<IPropertyHandle> DisplayNameHandle = DetailBuilder.GetProperty(TEXT("Settings.DisplayName"));
	DetailBuilder.HideProperty(DisplayNameHandle);
	ControlCategory.AddProperty(DisplayNameHandle).CustomWidget()
	.NameContent()
    [
        SNew(STextBlock)
        .Text(FText::FromString(TEXT("Display Name")))
        .Font(IDetailLayoutBuilder::GetDetailFont())
    ]
	.ValueContent()
    [
		SNew(SEditableTextBox)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(this, &FRigControlElementDetails::GetDisplayName)
		.OnTextCommitted(this, &FRigControlElementDetails::SetDisplayName, DetailBuilder.GetPropertyUtilities())
	];

	if (ControlTypeList.Num() == 0)
	{
		UEnum* Enum = StaticEnum<ERigControlType>();
		for (int64 Index = 0; Index < Enum->GetMaxEnumValue(); Index++)
		{
			ControlTypeList.Add(MakeShared<FString>(Enum->GetDisplayNameTextByValue(Index).ToString()));
		}
	}

	FRigControlElement* ControlElement = HierarchyBeingCustomized->FindChecked<FRigControlElement>(ElementKeyBeingCustomized);

	// when control type changes, we have to refresh detail panel
	const TSharedRef<IPropertyHandle> ControlTypeHandle = DetailBuilder.GetProperty(TEXT("Settings.ControlType"));
	ControlTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda(
		[this, &DetailBuilder]()
		{
			DetailBuilder.ForceRefreshDetails();

			if (this->HierarchyBeingCustomized && this->ElementKeyBeingCustomized.IsValid())
			{
				FRigControlElement* ControlElement = this->HierarchyBeingCustomized->FindChecked<FRigControlElement>(ElementKeyBeingCustomized);
				FRigControlValue ValueToSet;

				ControlElement->Settings.bLimitTranslation = false;
				ControlElement->Settings.bLimitRotation = false;
				ControlElement->Settings.bLimitScale = false;

				switch (ControlElement->Settings.ControlType)
				{
					case ERigControlType::Bool:
					{
						ValueToSet = FRigControlValue::Make<bool>(false);
						break;
					}
					case ERigControlType::Float:
					{
						ValueToSet = FRigControlValue::Make<float>(0.f);
						ControlElement->Settings.bLimitTranslation = true;
						ControlElement->Settings.MinimumValue = FRigControlValue::Make<float>(0.f);
						ControlElement->Settings.MaximumValue = FRigControlValue::Make<float>(1.f);
                        break;
					}
					case ERigControlType::Integer:
					{
						ValueToSet = FRigControlValue::Make<int32>(0);
						ControlElement->Settings.bLimitTranslation = true;
						ControlElement->Settings.MinimumValue = FRigControlValue::Make<int32>(0);
						ControlElement->Settings.MaximumValue = FRigControlValue::Make<int32>(10);
						break;
					}
					case ERigControlType::Vector2D:
					{
						ValueToSet = FRigControlValue::Make<FVector2D>(FVector2D::ZeroVector);
						ControlElement->Settings.bLimitTranslation = true;
						ControlElement->Settings.MinimumValue = FRigControlValue::Make<FVector2D>(FVector2D::ZeroVector);
						ControlElement->Settings.MaximumValue = FRigControlValue::Make<FVector2D>(FVector2D(1.f, 1.f));
						break;
					}
					case ERigControlType::Position:
					{
						ValueToSet = FRigControlValue::Make<FVector>(FVector::ZeroVector);
						ControlElement->Settings.MinimumValue = FRigControlValue::Make<FVector>(-FVector::OneVector);
						ControlElement->Settings.MaximumValue = FRigControlValue::Make<FVector>(FVector::OneVector);
						break;
					}
					case ERigControlType::Scale:
					{
						ValueToSet = FRigControlValue::Make<FVector>(FVector::OneVector);
						ControlElement->Settings.MinimumValue = FRigControlValue::Make<FVector>(FVector::ZeroVector);
						ControlElement->Settings.MaximumValue = FRigControlValue::Make<FVector>(FVector::OneVector);
						break;
					}
					case ERigControlType::Rotator:
					{
						ValueToSet = FRigControlValue::Make<FRotator>(FRotator::ZeroRotator);
						ControlElement->Settings.MinimumValue = FRigControlValue::Make<FRotator>(FRotator::ZeroRotator);
						ControlElement->Settings.MaximumValue = FRigControlValue::Make<FRotator>(FRotator(180.f, 180.f, 180.f));
						break;
					}
					case ERigControlType::Transform:
					{
						ValueToSet = FRigControlValue::Make<FTransform>(FTransform::Identity);
						ControlElement->Settings.MinimumValue = ValueToSet;
						ControlElement->Settings.MaximumValue = ValueToSet;
						break;
					}
					case ERigControlType::TransformNoScale:
					{
						FTransformNoScale Identity = FTransform::Identity;
						ValueToSet = FRigControlValue::Make<FTransformNoScale>(Identity);
						ControlElement->Settings.MinimumValue = ValueToSet;
                        ControlElement->Settings.MaximumValue = ValueToSet;
                        break;
					}
					case ERigControlType::EulerTransform:
					{
						FEulerTransform Identity = FEulerTransform::Identity;
						ValueToSet = FRigControlValue::Make<FEulerTransform>(Identity);
						ControlElement->Settings.MinimumValue = ValueToSet;
						ControlElement->Settings.MaximumValue = ValueToSet;
						break;
					}
					default:
					{
						ensure(false);
						break;
					}
				}

				this->HierarchyBeingCustomized->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);
				this->HierarchyBeingCustomized->SetControlValue(ControlElement, ValueToSet, ERigControlValueType::Initial, true);
				this->HierarchyBeingCustomized->SetControlValue(ControlElement, ValueToSet, ERigControlValueType::Current, true);

				if (this->HierarchyBeingCustomized != this->BlueprintBeingCustomized->Hierarchy)
				{
					if(FRigControlElement* OtherControlElement = this->BlueprintBeingCustomized->Hierarchy->Find<FRigControlElement>(ControlElement->GetKey()))
					{
						OtherControlElement->Settings = ControlElement->Settings;
						this->BlueprintBeingCustomized->Hierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);
                        this->BlueprintBeingCustomized->Hierarchy->SetControlValue(ControlElement, ValueToSet, ERigControlValueType::Initial, true);
                        this->BlueprintBeingCustomized->Hierarchy->SetControlValue(ControlElement, ValueToSet, ERigControlValueType::Current, true);
                   }
				}
			}
		}
	));

	struct Local
	{
		static void OnTransformChanged(FEditPropertyChain* InPropertyChain, UControlRigBlueprint* InBlueprint)
		{
			if(InBlueprint && InPropertyChain)
			{
				if(InPropertyChain->Num() > 1)
				{
					FPropertyChangedEvent ChangeEvent(InPropertyChain->GetHead()->GetValue(), EPropertyChangeType::ValueSet);
					ChangeEvent.SetActiveMemberProperty(InPropertyChain->GetTail()->GetValue());
					FPropertyChangedChainEvent ChainEvent(*InPropertyChain, ChangeEvent);
					InBlueprint->BroadcastPostEditChangeChainProperty(ChainEvent);
				}
			}
		}
	};

	if(ControlElement->Settings.ControlType == ERigControlType::Bool)
	{
		DetailBuilder.HideCategory(TEXT("Pose"));
		DetailBuilder.HideProperty(TEXT("Offset"));
		DetailBuilder.HideProperty(TEXT("Gizmo"));
	}
	else
	{
		// setup offset transform
		{
			const TSharedRef<IPropertyHandle> OffsetHandle = DetailBuilder.GetProperty(TEXT("Offset"));
			const TSharedRef<IPropertyHandle> OffsetInitialLocalTransformHandle = DetailBuilder.GetProperty(TEXT("Offset.Initial.Local.Transform"));
			ControlCategory.AddProperty(OffsetInitialLocalTransformHandle).DisplayName(FText::FromString(TEXT("Offset Transform")));
			DetailBuilder.HideProperty(TEXT("Offset"));

			OffsetPropertyChain.AddHead(FRigControlElement::StaticStruct()->FindPropertyByName(TEXT("Offset")));
			OffsetPropertyChain.AddTail(FRigCurrentAndInitialTransform::StaticStruct()->FindPropertyByName(TEXT("Initial")));
			OffsetPropertyChain.AddTail(FRigLocalAndGlobalTransform::StaticStruct()->FindPropertyByName(TEXT("Local")));
			OffsetPropertyChain.AddTail(FRigComputedTransform::StaticStruct()->FindPropertyByName(TEXT("Transform")));
			OffsetPropertyChain.SetActiveMemberPropertyNode(OffsetPropertyChain.GetTail()->GetValue());

			const FSimpleDelegate OnTransformChangedDelegate = FSimpleDelegate::CreateStatic(&Local::OnTransformChanged, &OffsetPropertyChain, BlueprintBeingCustomized);
			OffsetInitialLocalTransformHandle->SetOnPropertyValueChanged(OnTransformChangedDelegate);
			OffsetInitialLocalTransformHandle->SetOnChildPropertyValueChanged(OnTransformChangedDelegate);
		}

		// setup gizmo transform
		{
			const TSharedRef<IPropertyHandle> GizmoHandle = DetailBuilder.GetProperty(TEXT("Gizmo"));
			const TSharedRef<IPropertyHandle> GizmoInitialLocalTransformHandle = DetailBuilder.GetProperty(TEXT("Gizmo.Initial.Local.Transform"));
			GizmoCategory.AddProperty(GizmoInitialLocalTransformHandle).DisplayName(FText::FromString(TEXT("Gizmo Transform")));
			DetailBuilder.HideProperty(TEXT("Gizmo"));

			GizmoPropertyChain.AddHead(FRigControlElement::StaticStruct()->FindPropertyByName(TEXT("Gizmo")));
			GizmoPropertyChain.AddTail(FRigCurrentAndInitialTransform::StaticStruct()->FindPropertyByName(TEXT("Initial")));
			GizmoPropertyChain.AddTail(FRigLocalAndGlobalTransform::StaticStruct()->FindPropertyByName(TEXT("Local")));
			GizmoPropertyChain.AddTail(FRigComputedTransform::StaticStruct()->FindPropertyByName(TEXT("Transform")));
			GizmoPropertyChain.SetActiveMemberPropertyNode(GizmoPropertyChain.GetTail()->GetValue());

			const FSimpleDelegate OnTransformChangedDelegate = FSimpleDelegate::CreateStatic(&Local::OnTransformChanged, &GizmoPropertyChain, BlueprintBeingCustomized);
			GizmoInitialLocalTransformHandle->SetOnPropertyValueChanged(OnTransformChangedDelegate);
			GizmoInitialLocalTransformHandle->SetOnChildPropertyValueChanged(OnTransformChangedDelegate);
		}
	}

	FRigControlElementDetails_SetupValueWidget(ControlCategory, ERigControlValueType::Current, ControlElement, HierarchyBeingCustomized);
	FRigControlElementDetails_SetupValueWidget(LimitsCategory, ERigControlValueType::Minimum, ControlElement, HierarchyBeingCustomized);
	FRigControlElementDetails_SetupValueWidget(LimitsCategory, ERigControlValueType::Maximum, ControlElement, HierarchyBeingCustomized);

	switch (ControlElement->Settings.ControlType)
	{
		case ERigControlType::Float:
		case ERigControlType::Integer:
		case ERigControlType::Vector2D:
		case ERigControlType::Position:
		case ERigControlType::Scale:
		case ERigControlType::Rotator:
		case ERigControlType::Transform:
		case ERigControlType::TransformNoScale:
		case ERigControlType::EulerTransform:
		{
			const TSharedRef<IPropertyHandle> GizmoNameHandle = DetailBuilder.GetProperty(TEXT("Settings.GizmoName"));
			DetailBuilder.HideProperty(GizmoNameHandle);
			GizmoCategory.AddProperty(GizmoNameHandle).CustomWidget()
			.NameContent()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Gizmo Name")))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.IsEnabled(this, &FRigControlElementDetails::IsGizmoEnabled)
			]
			.ValueContent()
			[
				SNew(SControlRigGizmoNameList, ControlElement, BlueprintBeingCustomized)
				.OnGetNameListContent(this, &FRigControlElementDetails::GetGizmoNameList)
				.IsEnabled(this, &FRigControlElementDetails::IsGizmoEnabled)
			];
			break;
		}
		default:
		{
			DetailBuilder.HideProperty(TEXT("Settings.bGizmoEnabled"));
			DetailBuilder.HideProperty(TEXT("Settings.bGizmoVisible"));
			DetailBuilder.HideProperty(TEXT("Settings.GizmoName"));
			DetailBuilder.HideProperty(TEXT("Settings.GizmoColor"));
			break;
		}
	}

	if(ControlElement->Settings.ControlType != ERigControlType::Integer)
	{
		DetailBuilder.HideProperty(TEXT("Settings.ControlEnum"));
	}

	bool bShowLimitTranslation = false;
	bool bShowLimitRotation = false;
	bool bShowLimitScale = false;

	switch (ControlElement->Settings.ControlType)
	{
		case ERigControlType::Float:
    	case ERigControlType::Integer:
    	case ERigControlType::Vector2D:
    	case ERigControlType::Position:
    	case ERigControlType::Transform:
    	case ERigControlType::TransformNoScale:
    	case ERigControlType::EulerTransform:
		{
				bShowLimitTranslation = true;
			break;
		}
		default:
		{
			break;
		}
	}

	switch (ControlElement->Settings.ControlType)
	{
    	case ERigControlType::Rotator:
    	case ERigControlType::Transform:
    	case ERigControlType::TransformNoScale:
    	case ERigControlType::EulerTransform:
		{
			bShowLimitRotation = true;
			break;
		}
		default:
		{
			break;
		}
	}

	switch (ControlElement->Settings.ControlType)
	{
		case ERigControlType::Scale:
    	case ERigControlType::Transform:
    	case ERigControlType::EulerTransform:
		{
			bShowLimitScale = true;
			break;
		}
		default:
		{
			break;
		}
	}

	if(!bShowLimitTranslation)
	{
		DetailBuilder.HideProperty(TEXT("Settings.bLimitTranslation"));
	}
	if(!bShowLimitRotation)
	{
		DetailBuilder.HideProperty(TEXT("Settings.bLimitRotation"));
	}
	if(!bShowLimitScale)
	{
		DetailBuilder.HideProperty(TEXT("Settings.bLimitScale"));
	}

	switch (ControlElement->Settings.ControlType)
	{
		case ERigControlType::Integer:
		case ERigControlType::Float:
		case ERigControlType::Vector2D:
		{
			break;
		}
		default:
		{
			DetailBuilder.HideProperty(TEXT("Settings.PrimaryAxis"));
			break;
		}
	}

	if (ControlElement->Settings.ControlType == ERigControlType::Integer)
	{
		TSharedRef<IPropertyHandle> ControlEnum = DetailBuilder.GetProperty(TEXT("Settings.ControlEnum"));
		ControlEnum->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda(
			[this, &DetailBuilder]()
			{
				DetailBuilder.ForceRefreshDetails();

				if (this->HierarchyBeingCustomized != nullptr && this->ElementKeyBeingCustomized)
				{
					if(FRigControlElement* ControlBeingCustomized = this->HierarchyBeingCustomized->Find<FRigControlElement>(ElementKeyBeingCustomized))
					{
						const UEnum* ControlEnum = ControlBeingCustomized->Settings.ControlEnum;
						if (ControlEnum != nullptr)
						{
							int32 Maximum = (int32)ControlEnum->GetMaxEnumValue() - 1;
							ControlBeingCustomized->Settings.MinimumValue.Set<int32>(0);
							ControlBeingCustomized->Settings.MaximumValue.Set<int32>(Maximum);
							HierarchyBeingCustomized->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlBeingCustomized);

							FRigControlValue InitialValue = HierarchyBeingCustomized->GetControlValue(ControlBeingCustomized, ERigControlValueType::Initial);
							FRigControlValue CurrentValue = HierarchyBeingCustomized->GetControlValue(ControlBeingCustomized, ERigControlValueType::Current);

							ControlBeingCustomized->Settings.ApplyLimits(InitialValue);
							ControlBeingCustomized->Settings.ApplyLimits(CurrentValue);
							HierarchyBeingCustomized->SetControlValue(ControlBeingCustomized, InitialValue, ERigControlValueType::Initial);
							HierarchyBeingCustomized->SetControlValue(ControlBeingCustomized, CurrentValue, ERigControlValueType::Current);

							if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
							{
								URigHierarchy* DebuggedHierarchy = DebuggedRig->GetHierarchy();
								if(FRigControlElement* DebuggedControlElement = DebuggedHierarchy->Find<FRigControlElement>(ElementKeyBeingCustomized))
								{
									DebuggedControlElement->Settings.MinimumValue.Set<int32>(0);
                                    DebuggedControlElement->Settings.MaximumValue.Set<int32>(Maximum);
                                    DebuggedHierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, DebuggedControlElement);

                                    DebuggedHierarchy->SetControlValue(DebuggedControlElement, InitialValue, ERigControlValueType::Initial);
                                    DebuggedHierarchy->SetControlValue(DebuggedControlElement, CurrentValue, ERigControlValueType::Current);
								}
							}
						}
					}
				}
			}
		));
	}
}

FText FRigControlElementDetails::GetDisplayName() const
{
	if (HierarchyBeingCustomized != nullptr && ElementKeyBeingCustomized)
	{
		if(FRigControlElement* ControlElement = HierarchyBeingCustomized->Find<FRigControlElement>(ElementKeyBeingCustomized))
		{
			if (ControlElement->Settings.DisplayName.IsNone())
			{
				return FText();
			}
			return FText::FromName(ControlElement->GetDisplayName());
		}
	}
	return FText();
}

void FRigControlElementDetails::SetDisplayName(const FText& InNewText, ETextCommit::Type InCommitType, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	if (HierarchyBeingCustomized != nullptr && ElementKeyBeingCustomized)
	{
		if(FRigControlElement* ControlElement = HierarchyBeingCustomized->Find<FRigControlElement>(ElementKeyBeingCustomized))
		{
			if(BlueprintBeingCustomized)
			{
				BlueprintBeingCustomized->Hierarchy->Modify();
			}

			const FString NewDisplayName = InNewText.ToString().TrimStartAndEnd();
			if (NewDisplayName.IsEmpty())
			{
				ControlElement->Settings.DisplayName = FName(NAME_None);
			}
			else
			{
				ControlElement->Settings.DisplayName = *NewDisplayName;
			}

			HierarchyBeingCustomized->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);

			if (BlueprintBeingCustomized && BlueprintBeingCustomized->Hierarchy != HierarchyBeingCustomized)
			{
				if(FRigControlElement* OtherControlElement = BlueprintBeingCustomized->Hierarchy->Find<FRigControlElement>(ElementKeyBeingCustomized))
				{
					OtherControlElement->Settings.DisplayName = ControlElement->Settings.DisplayName;
					BlueprintBeingCustomized->Hierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, OtherControlElement);
				}
			}
		}
	}
}

bool FRigControlElementDetails::IsGizmoEnabled() const
{
	URigHierarchy* Hierarchy = HierarchyBeingCustomized;
	if (Hierarchy != nullptr && ElementKeyBeingCustomized)
	{
		if(FRigControlElement* ControlElement = Hierarchy->Find<FRigControlElement>(ElementKeyBeingCustomized))
		{
			return ControlElement->Settings.bGizmoEnabled;
		}
	}
	return false;
}

bool FRigControlElementDetails::IsEnabled(ERigControlValueType InValueType) const
{
	switch (InValueType)
	{
		case ERigControlValueType::Minimum:
		case ERigControlValueType::Maximum:
		{
			if (HierarchyBeingCustomized != nullptr && ElementKeyBeingCustomized)
			{
				if(FRigControlElement* ControlElement = HierarchyBeingCustomized->Find<FRigControlElement>(ElementKeyBeingCustomized))
				{
					return ControlElement->Settings.bLimitTranslation || ControlElement->Settings.bLimitRotation || ControlElement->Settings.bLimitScale;
				}
			}
			return false;
		}
		default:
		{
			break;
		}
	}
	return true;
}

const TArray<TSharedPtr<FString>>& FRigControlElementDetails::GetGizmoNameList() const
{
	return GizmoNameList;
}

const TArray<TSharedPtr<FString>>& FRigControlElementDetails::GetControlTypeList() const
{
	return ControlTypeList;
}

void FRigSpaceElementDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FRigTransformElementDetails::CustomizeDetails(DetailBuilder);
}

#undef LOCTEXT_NAMESPACE
