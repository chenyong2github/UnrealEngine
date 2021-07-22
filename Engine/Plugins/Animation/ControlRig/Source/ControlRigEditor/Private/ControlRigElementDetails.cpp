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
#include "Widgets/Input/SButton.h"
#include "Editor/SControlRigGizmoNameList.h"
#include "ControlRigBlueprint.h"
#include "IDetailGroup.h"
#include "Graph/ControlRigGraph.h"
#include "PropertyCustomizationHelpers.h"
#include "SEnumCombo.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "Graph/SControlRigGraphPinVariableBinding.h"

#define LOCTEXT_NAMESPACE "ControlRigElementDetails"

static const FText ControlRigDetailsMultipleValues = LOCTEXT("MultipleValues", "Multiple Values");

namespace FRigElementKeyDetailsDefs
{
	// Active foreground pin alpha
	static const float ActivePinForegroundAlpha = 1.f;
	// InActive foreground pin alpha
	static const float InactivePinForegroundAlpha = 0.15f;
	// Active background pin alpha
	static const float ActivePinBackgroundAlpha = 0.8f;
	// InActive background pin alpha
	static const float InactivePinBackgroundAlpha = 0.4f;
};

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
					UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(Rig->GetClass()->ClassGeneratedBy);
					if (Blueprint)
					{
						if(Blueprint->GetOutermost() == Package)
						{
							OutBlueprint = Blueprint;
							break;
						}
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

UControlRigBlueprint* RigElementDetails_GetBlueprintFromHierarchy(URigHierarchy* InHierarchy)
{
	if(InHierarchy == nullptr)
	{
		return nullptr;
	}

	UControlRigBlueprint* Blueprint = InHierarchy->GetTypedOuter<UControlRigBlueprint>();
	if(Blueprint == nullptr)
	{
		UControlRig* Rig = InHierarchy->GetTypedOuter<UControlRig>();
		if(Rig)
		{
			Blueprint = Cast<UControlRigBlueprint>(Rig->GetClass()->ClassGeneratedBy);
        }
	}
	return Blueprint;
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
				SAssignNew(SearchableComboBox, SSearchableComboBox)
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
			// Use button
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(1,0)
			.VAlign(VAlign_Center)
			[
				SAssignNew(UseSelectedButton, SButton)
				.ButtonStyle( FEditorStyle::Get(), "NoBorder" )
				.ButtonColorAndOpacity_Lambda([this]() { return OnGetWidgetBackground(UseSelectedButton); })
				.OnClicked(this, &FRigElementKeyDetails::OnGetSelectedClicked)
				.ContentPadding(1.f)
				.ToolTipText(NSLOCTEXT("GraphEditor", "ObjectGraphPin_Use_Tooltip", "Use item selected"))
				[
					SNew(SImage)
					.ColorAndOpacity_Lambda( [this]() { return OnGetWidgetForeground(UseSelectedButton); })
					.Image(FEditorStyle::GetBrush("Icons.CircleArrowLeft"))
				]
			]
			// Select in hierarchy button
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(1,0)
			.VAlign(VAlign_Center)
			[
				SAssignNew(SelectElementButton, SButton)
				.ButtonStyle( FEditorStyle::Get(), "NoBorder" )
				.ButtonColorAndOpacity_Lambda([this]() { return OnGetWidgetBackground(SelectElementButton); })
				.OnClicked(this, &FRigElementKeyDetails::OnSelectInHierarchyClicked)
				.ContentPadding(0)
				.ToolTipText(NSLOCTEXT("GraphEditor", "ObjectGraphPin_Browse_Tooltip", "Select in hierarchy"))
				[
					SNew(SImage)
					.ColorAndOpacity_Lambda( [this]() { return OnGetWidgetForeground(SelectElementButton); })
					.Image(FEditorStyle::GetBrush("Icons.Search"))
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
		for(int32 ObjectIndex = 0; ObjectIndex < NameHandle->GetNumPerObjectValues(); ObjectIndex++)
		{
			FString PerObjectValue;
			NameHandle->GetPerObjectValue(ObjectIndex, PerObjectValue);

			if(ObjectIndex == 0)
			{
				ElementNameStr = PerObjectValue;
			}
			else if(ElementNameStr != PerObjectValue)
			{
				return ControlRigDetailsMultipleValues.ToString();
			}
		}
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
				ElementNameList = *RigGraph->GetElementNameList(GetElementType());
				if(SearchableComboBox.IsValid())
				{
					SearchableComboBox->RefreshOptions();
				}
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

FSlateColor FRigElementKeyDetails::OnGetWidgetForeground(const TSharedPtr<SButton> Button) const
{
	float Alpha = (Button.IsValid() && Button->IsHovered()) ? FRigElementKeyDetailsDefs::ActivePinForegroundAlpha : FRigElementKeyDetailsDefs::InactivePinForegroundAlpha;
	return FSlateColor(FLinearColor(1.f, 1.f, 1.f, Alpha));
}

FSlateColor FRigElementKeyDetails::OnGetWidgetBackground(const TSharedPtr<SButton> Button) const
{
	float Alpha = (Button.IsValid() && Button->IsHovered()) ? FRigElementKeyDetailsDefs::ActivePinBackgroundAlpha : FRigElementKeyDetailsDefs::InactivePinBackgroundAlpha;
	return FSlateColor(FLinearColor(1.f, 1.f, 1.f, Alpha));
}

FReply FRigElementKeyDetails::OnGetSelectedClicked()
{
	if (BlueprintBeingCustomized)
	{
		const TArray<FRigElementKey>& Selected = BlueprintBeingCustomized->Hierarchy->GetSelectedKeys();
		if (Selected.Num() > 0)
		{
			if (TypeHandle.IsValid())
			{
				uint8 Index = (uint8) Selected[0].Type;
				TypeHandle->SetValue(Index);
			}
			SetElementName(Selected[0].Name.ToString());
		}
	}
	return FReply::Handled();
}

FReply FRigElementKeyDetails::OnSelectInHierarchyClicked()
{
	if (BlueprintBeingCustomized)
	{
		FRigElementKey Key;
		if (TypeHandle.IsValid())
		{
			uint8 Type;
			TypeHandle->GetValue(Type);
			Key.Type = (ERigElementType) Type;
		}

		if (NameHandle.IsValid())
		{
			NameHandle->GetValue(Key.Name);
		}
				
		if (Key.IsValid())
		{
			BlueprintBeingCustomized->GetHierarchyController()->SetSelection({Key});
		}	
	}
	return FReply::Handled();
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

	URigVMGraph* Model = GraphBeingCustomized->GetModel();
	if(Model == nullptr)
	{
		return;
	}

	const TArray<FName> SelectedNodeNames = Model->GetSelectNodes();
	if(SelectedNodeNames.Num() == 0)
	{
		return;
	}

	URigVMNode* ModelNode = Model->FindNodeByName(SelectedNodeNames[0]);
	if(ModelNode == nullptr)
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

		URigVMPin* ModelPin = ModelNode->FindPin(Property->GetName());
		if(ModelPin == nullptr)
		{
			continue;
		}

		if(ModelPin->IsBoundToVariable())
		{
			CategoryBuilder.AddCustomRow(FText::FromString(Property->GetName()))
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				SNew(SControlRigVariableBinding)
					.ModelPin(ModelPin)
					.Blueprint(BlueprintBeingCustomized)
			];

			continue;
		}

		if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			FString CustomWidgetName = NameProperty->GetMetaData(TEXT("CustomWidget"));
			if (!CustomWidgetName.IsEmpty())
			{
				const TArray<TSharedPtr<FString>>* NameList = nullptr;
				if (CustomWidgetName == TEXT("BoneName"))
				{
					NameList = GraphBeingCustomized->GetBoneNameList();
				}
				else if (CustomWidgetName == TEXT("ControlName"))
				{
					NameList = GraphBeingCustomized->GetControlNameList();
				}
				else if (CustomWidgetName == TEXT("SpaceName"))
				{
					NameList = GraphBeingCustomized->GetNullNameList();
				}
				else if (CustomWidgetName == TEXT("CurveName"))
				{
					NameList = GraphBeingCustomized->GetCurveNameList();
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
		else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			const FSimpleDelegate OnStructContentsChangedDelegate = FSimpleDelegate::CreateSP(this, &FRigUnitDetails::OnStructContentsChanged, Property, DetailBuilder.GetPropertyUtilities());
			PropertyHandle->SetOnPropertyValueChanged(OnStructContentsChangedDelegate);
			PropertyHandle->SetOnChildPropertyValueChanged(OnStructContentsChangedDelegate);
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

void FRigUnitDetails::OnStructContentsChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	const FPropertyChangedEvent ChangeEvent(InProperty, EPropertyChangeType::ValueSet);
	PropertyUtilities->NotifyFinishedChangingProperties(ChangeEvent);
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

void FRigBaseElementDetails::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	BlueprintBeingCustomized = nullptr;
	HierarchyBeingCustomized = nullptr;
	ObjectsBeingCustomized.Reset();
	
	TArray<UObject*> Objects;
	InStructPropertyHandle->GetOuterObjects(Objects);
	for (UObject* Object : Objects)
	{
		UDetailsViewWrapperObject* WrapperObject = CastChecked<UDetailsViewWrapperObject>(Object);

		if(HierarchyBeingCustomized == nullptr)
		{
			HierarchyBeingCustomized = Cast<URigHierarchy>(WrapperObject->GetOuter());
		}

		ObjectsBeingCustomized.Add(WrapperObject);
	}

	if(HierarchyBeingCustomized)
	{
		BlueprintBeingCustomized = HierarchyBeingCustomized->GetTypedOuter<UControlRigBlueprint>();
		if(BlueprintBeingCustomized == nullptr)
		{
			if(UControlRig* ControlRig = HierarchyBeingCustomized->GetTypedOuter<UControlRig>())
			{
				BlueprintBeingCustomized = Cast<UControlRigBlueprint>(ControlRig->GetClass()->ClassGeneratedBy);
			}
		}
	}

	if(BlueprintBeingCustomized == nullptr)
	{	
		RigElementDetails_GetCustomizedInfo(InStructPropertyHandle, BlueprintBeingCustomized);
	}
}

void FRigBaseElementDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructBuilder.AddCustomRow(FText::FromString(TEXT("Name")))
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
		.IsEnabled(ObjectsBeingCustomized.Num() == 1)
	];
}

FRigElementKey FRigBaseElementDetails::GetElementKey() const
{
	check(ObjectsBeingCustomized.Num() == 1);
	if(ObjectsBeingCustomized[0].IsValid())
	{
		return ObjectsBeingCustomized[0]->GetContent<FRigBaseElement>()->GetKey(); 
	}
	return FRigElementKey();
}

FText FRigBaseElementDetails::GetName() const
{
	if(ObjectsBeingCustomized.Num() > 1)
	{
		return ControlRigDetailsMultipleValues;
	}
	return FText::FromName(GetElementKey().Name);
}

void FRigBaseElementDetails::SetName(const FText& InNewText, ETextCommit::Type InCommitType)
{
	if(ObjectsBeingCustomized.Num() > 1)
	{
		return;
	}
	
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
		Controller->RenameElement(GetElementKey(), *InNewText.ToString(), true, true);
	}
}

void FRigBaseElementDetails::OnStructContentsChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	const FPropertyChangedEvent ChangeEvent(InProperty, EPropertyChangeType::ValueSet);
	PropertyUtilities->NotifyFinishedChangingProperties(ChangeEvent);
}

bool FRigBaseElementDetails::IsSetupModeEnabled() const
{
	if(BlueprintBeingCustomized)
	{
		if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
		{
			return DebuggedRig->IsSetupModeEnabled();
		}
	}
	return false;
}

TArray<FRigElementKey> FRigBaseElementDetails::GetElementKeys() const
{
	TArray<FRigElementKey> Keys;
	for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(ObjectBeingCustomized.IsValid())
		{
			Keys.Add(ObjectBeingCustomized->GetContent<FRigBaseElement>()->GetKey());
		}
	}
	return Keys;
}

bool FRigBaseElementDetails::IsAnyControlOfType(ERigControlType InType) const
{
	for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(ObjectBeingCustomized.IsValid())
		{
			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(ObjectBeingCustomized->GetContent<FRigBaseElement>()))
			{
				if(ControlElement->Settings.ControlType == InType)
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool FRigBaseElementDetails::IsAnyControlNotOfType(ERigControlType InType) const
{
	for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(ObjectBeingCustomized.IsValid())
		{
			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(ObjectBeingCustomized->GetContent<FRigBaseElement>()))
			{
				if(ControlElement->Settings.ControlType != InType)
				{
					return true;
				}
			}
		}
	}
	return false;
}

void FRigTransformElementDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FRigBaseElementDetails::CustomizeChildren(InStructPropertyHandle, StructBuilder, StructCustomizationUtils);

	IDetailGroup& PoseGroupDefault = StructBuilder.AddGroup(TEXT("Pose"), LOCTEXT("Pose", "Pose")); 
	IDetailGroup& PoseGroupAdvanced = StructBuilder.GetParentCategory().AddGroup(TEXT("Pose"), LOCTEXT("Pose", "Pose"), true, false); 
	
	{
		const TSharedPtr<IPropertyHandle> PoseHandle = InStructPropertyHandle->GetChildHandle(TEXT("Pose"));
		const TSharedPtr<IPropertyHandle> InitialHandle = PoseHandle->GetChildHandle(TEXT("Initial"));
		const TSharedPtr<IPropertyHandle> CurrentHandle = PoseHandle->GetChildHandle(TEXT("Current"));

		// setup initial global
		{
			const TSharedPtr<IPropertyHandle> GlobalHandle = InitialHandle->GetChildHandle(TEXT("Global"));
			const TSharedPtr<IPropertyHandle> TransformHandle = GlobalHandle->GetChildHandle(TEXT("Transform"));

			PoseGroupAdvanced.AddPropertyRow(TransformHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Initial Global")))
			.IsEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FRigBaseElementDetails::IsSetupModeEnabled)));
		}

		// setup initial local
		{
			const TSharedPtr<IPropertyHandle> LocalHandle = InitialHandle->GetChildHandle(TEXT("Local"));
			const TSharedPtr<IPropertyHandle> TransformHandle = LocalHandle->GetChildHandle(TEXT("Transform"));

			PoseGroupAdvanced.AddPropertyRow(TransformHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Initial Local")))
			.IsEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FRigBaseElementDetails::IsSetupModeEnabled)));
		}

		// setup current global
		{
			const TSharedPtr<IPropertyHandle> GlobalHandle = CurrentHandle->GetChildHandle(TEXT("Global"));
			const TSharedPtr<IPropertyHandle> TransformHandle = GlobalHandle->GetChildHandle(TEXT("Transform"));

			PoseGroupAdvanced.AddPropertyRow(TransformHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Current Global")))
			.IsEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FRigBaseElementDetails::IsSetupModeEnabled)));
		}

		// setup current local
		{
			const TSharedPtr<IPropertyHandle> LocalHandle = CurrentHandle->GetChildHandle(TEXT("Local"));
			const TSharedPtr<IPropertyHandle> TransformHandle = LocalHandle->GetChildHandle(TEXT("Transform"));

			PoseGroupDefault.AddPropertyRow(TransformHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Current Local")))
			.IsEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FRigTransformElementDetails::IsCurrentLocalEnabled)));
		}
	}
}

bool FRigTransformElementDetails::IsCurrentLocalEnabled() const
{
	for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(ObjectBeingCustomized.IsValid())
		{
			if(ObjectBeingCustomized->GetContent<FRigBaseElement>()->GetType() == ERigElementType::Control)
			{
				return false;
			}
		}
	}
	return true;
}

void FRigControlElementDetails_SetupBoolValueWidget(IDetailGroup& InGroup, IDetailChildrenBuilder& InStructBuilder, ERigControlValueType InValueType, FRigControlElement* InControlElement, URigHierarchy* InHierarchy)
{
	UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
	UEnum* ValueTypeEnum = StaticEnum<ERigControlValueType>();

	const FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue((int64)InValueType).ToString();
	const FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s Value"), *ValueTypeName));
	TWeakObjectPtr<URigHierarchy> HierarchyPtr = InHierarchy;
	const FRigElementKey Key = InControlElement->GetKey();

	InStructBuilder.AddCustomRow(PropertyLabel)
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
	        			if(InValueType == ERigControlValueType::Initial)
	        			{
                            if(UControlRigBlueprint* Blueprint = RigElementDetails_GetBlueprintFromHierarchy(HierarchyPtr.Get()))
                            {
                                Blueprint->Hierarchy->SetControlValue(ControlElement->GetKey(), Value, InValueType, true);
                            }
                        }
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

void FRigControlElementDetails_SetupIntegerValueWidget(IDetailGroup& InGroup, IDetailChildrenBuilder& InStructBuilder, ERigControlValueType InValueType, FRigControlElement* InControlElement, URigHierarchy* InHierarchy)
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
		InStructBuilder.AddCustomRow(PropertyLabel)
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
                			if(InValueType == ERigControlValueType::Initial)
                			{
                                if(UControlRigBlueprint* Blueprint = RigElementDetails_GetBlueprintFromHierarchy(HierarchyPtr.Get()))
                                {
                                    Blueprint->Hierarchy->SetControlValue(ControlElement->GetKey(), Value, InValueType, true);
                                }
                            }
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
		InStructBuilder.AddCustomRow(PropertyLabel)
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
                .AllowSpin(InValueType == ERigControlValueType::Current || InValueType == ERigControlValueType::Initial)
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
                				if(InValueType == ERigControlValueType::Initial)
                				{
                                    if(UControlRigBlueprint* Blueprint = RigElementDetails_GetBlueprintFromHierarchy(HierarchyPtr.Get()))
                                    {
                                        Blueprint->Hierarchy->SetControlValue(ControlElement->GetKey(), Value, InValueType, true);
                                    }
                                }
                            }
                		}
                	}
                })
            ]
        ]
		.IsEnabled(EnabledAttribute);
	}
}

void FRigControlElementDetails_SetupFloatValueWidget(IDetailGroup& InGroup, IDetailChildrenBuilder& InStructBuilder, ERigControlValueType InValueType, FRigControlElement* InControlElement, URigHierarchy* InHierarchy)
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

	InStructBuilder.AddCustomRow(PropertyLabel)
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
            .AllowSpin(InValueType == ERigControlValueType::Current || InValueType == ERigControlValueType::Initial)
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
            				if(InValueType == ERigControlValueType::Initial)
            				{
            					if(UControlRigBlueprint* Blueprint = RigElementDetails_GetBlueprintFromHierarchy(HierarchyPtr.Get()))
            					{
            						Blueprint->Hierarchy->SetControlValue(ControlElement->GetKey(), Value, InValueType, true);
            					}
            				}
                        }
            		}
            	}
            })
        ]
    ]
	.IsEnabled(EnabledAttribute);
}

template<typename T>
void FRigControlElementDetails_SetupStructValueWidget(IDetailGroup& InGroup, IDetailChildrenBuilder& InStructBuilder, ERigControlValueType InValueType, FRigControlElement* InControlElement, URigHierarchy* InHierarchy)
{
	UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
	UEnum* ValueTypeEnum = StaticEnum<ERigControlValueType>();

	const FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue((int64)InValueType).ToString();
	const FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s Value"), *ValueTypeName));
	const UStruct* ValueStruct = TBaseStructure<T>::Get();

	const TSharedPtr<FStructOnScope> StructToDisplay = MakeShareable(new FStructOnScope(ValueStruct));

	TWeakObjectPtr<URigHierarchy> HierarchyPtr = InHierarchy;
	const FRigElementKey Key = InControlElement->GetKey();

	const TAttribute<bool> EnabledAttribute = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([HierarchyPtr, Key, InValueType, StructToDisplay, ValueStruct]()->bool
    {
        if(HierarchyPtr.IsValid())
        {
            if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(Key))
            {
            	// update the struct with the current control value
            	uint8* StructMemory = StructToDisplay->GetStructMemory();
            	const FRigControlValue& CurrentValue = HierarchyPtr->GetControlValue(Key, InValueType);            	
            	FMemory::Memcpy(StructToDisplay->GetStructMemory(), &CurrentValue.GetRef<T>(), sizeof(T));

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

	IDetailPropertyRow* Row = InStructBuilder.AddExternalStructure(StructToDisplay.ToSharedRef());
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
			if(InValueType == ERigControlValueType::Initial)
			{
                if(UControlRigBlueprint* Blueprint = RigElementDetails_GetBlueprintFromHierarchy(HierarchyPtr.Get()))
                {
                    Blueprint->Hierarchy->SetControlValue(Key, Value, InValueType, true);
                }
            }
		}
	});

	TSharedPtr<IPropertyHandle> Handle = Row->GetPropertyHandle();
	Handle->SetOnPropertyValueChanged(OnStructContentsChangedDelegate);
	Handle->SetOnChildPropertyValueChanged(OnStructContentsChangedDelegate);
}

void FRigControlElementDetails_SetupValueWidget(IDetailGroup& InGroup, IDetailChildrenBuilder& InStructBuilder, ERigControlValueType InValueType, FRigControlElement* InControlElement, URigHierarchy* InHierarchy)
{
	switch(InControlElement->Settings.ControlType)
	{
		case ERigControlType::Bool:
		{
			if((InValueType == ERigControlValueType::Minimum) || (InValueType == ERigControlValueType::Maximum))
			{
				return;
			}
			FRigControlElementDetails_SetupBoolValueWidget(InGroup, InStructBuilder, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::Integer:
		{
			FRigControlElementDetails_SetupIntegerValueWidget(InGroup, InStructBuilder, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::Float:
		{
			FRigControlElementDetails_SetupFloatValueWidget(InGroup, InStructBuilder, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::Vector2D:
		{
			FRigControlElementDetails_SetupStructValueWidget<FVector2D>(InGroup, InStructBuilder, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::Position:
		case ERigControlType::Scale:
		{
			FRigControlElementDetails_SetupStructValueWidget<FVector>(InGroup, InStructBuilder, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::Rotator:
		{
			FRigControlElementDetails_SetupStructValueWidget<FRotator>(InGroup, InStructBuilder, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::TransformNoScale:
		{
			FRigControlElementDetails_SetupStructValueWidget<FTransformNoScale>(InGroup, InStructBuilder, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::EulerTransform:
		{
			FRigControlElementDetails_SetupStructValueWidget<FEulerTransform>(InGroup, InStructBuilder, InValueType, InControlElement, InHierarchy);
			break;
		}
		case ERigControlType::Transform:
		{
			FRigControlElementDetails_SetupStructValueWidget<FTransform>(InGroup, InStructBuilder, InValueType, InControlElement, InHierarchy);
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

void FRigControlElementDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FRigTransformElementDetails::CustomizeChildren(InStructPropertyHandle, StructBuilder, StructCustomizationUtils);

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

	if (HierarchyBeingCustomized == nullptr)
	{
		return;
	}

	IDetailGroup* ControlGroup = &StructBuilder.AddGroup(TEXT("Control"), LOCTEXT("Control", "Control"));
	IDetailGroup* GizmoGroup = nullptr;
	IDetailGroup* LimitsGroup = nullptr;

	const TSharedPtr<IPropertyHandle> SettingsHandle = InStructPropertyHandle->GetChildHandle(TEXT("Settings"));
	const TSharedPtr<IPropertyHandle> DisplayNameHandle = SettingsHandle->GetChildHandle(TEXT("DisplayName"));

	ControlGroup->AddWidgetRow()
	.NameContent()
	[
		DisplayNameHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SEditableTextBox)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(this, &FRigControlElementDetails::GetDisplayName)
		.OnTextCommitted(this, &FRigControlElementDetails::SetDisplayName)
		.IsEnabled(ObjectsBeingCustomized.Num() == 1)
	];

	if (ControlTypeList.Num() == 0)
	{
		UEnum* Enum = StaticEnum<ERigControlType>();
		for (int64 Index = 0; Index < Enum->GetMaxEnumValue(); Index++)
		{
			ControlTypeList.Add(MakeShared<FString>(Enum->GetDisplayNameTextByValue(Index).ToString()));
		}
	}

	// when control type changes, we have to refresh detail panel
	const TSharedPtr<IPropertyHandle> ControlTypeHandle = SettingsHandle->GetChildHandle(TEXT("ControlType"));
	ControlTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda(
		[this, &StructCustomizationUtils]()
		{
			TArray<FRigControlElement*> ControlElementsInView = GetElementsInDetailsView<FRigControlElement>();
			TArray<FRigControlElement*> ControlElementsInHierarchy = GetElementsInHierarchy<FRigControlElement>();
			check(ControlElementsInView.Num() == ControlElementsInHierarchy.Num());

			if (this->HierarchyBeingCustomized && ControlElementsInHierarchy.Num() > 0)
			{
				for(int32 ControlIndex = 0; ControlIndex< ControlElementsInView.Num(); ControlIndex++)
				{
					FRigControlElement* ViewElement = ControlElementsInView[ControlIndex];
					FRigControlElement* ControlElement = ControlElementsInHierarchy[ControlIndex];
					
					FRigControlValue ValueToSet;

					ControlElement->Settings.ControlType = ViewElement->Settings.ControlType;
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
							ControlElement->Settings.MaximumValue = FRigControlValue::Make<float>(100.f);
							break;
						}
						case ERigControlType::Integer:
						{
							ValueToSet = FRigControlValue::Make<int32>(0);
							ControlElement->Settings.bLimitTranslation = true;
							ControlElement->Settings.MinimumValue = FRigControlValue::Make<int32>(0);
							ControlElement->Settings.MaximumValue = FRigControlValue::Make<int32>(100);
							break;
						}
						case ERigControlType::Vector2D:
						{
							ValueToSet = FRigControlValue::Make<FVector2D>(FVector2D::ZeroVector);
							ControlElement->Settings.bLimitTranslation = true;
							ControlElement->Settings.MinimumValue = FRigControlValue::Make<FVector2D>(FVector2D::ZeroVector);
							ControlElement->Settings.MaximumValue = FRigControlValue::Make<FVector2D>(FVector2D(100.f, 100.f));
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
					this->HierarchyBeingCustomized->SetControlValue(ControlElement, ValueToSet, ERigControlValueType::Initial, true, false, true);
					this->HierarchyBeingCustomized->SetControlValue(ControlElement, ValueToSet, ERigControlValueType::Current, true, false, true);

					FRigControlElement::StaticStruct()->CopyScriptStruct(ViewElement, ControlElement);

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
				
				StructCustomizationUtils.GetPropertyUtilities()->ForceRefresh();
			}
		}
	));

	ControlGroup->AddPropertyRow(ControlTypeHandle.ToSharedRef());
	ControlGroup->AddPropertyRow(SettingsHandle->GetChildHandle(TEXT("bAnimatable")).ToSharedRef());

	// any but bool controls show the offset + gizmo
	if(IsAnyControlNotOfType(ERigControlType::Bool))
	{
		// setup offset
		{
			const TSharedPtr<IPropertyHandle> OffsetHandle = InStructPropertyHandle->GetChildHandle(TEXT("Offset"));
			const TSharedPtr<IPropertyHandle> InitialHandle = OffsetHandle->GetChildHandle(TEXT("Initial"));
			const TSharedPtr<IPropertyHandle> LocalHandle = InitialHandle->GetChildHandle(TEXT("Local"));
			const TSharedPtr<IPropertyHandle> TransformHandle = LocalHandle->GetChildHandle(TEXT("Transform"));
			ControlGroup->AddPropertyRow(TransformHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Offset Transform")));
		}

		GizmoGroup = &StructBuilder.AddGroup(TEXT("Gizmo"), LOCTEXT("Gizmo", "Gizmo"));
	}

	if(IsAnyControlOfType(ERigControlType::Float) ||
		IsAnyControlOfType(ERigControlType::Integer) ||
		IsAnyControlOfType(ERigControlType::Vector2D) ||
		IsAnyControlOfType(ERigControlType::Position) ||
		IsAnyControlOfType(ERigControlType::Rotator) ||
		IsAnyControlOfType(ERigControlType::Scale) ||
		IsAnyControlOfType(ERigControlType::Transform) ||
		IsAnyControlOfType(ERigControlType::TransformNoScale) ||
		IsAnyControlOfType(ERigControlType::EulerTransform))
	{
		LimitsGroup = &StructBuilder.AddGroup(TEXT("Limits"), LOCTEXT("Limits", "Limits"));
	}
	
	if(IsAnyControlOfType(ERigControlType::Float) ||
		IsAnyControlOfType(ERigControlType::Integer) ||
		IsAnyControlOfType(ERigControlType::Vector2D) ||
		IsAnyControlOfType(ERigControlType::Position) ||
		IsAnyControlOfType(ERigControlType::Transform) ||
		IsAnyControlOfType(ERigControlType::TransformNoScale) ||
		IsAnyControlOfType(ERigControlType::EulerTransform))
	{
		const TSharedPtr<IPropertyHandle> LimitHandle = SettingsHandle->GetChildHandle(TEXT("bLimitTranslation"));
		LimitsGroup->AddPropertyRow(LimitHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Limit Translation")));
	}

	if(IsAnyControlOfType(ERigControlType::Rotator) ||
		IsAnyControlOfType(ERigControlType::Transform) ||
		IsAnyControlOfType(ERigControlType::TransformNoScale) ||
		IsAnyControlOfType(ERigControlType::EulerTransform))
	{
		const TSharedPtr<IPropertyHandle> LimitHandle = SettingsHandle->GetChildHandle(TEXT("bLimitRotation"));
		LimitsGroup->AddPropertyRow(LimitHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Limit Rotation")));
	}

	if(IsAnyControlOfType(ERigControlType::Scale) ||
		IsAnyControlOfType(ERigControlType::Transform) ||
		IsAnyControlOfType(ERigControlType::EulerTransform))
	{
		const TSharedPtr<IPropertyHandle> LimitHandle = SettingsHandle->GetChildHandle(TEXT("bLimitScale"));
		LimitsGroup->AddPropertyRow(LimitHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Limit Scale")));
	}

	if(!(IsAnyControlNotOfType(ERigControlType::Integer) &&
		IsAnyControlNotOfType(ERigControlType::Float) &&
		IsAnyControlNotOfType(ERigControlType::Vector2D)))
	{
		const TSharedPtr<IPropertyHandle> PrimaryAxisHandle = SettingsHandle->GetChildHandle(TEXT("PrimaryAxis"));
		ControlGroup->AddPropertyRow(PrimaryAxisHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Primary Axis")));
	}

	TArray<FRigControlElement*> ControlElements;
	for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(ObjectBeingCustomized.IsValid())
		{
			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(ObjectBeingCustomized->GetContent<FRigBaseElement>()))
			{
				ControlElements.Add(ControlElement);
			}
		}
	}

	// only setup value widgets if there is only ony control selected
	if(ControlElements.Num() == 1)
	{
		FRigControlElementDetails_SetupValueWidget(*ControlGroup, StructBuilder, ERigControlValueType::Current, ControlElements[0], HierarchyBeingCustomized);

		switch (ControlElements[0]->Settings.ControlType)
		{
			case ERigControlType::Bool:
			case ERigControlType::Float:
			case ERigControlType::Integer:
			case ERigControlType::Vector2D:
			{
				FRigControlElementDetails_SetupValueWidget(*ControlGroup, StructBuilder, ERigControlValueType::Initial, ControlElements[0], HierarchyBeingCustomized);
				break;
			}
			default:
			{
				break;
			}
		}
		
		FRigControlElementDetails_SetupValueWidget(*ControlGroup, StructBuilder, ERigControlValueType::Minimum, ControlElements[0], HierarchyBeingCustomized);
		FRigControlElementDetails_SetupValueWidget(*ControlGroup, StructBuilder, ERigControlValueType::Maximum, ControlElements[0], HierarchyBeingCustomized);
	}

	if(IsAnyControlOfType(ERigControlType::Float) ||
		IsAnyControlOfType(ERigControlType::Integer) ||
		IsAnyControlOfType(ERigControlType::Vector2D) ||
		IsAnyControlOfType(ERigControlType::Position) ||
		IsAnyControlOfType(ERigControlType::Scale) ||
		IsAnyControlOfType(ERigControlType::Rotator) ||
		IsAnyControlOfType(ERigControlType::Transform) ||
		IsAnyControlOfType(ERigControlType::TransformNoScale) ||
		IsAnyControlOfType(ERigControlType::EulerTransform))
	{
		GizmoGroup->AddPropertyRow(SettingsHandle->GetChildHandle(TEXT("bGizmoEnabled")).ToSharedRef());
		GizmoGroup->AddPropertyRow(SettingsHandle->GetChildHandle(TEXT("bGizmoVisible")).ToSharedRef());

		// setup gizmo transform
		{
			const TSharedPtr<IPropertyHandle> GizmoHandle = InStructPropertyHandle->GetChildHandle(TEXT("Gizmo"));
			const TSharedPtr<IPropertyHandle> InitialHandle = GizmoHandle->GetChildHandle(TEXT("Initial"));
			const TSharedPtr<IPropertyHandle> LocalHandle = InitialHandle->GetChildHandle(TEXT("Local"));
			const TSharedPtr<IPropertyHandle> TransformHandle = LocalHandle->GetChildHandle(TEXT("Transform"));
			GizmoGroup->AddPropertyRow(TransformHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Gizmo Transform")))
			.IsEnabled(TAttribute<bool>::CreateSP(this, &FRigControlElementDetails::IsGizmoEnabled));
		}

		const TSharedPtr<IPropertyHandle> GizmoNameHandle = SettingsHandle->GetChildHandle(TEXT("GizmoName"));
		GizmoGroup->AddPropertyRow(GizmoNameHandle.ToSharedRef()).CustomWidget()
		.NameContent()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Gizmo Name")))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.IsEnabled(this, &FRigControlElementDetails::IsGizmoEnabled)
		]
		.ValueContent()
		[
			SNew(SControlRigGizmoNameList, ControlElements, BlueprintBeingCustomized)
			.OnGetNameListContent(this, &FRigControlElementDetails::GetGizmoNameList)
			.IsEnabled(this, &FRigControlElementDetails::IsGizmoEnabled)
		];

		GizmoGroup->AddPropertyRow(SettingsHandle->GetChildHandle(TEXT("GizmoColor")).ToSharedRef());
	}

	if(IsAnyControlOfType(ERigControlType::Integer))
	{
		const TSharedPtr<IPropertyHandle> ControlEnumHandle = SettingsHandle->GetChildHandle(TEXT("ControlEnum"));
		ControlGroup->AddPropertyRow(ControlEnumHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Control Enum")));

		ControlEnumHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda(
			[this, &StructCustomizationUtils, ControlElements]()
			{
				StructCustomizationUtils.GetPropertyUtilities()->ForceRefresh();

				if (this->HierarchyBeingCustomized != nullptr)
				{
					for(FRigControlElement* ControlBeingCustomized : ControlElements)
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
							HierarchyBeingCustomized->SetControlValue(ControlBeingCustomized, InitialValue, ERigControlValueType::Initial, false, false, true);
							HierarchyBeingCustomized->SetControlValue(ControlBeingCustomized, CurrentValue, ERigControlValueType::Current, false, false, true);

							if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
							{
								URigHierarchy* DebuggedHierarchy = DebuggedRig->GetHierarchy();
								if(FRigControlElement* DebuggedControlElement = DebuggedHierarchy->Find<FRigControlElement>(ControlBeingCustomized->GetKey()))
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

bool FRigControlElementDetails::IsGizmoEnabled() const
{
	for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(ObjectBeingCustomized.IsValid())
		{
			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(ObjectBeingCustomized->GetContent<FRigBaseElement>()))
			{
				if(ControlElement->Settings.bGizmoEnabled)
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool FRigControlElementDetails::IsEnabled(ERigControlValueType InValueType) const
{
	for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(ObjectBeingCustomized.IsValid())
		{
			if(FRigControlElement* ControlElement = Cast<FRigControlElement>(ObjectBeingCustomized->GetContent<FRigBaseElement>()))
			{
				switch (InValueType)
				{
					case ERigControlValueType::Minimum:
					case ERigControlValueType::Maximum:
					{
						if(ControlElement->Settings.bLimitTranslation || ControlElement->Settings.bLimitRotation || ControlElement->Settings.bLimitScale)
						{
							return true;
						}
					}
					default:
					{
						break;
					}
				}
			}
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

FText FRigControlElementDetails::GetDisplayName() const
{
	FName DisplayName(NAME_None);

	for(int32 ObjectIndex = 0; ObjectIndex < ObjectsBeingCustomized.Num(); ObjectIndex++)
	{
		TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized = ObjectsBeingCustomized[ObjectIndex];
		if(ObjectBeingCustomized.IsValid())
		{
			const FRigControlElement* ControlElement = ObjectBeingCustomized->GetContent<FRigControlElement>();
			if(ObjectIndex == 0)
			{
				DisplayName = ControlElement->Settings.DisplayName;
			}
			else if(DisplayName != ControlElement->Settings.DisplayName)
			{
				return ControlRigDetailsMultipleValues;
			}
		}
	}

	if(!DisplayName.IsNone())
	{
		return FText::FromName(DisplayName);
	}
	return FText();
}

void FRigControlElementDetails::SetDisplayName(const FText& InNewText, ETextCommit::Type InCommitType)
{
	const FName DisplayName = InNewText.IsEmpty() ? FName(NAME_None) : FName(*InNewText.ToString());
	
	for(int32 ObjectIndex = 0; ObjectIndex < ObjectsBeingCustomized.Num(); ObjectIndex++)
	{
		TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized = ObjectsBeingCustomized[ObjectIndex];
		if(ObjectBeingCustomized.IsValid())
		{
			FRigControlElement* ControlElement = ObjectBeingCustomized->GetContent<FRigControlElement>();

			FRigControlSettings Settings = ControlElement->Settings;
			Settings.DisplayName = DisplayName;

			if(GetHierarchy())
			{
				GetHierarchy()->SetControlSettings(ControlElement->GetKey(), Settings);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
