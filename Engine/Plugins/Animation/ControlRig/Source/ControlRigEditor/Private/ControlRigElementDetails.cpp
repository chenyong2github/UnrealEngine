// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigElementDetails.h"
#include "Widgets/SWidget.h"
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
#include "Graph/ControlRigGraph.h"
#include "PropertyCustomizationHelpers.h"
#include "SEnumCombo.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "Graph/SControlRigGraphPinVariableBinding.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ScopedTransaction.h"
#include "AnimationCoreLibrary.h"
#include "HAL/PlatformApplicationMisc.h"

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
				.IsEnabled(!NameHandle->IsEditConst())
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
	else if(PropertyPath.StartsWith(TEXT("Shape.")))
	{
		PropertyPath.RightChopInline(6);
		PropertyChain.AddTail(FRigControlElement::StaticStruct()->FindPropertyByName(TEXT("Shape")));
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
	BlueprintBeingCustomized = nullptr;
	HierarchyBeingCustomized = nullptr;
	ObjectsBeingCustomized.Reset();

	TArray<TWeakObjectPtr<UObject>> DetailObjects;
	DetailBuilder.GetObjectsBeingCustomized(DetailObjects);
	for(TWeakObjectPtr<UObject> DetailObject : DetailObjects)
	{
		UDetailsViewWrapperObject* WrapperObject = CastChecked<UDetailsViewWrapperObject>(DetailObject.Get());

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
		RigElementDetails_GetCustomizedInfo(DetailBuilder.GetProperty(TEXT("Key")), BlueprintBeingCustomized);
	}

	IDetailCategoryBuilder& GeneralCategory = DetailBuilder.EditCategory(TEXT("General"), LOCTEXT("General", "General"));

	GeneralCategory.AddCustomRow(FText::FromString(TEXT("Name")))
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
		.OnVerifyTextChanged(this, &FRigBaseElementDetails::OnVerifyNameChanged)
		.IsEnabled(ObjectsBeingCustomized.Num() == 1)
	];

	DetailBuilder.HideCategory(TEXT("RigElement"));
}

FRigElementKey FRigBaseElementDetails::GetElementKey() const
{
	check(ObjectsBeingCustomized.Num() == 1);
	if(ObjectsBeingCustomized[0].IsValid())
	{
		return ObjectsBeingCustomized[0]->GetContent<FRigBaseElement>().GetKey(); 
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
	if(InCommitType == ETextCommit::OnCleared)
	{
		return;
	}
	
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

bool FRigBaseElementDetails::OnVerifyNameChanged(const FText& InText, FText& OutErrorMessage)
{
	if(ObjectsBeingCustomized.Num() > 1)
	{
		return false;
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

	if (!Hierarchy)
	{
		return false;
	}

	if (GetElementKey().Name.ToString() == InText.ToString())
	{
		return true;
	}

	FString OutErrorMessageStr;
	if (!Hierarchy->IsNameAvailable(InText.ToString(), GetElementKey().Type, &OutErrorMessageStr))
	{
		OutErrorMessage = FText::FromString(OutErrorMessageStr);
		return false;
	}

	return true;
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
			Keys.Add(ObjectBeingCustomized->GetContent<FRigBaseElement>().GetKey());
		}
	}
	return Keys;
}

bool FRigBaseElementDetails::IsAnyElementOfType(ERigElementType InType) const
{
	for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(ObjectBeingCustomized.IsValid())
		{
			if(ObjectBeingCustomized->GetContent<FRigBaseElement>().GetKey().Type == InType)
			{
				return true;
			}
		}
	}
	return false;
}

bool FRigBaseElementDetails::IsAnyElementNotOfType(ERigElementType InType) const
{
	for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(ObjectBeingCustomized.IsValid())
		{
			if(ObjectBeingCustomized->GetContent<FRigBaseElement>().GetKey().Type != InType)
			{
				return true;
			}
		}
	}
	return false;
}

bool FRigBaseElementDetails::IsAnyControlOfType(ERigControlType InType) const
{
	for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(ObjectBeingCustomized.IsValid())
		{
			if(ObjectBeingCustomized->IsChildOf<FRigControlElement>())
			{
				const FRigControlElement ControlElement = ObjectBeingCustomized->GetContent<FRigControlElement>();
				if(ControlElement.Settings.ControlType == InType)
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
			if(ObjectBeingCustomized->IsChildOf<FRigControlElement>())
			{
				const FRigControlElement ControlElement = ObjectBeingCustomized->GetContent<FRigControlElement>();
				if(ControlElement.Settings.ControlType != InType)
				{
					return true;
				}
			}
		}
	}
	return false;
}

void FRigBaseElementDetails::RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule)
{
	FRigBoneElementDetails().RegisterSectionMappings(PropertyEditorModule, UDetailsViewWrapperObject::GetClassForStruct(FRigBoneElement::StaticStruct()));
	FRigNullElementDetails().RegisterSectionMappings(PropertyEditorModule, UDetailsViewWrapperObject::GetClassForStruct(FRigNullElement::StaticStruct()));
	FRigControlElementDetails().RegisterSectionMappings(PropertyEditorModule, UDetailsViewWrapperObject::GetClassForStruct(FRigControlElement::StaticStruct()));
}

void FRigBaseElementDetails::RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule, UClass* InClass)
{
}

namespace ERigTransformElementDetailsTransform
{
	enum Type
	{
		Initial,
		Current,
		Offset,
		Max
	};
}

void FRigTransformElementDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FRigBaseElementDetails::CustomizeDetails(DetailBuilder);
}

void FRigTransformElementDetails::RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule, UClass* InClass)
{
	FRigBaseElementDetails::RegisterSectionMappings(PropertyEditorModule, InClass);
	
	TSharedRef<FPropertySection> TransformSection = PropertyEditorModule.FindOrCreateSection(InClass->GetFName(), "Transform", LOCTEXT("Transform", "Transform"));
	TransformSection->AddCategory("General");
	TransformSection->AddCategory("Transform");
}

void FRigTransformElementDetails::CustomizeTransform(IDetailLayoutBuilder& DetailBuilder)
{
	URigHierarchy* HierarchyBeingDebugged = HierarchyBeingCustomized;
	bool IsSetupModeEnabled = false;
	if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
	{
		IsSetupModeEnabled = DebuggedRig->IsSetupModeEnabled(); 
 		if(!IsSetupModeEnabled)
		{
			HierarchyBeingDebugged = DebuggedRig->GetHierarchy();
		}
	}

	TArray<FRigElementKey> Keys = GetElementKeys();
	Keys = HierarchyBeingDebugged->SortKeys(Keys);
	const bool bAllControls = !IsAnyElementNotOfType(ERigElementType::Control);
	TArray<ERigTransformElementDetailsTransform::Type> TransformTypes;
	TArray<FText> ButtonLabels;
	TArray<FText> ButtonTooltips;

	if(bAllControls)
	{
		TransformTypes = {
			ERigTransformElementDetailsTransform::Initial,
			ERigTransformElementDetailsTransform::Current,
			ERigTransformElementDetailsTransform::Offset
		};
		ButtonLabels = {
			LOCTEXT("Initial", "Initial"),
			LOCTEXT("Current", "Current"),
			LOCTEXT("Offset", "Offset")
		};
		ButtonTooltips = {
			LOCTEXT("InitialTooltip", "Initial transform in the reference pose"),
			LOCTEXT("CurrentTooltip", "Current animation transform"),
			LOCTEXT("OffsetTooltip", "Offset transform under the control")
		};
	}
	else
	{
		TransformTypes = {
			ERigTransformElementDetailsTransform::Initial,
			ERigTransformElementDetailsTransform::Current
		};
		ButtonLabels = {
			LOCTEXT("Initial", "Initial"),
			LOCTEXT("Current", "Current")
		};
		ButtonTooltips = {
			LOCTEXT("InitialTooltip", "Initial transform in the reference pose"),
			LOCTEXT("CurrentTooltip", "Current animation transform")
		};
	}

	TArray<bool> bTransformsEnabled;

	// determine if the transforms are enabled
	for(int32 Index = 0; Index < TransformTypes.Num(); Index++)
	{
		ERigTransformElementDetailsTransform::Type CurrentTransformType = (ERigTransformElementDetailsTransform::Type)Index;

		bool bIsTransformEnabled = true;
		if (IsAnyElementOfType(ERigElementType::Control))
		{
			bIsTransformEnabled = IsAnyControlOfType(ERigControlType::EulerTransform) ||
				IsAnyControlOfType(ERigControlType::Transform) ||
				CurrentTransformType == ERigTransformElementDetailsTransform::Offset;

			if(!bIsTransformEnabled)
			{
				ButtonTooltips[Index] = FText::FromString(
					FString::Printf(TEXT("%s\n%s"),
						*ButtonTooltips[Index].ToString(), 
						TEXT("Only transform controls can be edited here. Refer to the 'Value' section instead.")));
			}
		}
		else if (IsAnyElementOfType(ERigElementType::Bone) && CurrentTransformType == ERigTransformElementDetailsTransform::Initial)
		{
			for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
			{
				if(ObjectBeingCustomized.IsValid())
				{
					if(ObjectBeingCustomized->IsChildOf<FRigBoneElement>())
					{
						const FRigBoneElement BoneElement = ObjectBeingCustomized->GetContent<FRigBoneElement>();
						bIsTransformEnabled = BoneElement.BoneType == ERigBoneType::User;

						if(!bIsTransformEnabled)
						{
							ButtonTooltips[Index] = FText::FromString(
								FString::Printf(TEXT("%s\n%s"),
									*ButtonTooltips[Index].ToString(), 
									TEXT("Imported Bones' initial transform cannot be edited.")));
						}
					}
				}
			}			
		}

		bTransformsEnabled.Add(bIsTransformEnabled);
	}

	static TAttribute<TArray<ERigTransformElementDetailsTransform::Type>> PickedTransforms = 
		TArray<ERigTransformElementDetailsTransform::Type>({ERigTransformElementDetailsTransform::Current});

	TSharedPtr<SSegmentedControl<ERigTransformElementDetailsTransform::Type>> TransformChoiceWidget =
		SSegmentedControl<ERigTransformElementDetailsTransform::Type>::Create(
			TransformTypes,
			ButtonLabels,
			ButtonTooltips,
			PickedTransforms
		);

	IDetailCategoryBuilder& TransformCategory = DetailBuilder.EditCategory(TEXT("Transform"), LOCTEXT("Transform", "Transform"));
	AddChoiceWidgetRow(TransformCategory, FText::FromString(TEXT("TransformType")), TransformChoiceWidget.ToSharedRef());
	
	TSharedRef<UE::Math::TVector<float>> IsComponentRelative = MakeShareable(new UE::Math::TVector<float>(1.f, 1.f, 1.f));

	using TransformType = FEulerTransform;
	using NumericType = FVector::FReal;

	SAdvancedTransformInputBox<TransformType>::FArguments TransformWidgetArgs = SAdvancedTransformInputBox<TransformType>::FArguments()
		.DisplayToggle(false)
		.DisplayRelativeWorld(true)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.OnGetIsComponentRelative_Lambda(
			[IsComponentRelative](ESlateTransformComponent::Type InComponent)
			{
				return IsComponentRelative->operator[]((int32)InComponent) > 0.f;
			})
		.OnIsComponentRelativeChanged_Lambda(
			[IsComponentRelative](ESlateTransformComponent::Type InComponent, bool bIsRelative)
			{
				IsComponentRelative->operator[]((int32)InComponent) = bIsRelative ? 1.f : 0.f;
			});
	
	for(int32 Index = 0; Index < ButtonLabels.Num(); Index++)
	{
		ERigTransformElementDetailsTransform::Type CurrentTransformType = (ERigTransformElementDetailsTransform::Type)Index;

		auto GetRelativeAbsoluteTransforms = [CurrentTransformType, Keys, HierarchyBeingDebugged](
			const FRigElementKey& Key,
			ERigTransformElementDetailsTransform::Type InTransformType = ERigTransformElementDetailsTransform::Max
			) -> TPair<TransformType, TransformType>
		{
			if(InTransformType == ERigTransformElementDetailsTransform::Max)
			{
				InTransformType = CurrentTransformType;
			}

			TransformType RelativeTransform = TransformType::Identity;
			TransformType AbsoluteTransform = TransformType::Identity;

			const bool bInitial = InTransformType == ERigTransformElementDetailsTransform::Initial; 
			if(bInitial || InTransformType == ERigTransformElementDetailsTransform::Current)
			{
				RelativeTransform = HierarchyBeingDebugged->GetLocalTransform(Key, bInitial);
				AbsoluteTransform = HierarchyBeingDebugged->GetGlobalTransform(Key, bInitial);
			}
			else
			{
				if(FRigControlElement* ControlElement = HierarchyBeingDebugged->Find<FRigControlElement>(Key))
				{
					RelativeTransform = HierarchyBeingDebugged->GetControlOffsetTransform(ControlElement, bInitial ? ERigTransformType::InitialLocal : ERigTransformType::CurrentLocal);
					AbsoluteTransform = HierarchyBeingDebugged->GetControlOffsetTransform(ControlElement, bInitial ? ERigTransformType::InitialGlobal : ERigTransformType::CurrentGlobal);
				}
			}

			return TPair<FTransform, FTransform>(RelativeTransform, AbsoluteTransform);
		};

		auto GetCombinedTransform = [IsComponentRelative, GetRelativeAbsoluteTransforms](
			const FRigElementKey& Key,
			ERigTransformElementDetailsTransform::Type InTransformType = ERigTransformElementDetailsTransform::Max
			) -> TransformType
		{
			const TPair<TransformType, TransformType> TransformPair = GetRelativeAbsoluteTransforms(Key, InTransformType);
			const TransformType RelativeTransform = TransformPair.Key;
			const TransformType AbsoluteTransform = TransformPair.Value;

			TransformType Xfo;
			Xfo.SetLocation((IsComponentRelative->X > 0.f) ? RelativeTransform.GetLocation() : AbsoluteTransform.GetLocation());
			Xfo.SetRotation((IsComponentRelative->Y > 0.f) ? RelativeTransform.GetRotation() : AbsoluteTransform.GetRotation());
			Xfo.SetScale3D((IsComponentRelative->Z > 0.f) ? RelativeTransform.GetScale3D() : AbsoluteTransform.GetScale3D());

			return Xfo;
		};

		auto GetSingleTransform = [GetRelativeAbsoluteTransforms](
			const FRigElementKey& Key,
			bool bIsRelative,
			ERigTransformElementDetailsTransform::Type InTransformType = ERigTransformElementDetailsTransform::Max
			) -> TransformType
		{
			const TPair<TransformType, TransformType> TransformPair = GetRelativeAbsoluteTransforms(Key, InTransformType);
			const TransformType RelativeTransform = TransformPair.Key;
			const TransformType AbsoluteTransform = TransformPair.Value;
			return bIsRelative ? RelativeTransform : AbsoluteTransform;
		};

		auto SetSingleTransform = [CurrentTransformType, GetRelativeAbsoluteTransforms, this, IsSetupModeEnabled, HierarchyBeingDebugged](
			const FRigElementKey& Key,
			TransformType InTransform,
			bool bIsRelative)
		{
			const bool bInitial = CurrentTransformType == ERigTransformElementDetailsTransform::Initial; 

			TArray<URigHierarchy*> HierarchiesToUpdate;
			HierarchiesToUpdate.Add(HierarchyBeingDebugged);
			if(bInitial || IsSetupModeEnabled)
			{
				HierarchiesToUpdate.Add(HierarchyBeingCustomized);
			}

			for(URigHierarchy* HierarchyToUpdate : HierarchiesToUpdate)
			{
				if(bInitial || CurrentTransformType == ERigTransformElementDetailsTransform::Current)
				{
					if(bIsRelative)
					{
						HierarchyToUpdate->SetLocalTransform(Key, InTransform, bInitial, true, true);
					}
					else
					{
						HierarchyToUpdate->SetGlobalTransform(Key, InTransform, bInitial, true, true);
					}
				}
				else
				{
					if(!bIsRelative)
					{
						const FTransform ParentTransform = HierarchyToUpdate->GetParentTransform(Key, bInitial);
						InTransform = FTransform(InTransform).GetRelativeTransform(ParentTransform);
					}
							
					if(FRigControlElement* ControlElement = HierarchyToUpdate->Find<FRigControlElement>(Key))
					{
						HierarchyToUpdate->SetControlOffsetTransform(Key, InTransform, bInitial, true, true);
					}
				}
			}
		};

		TransformWidgetArgs.Visibility_Lambda([TransformChoiceWidget, Index]() -> EVisibility
		{
			return TransformChoiceWidget->HasValue((ERigTransformElementDetailsTransform::Type)Index) ? EVisibility::Visible : EVisibility::Collapsed;
		});

		TransformWidgetArgs.IsEnabled(bTransformsEnabled[Index]);

		TransformWidgetArgs.OnGetNumericValue_Lambda([Keys, GetCombinedTransform](
			ESlateTransformComponent::Type Component,
			ESlateRotationRepresentation::Type Representation,
			ESlateTransformSubComponent::Type SubComponent) -> TOptional<NumericType>
		{
			TOptional<NumericType> FirstValue;

			for(int32 Index = 0; Index < Keys.Num(); Index++)
			{
				const FRigElementKey& Key = Keys[Index];
				TransformType Xfo = GetCombinedTransform(Key);

				TOptional<NumericType> CurrentValue = SAdvancedTransformInputBox<TransformType>::GetNumericValueFromTransform(Xfo, Component, Representation, SubComponent);
				if(!CurrentValue.IsSet())
				{
					return CurrentValue;
				}

				if(Index == 0)
				{
					FirstValue = CurrentValue;
				}
				else
				{
					if(!FMath::IsNearlyEqual(FirstValue.GetValue(), CurrentValue.GetValue()))
					{
						return TOptional<NumericType>();
					}
				}
			}
			
			return FirstValue;
		});

		TransformWidgetArgs.OnNumericValueChanged_Lambda([
			Keys,
			this,
			IsComponentRelative,
			GetSingleTransform,
			SetSingleTransform
		](
			ESlateTransformComponent::Type Component,
			ESlateRotationRepresentation::Type Representation,
			ESlateTransformSubComponent::Type SubComponent,
			NumericType InNumericValue)
		{
			const bool bIsRelative = IsComponentRelative->Component((int32)Component) > 0.f;

			FScopedTransaction Transaction(LOCTEXT("ChangeNumericValue", "Change Numeric Value"));

			for(const FRigElementKey& Key : Keys)
			{
				TransformType Transform = GetSingleTransform(Key, bIsRelative);
				SAdvancedTransformInputBox<TransformType>::ApplyNumericValueChange(Transform, InNumericValue, Component, Representation, SubComponent);
				SetSingleTransform(Key, Transform, bIsRelative);
			}
		});

		TransformWidgetArgs.OnCopyToClipboard_Lambda([Keys, IsComponentRelative, GetSingleTransform](
			ESlateTransformComponent::Type InComponent
			)
		{
			if(Keys.Num() == 0)
			{
				return;
			}

			// make sure that we use the same relative setting on all components when copying
			IsComponentRelative->Y = IsComponentRelative->Z = IsComponentRelative->X;
			const bool bIsRelative = IsComponentRelative->Y > 0.f; 

			const FRigElementKey& FirstKey = Keys[0];
			TransformType Xfo = GetSingleTransform(FirstKey, bIsRelative);

			FString Content;
			switch(InComponent)
			{
				case ESlateTransformComponent::Location:
				{
					const FVector Data = Xfo.GetLocation();
					TBaseStructure<FVector>::Get()->ExportText(Content, &Data, &Data, nullptr, PPF_None, nullptr);
					break;
				}
				case ESlateTransformComponent::Rotation:
				{
					const FRotator Data = Xfo.Rotator();
					TBaseStructure<FRotator>::Get()->ExportText(Content, &Data, &Data, nullptr, PPF_None, nullptr);
					break;
				}
				case ESlateTransformComponent::Scale:
				{
					const FVector Data = Xfo.GetScale3D();
					TBaseStructure<FVector>::Get()->ExportText(Content, &Data, &Data, nullptr, PPF_None, nullptr);
					break;
				}
				case ESlateTransformComponent::Max:
				default:
				{
					TBaseStructure<TransformType>::Get()->ExportText(Content, &Xfo, &Xfo, nullptr, PPF_None, nullptr);
					break;
				}
			}

			if(!Content.IsEmpty())
			{
				FPlatformApplicationMisc::ClipboardCopy(*Content);
			}
		});

		TransformWidgetArgs.OnPasteFromClipboard_Lambda([Keys, IsComponentRelative, GetSingleTransform, SetSingleTransform](
			ESlateTransformComponent::Type InComponent
			)
		{
			if(Keys.Num() == 0)
			{
				return;
			}
			
			
			// make sure that we use the same relative setting on all components when pasting
			IsComponentRelative->Y = IsComponentRelative->Z = IsComponentRelative->X;
			const bool bIsRelative = IsComponentRelative->Y > 0.f; 

			FString Content;
			FPlatformApplicationMisc::ClipboardPaste(Content);

			if(Content.IsEmpty())
			{
				return;
			}

			FScopedTransaction Transaction(LOCTEXT("PasteTransform", "Paste Transform"));

			for(const FRigElementKey& Key : Keys)
			{
				TransformType Xfo = GetSingleTransform(Key, bIsRelative);

				{
					class FRigPasteTransformWidgetErrorPipe : public FOutputDevice
					{
					public:

						int32 NumErrors;

						FRigPasteTransformWidgetErrorPipe()
							: FOutputDevice()
							, NumErrors(0)
						{
						}

						virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
						{
							UE_LOG(LogControlRig, Error, TEXT("Error Pasting to Widget: %s"), V);
							NumErrors++;
						}
					};

					FRigPasteTransformWidgetErrorPipe ErrorPipe;
					
					switch(InComponent)
					{
						case ESlateTransformComponent::Location:
						{
							FVector Data = Xfo.GetLocation();
							TBaseStructure<FVector>::Get()->ImportText(*Content, &Data, nullptr, PPF_None, &ErrorPipe, TBaseStructure<FVector>::Get()->GetName(), true);
							Xfo.SetLocation(Data);
							break;
						}
						case ESlateTransformComponent::Rotation:
						{
							FRotator Data = Xfo.Rotator();
							TBaseStructure<FRotator>::Get()->ImportText(*Content, &Data, nullptr, PPF_None, &ErrorPipe, TBaseStructure<FRotator>::Get()->GetName(), true);
							Xfo.SetRotator(Data);
							break;
						}
						case ESlateTransformComponent::Scale:
						{
							FVector Data = Xfo.GetScale3D();
							TBaseStructure<FVector>::Get()->ImportText(*Content, &Data, nullptr, PPF_None, &ErrorPipe, TBaseStructure<FVector>::Get()->GetName(), true);
							Xfo.SetScale3D(Data);
							break;
						}
						case ESlateTransformComponent::Max:
						default:
						{
							TBaseStructure<TransformType>::Get()->ImportText(*Content, &Xfo, nullptr, PPF_None, &ErrorPipe, TBaseStructure<TransformType>::Get()->GetName(), true);
							break;
						}
					}

					if(ErrorPipe.NumErrors == 0)
					{
						SetSingleTransform(Key, Xfo, bIsRelative);
					}
				}
			}
		});

		TransformWidgetArgs.DiffersFromDefault_Lambda([
			CurrentTransformType,
			Keys,
			GetSingleTransform
			
		](
			ESlateTransformComponent::Type InComponent) -> bool
		{
			for(const FRigElementKey& Key : Keys)
			{
				const TransformType CurrentTransform = GetSingleTransform(Key, true);
				TransformType DefaultTransform;

				switch(CurrentTransformType)
				{
					case ERigTransformElementDetailsTransform::Current:
					{
						DefaultTransform = GetSingleTransform(Key, true, ERigTransformElementDetailsTransform::Initial);
						break;
					}
					default:
					{
						DefaultTransform = TransformType::Identity; 
						break;
					}
				}

				switch(InComponent)
				{
					case ESlateTransformComponent::Location:
					{
						if(!DefaultTransform.GetLocation().Equals(CurrentTransform.GetLocation()))
						{
							return true;
						}
						break;
					}
					case ESlateTransformComponent::Rotation:
					{
						if(!DefaultTransform.Rotator().Equals(CurrentTransform.Rotator()))
						{
							return true;
						}
						break;
					}
					case ESlateTransformComponent::Scale:
					{
						if(!DefaultTransform.GetScale3D().Equals(CurrentTransform.GetScale3D()))
						{
							return true;
						}
						break;
					}
					default: // also no component whole transform
					{
						if(!DefaultTransform.GetLocation().Equals(CurrentTransform.GetLocation()) ||
							!DefaultTransform.Rotator().Equals(CurrentTransform.Rotator()) ||
							!DefaultTransform.GetScale3D().Equals(CurrentTransform.GetScale3D()))
						{
							return true;
						}
						break;
					}
				}
			}
			return false;
		});

		TransformWidgetArgs.OnResetToDefault_Lambda([CurrentTransformType, Keys, GetSingleTransform, SetSingleTransform](
			ESlateTransformComponent::Type InComponent)
		{
			FScopedTransaction Transaction(LOCTEXT("ResetTransformToDefault", "Reset Transform to Default"));
			
			for(const FRigElementKey& Key : Keys)
			{
				TransformType CurrentTransform = GetSingleTransform(Key, true);
				TransformType DefaultTransform;

				switch(CurrentTransformType)
				{
					case ERigTransformElementDetailsTransform::Current:
					{
						DefaultTransform = GetSingleTransform(Key, true, ERigTransformElementDetailsTransform::Initial);
						break;
					}
					default:
					{
						DefaultTransform = TransformType::Identity; 
						break;
					}
				}

				switch(InComponent)
				{
					case ESlateTransformComponent::Location:
					{
						CurrentTransform.SetLocation(DefaultTransform.GetLocation());
						break;
					}
					case ESlateTransformComponent::Rotation:
					{
						CurrentTransform.SetRotator(DefaultTransform.Rotator());
						break;
					}
					case ESlateTransformComponent::Scale:
					{
						CurrentTransform.SetScale3D(DefaultTransform.GetScale3D());
						break;
					}
					default: // whole transform / max component
					{
						CurrentTransform = DefaultTransform;
						break;
					}
				}

				SetSingleTransform(Key, CurrentTransform, true);
			}
		});

		SAdvancedTransformInputBox<TransformType>::ConstructGroupedTransformRows(
			TransformCategory, 
			ButtonLabels[Index], 
			ButtonTooltips[Index], 
			TransformWidgetArgs);
	}
}

bool FRigTransformElementDetails::IsCurrentLocalEnabled() const
{
	for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(ObjectBeingCustomized.IsValid())
		{
			if(ObjectBeingCustomized->GetContent<FRigBaseElement>().GetType() == ERigElementType::Control)
			{
				return false;
			}
		}
	}
	return true;
}

void FRigTransformElementDetails::AddChoiceWidgetRow(IDetailCategoryBuilder& InCategory, const FText& InSearchText, TSharedRef<SWidget> InWidget)
{
	InCategory.AddCustomRow(FText::FromString(TEXT("TransformType")))
	.ValueContent()
	.MinDesiredWidth(375.f)
	.MaxDesiredWidth(375.f)
	.HAlign(HAlign_Left)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			InWidget
		]
	];
}

void FRigControlElementDetails_SetupBoolValueWidget(IDetailCategoryBuilder& InCategory, const TSharedRef<IPropertyUtilities>& InPropertyUtilities, ERigControlValueType InValueType, const FRigElementKey& InKey, URigHierarchy* InHierarchy)
{
	UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
	UEnum* ValueTypeEnum = StaticEnum<ERigControlValueType>();

	const FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue((int64)InValueType).ToString();
	const FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s Value"), *ValueTypeName));
	TWeakObjectPtr<URigHierarchy> HierarchyPtr = InHierarchy;

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
	        .IsChecked_Lambda([HierarchyPtr, InKey, InValueType]() -> ECheckBoxState
	        {
	        	if(HierarchyPtr.IsValid())
	        	{
	        		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
	        		{
                        bool Value = HierarchyPtr->GetControlValue(ControlElement, InValueType).Get<bool>();
						return Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                    }
	        	}
	        	return ECheckBoxState::Unchecked;
	        })
	        .OnCheckStateChanged_Lambda([HierarchyPtr, InKey, InValueType](ECheckBoxState NewState)
	        {
	        	if(HierarchyPtr.IsValid())
	        	{
	        		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
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
	.IsEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([HierarchyPtr, InKey, InValueType]()->bool
    {
		if(HierarchyPtr.IsValid())
		{
			if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
			{
				return ControlElement->Settings.IsValueTypeEnabled(InValueType);
			}
		}
		return false;
    })));
}

void FRigControlElementDetails_SetupIntegerValueWidget(IDetailCategoryBuilder& InCategory, const TSharedRef<IPropertyUtilities>& InPropertyUtilities, ERigControlValueType InValueType, const FRigElementKey& InKey, URigHierarchy* InHierarchy)
{
	FRigControlElement* ControlElement = InHierarchy->Find<FRigControlElement>(InKey);
	if(ControlElement == nullptr)
	{
		return;
	}

	UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
	UEnum* ValueTypeEnum = StaticEnum<ERigControlValueType>();

	const FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue((int64)InValueType).ToString();
	const FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s Value"), *ValueTypeName));
	TWeakObjectPtr<URigHierarchy> HierarchyPtr = InHierarchy;

	const TAttribute<bool> EnabledAttribute = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([HierarchyPtr, InKey, InValueType]()->bool
    {
		if(HierarchyPtr.IsValid())
		{
            if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
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

	if (ControlElement->Settings.ControlEnum)
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
                SNew(SEnumComboBox, ControlElement->Settings.ControlEnum)
                .CurrentValue_Lambda([HierarchyPtr, InKey, InValueType]() -> int32
                {
                	if(HierarchyPtr.IsValid())
                	{
                		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
                		{
                            return HierarchyPtr->GetControlValue(ControlElement, InValueType).Get<int32>();
                        }
                	}
                	return 0;
                })
                .OnEnumSelectionChanged_Lambda([HierarchyPtr, InKey, InValueType](int32 NewSelection, ESelectInfo::Type)
                {
                	if(HierarchyPtr.IsValid())
                	{
                		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
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
                .AllowSpin(InValueType == ERigControlValueType::Current || InValueType == ERigControlValueType::Initial)
                .MinSliderValue_Lambda([HierarchyPtr, InKey, InValueType]() -> TOptional<int32>
				{
				    if(InValueType == ERigControlValueType::Current || InValueType == ERigControlValueType::Initial)
				    {
				    	if(HierarchyPtr.IsValid())
				    	{
				    		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
				    		{
                                return ControlElement->Settings.MinimumValue.Get<int32>();
                            }
				    	}
				    }
				    return TOptional<int32>();
				})
				.MaxSliderValue_Lambda([HierarchyPtr, InKey, InValueType]() -> TOptional<int32>
				{
				    if(InValueType == ERigControlValueType::Current || InValueType == ERigControlValueType::Initial)
				    {
				    	if(HierarchyPtr.IsValid())
				    	{
				    		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
				    		{
                                return ControlElement->Settings.MaximumValue.Get<int32>();
                            }
				    	}
				    }
				    return TOptional<int32>();
				})
                .Value_Lambda([HierarchyPtr, InKey, InValueType]() -> int32
                {
                	if(HierarchyPtr.IsValid())
                	{
                		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
                		{
                            return HierarchyPtr->GetControlValue(ControlElement, InValueType).Get<int32>();
                        }
                	}
                	return 0;
                })
                .OnValueChanged_Lambda([HierarchyPtr, InKey, InValueType](TOptional<int32> InNewSelection)
                {
                	if(InNewSelection.IsSet())
                	{
                		if(HierarchyPtr.IsValid())
                		{
                			if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
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

void FRigControlElementDetails_SetupFloatValueWidget(IDetailCategoryBuilder& InCategory, const TSharedRef<IPropertyUtilities>& InPropertyUtilities, ERigControlValueType InValueType, const FRigElementKey& InKey, URigHierarchy* InHierarchy)
{
	UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
	UEnum* ValueTypeEnum = StaticEnum<ERigControlValueType>();

	const FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue((int64)InValueType).ToString();
	const FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s Value"), *ValueTypeName));
	TWeakObjectPtr<URigHierarchy> HierarchyPtr = InHierarchy;

	const TAttribute<bool> EnabledAttribute = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([HierarchyPtr, InKey, InValueType]()->bool
    {
        if(HierarchyPtr.IsValid())
        {
            if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
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
            .AllowSpin(InValueType == ERigControlValueType::Current || InValueType == ERigControlValueType::Initial)
            .Value_Lambda([HierarchyPtr, InKey, InValueType]() -> float
            {
            	if(HierarchyPtr.IsValid())
            	{
            		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
            		{
                        return HierarchyPtr->GetControlValue(ControlElement, InValueType).Get<float>();
                    }
            	}
            	return 0.f;
            })
            .MinSliderValue_Lambda([HierarchyPtr, InKey, InValueType]() -> TOptional<float>
            {
                if(InValueType == ERigControlValueType::Current || InValueType == ERigControlValueType::Initial)
                {
                	if(HierarchyPtr.IsValid())
                	{
                		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
                		{
                            return ControlElement->Settings.MinimumValue.Get<float>();
                        }
                	}
                }
                return TOptional<float>();
            })
            .MaxSliderValue_Lambda([HierarchyPtr, InKey, InValueType]() -> TOptional<float>
            {
                if(InValueType == ERigControlValueType::Current || InValueType == ERigControlValueType::Initial)
                {
                	if(HierarchyPtr.IsValid())
                	{
                		if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
                		{
                            return ControlElement->Settings.MaximumValue.Get<float>();
                        }
                	}
                }
                return TOptional<float>();
            })
            .OnValueChanged_Lambda([HierarchyPtr, InKey, InValueType](TOptional<float> InNewSelection)
            {
            	if(InNewSelection.IsSet())
            	{
            		if(HierarchyPtr.IsValid())
            		{
            			if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
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
T FRigControlElementDetails_ExtractValue(const FRigControlValue& InValue)
{
	return InValue.Get<T>();
}

template<typename T>
FRigControlValue FRigControlElementDetails_PackageValue(const T& InValue)
{
	return FRigControlValue::Make<T>(InValue);
}

template<>
FVector2D FRigControlElementDetails_ExtractValue(const FRigControlValue& InValue)
{
	const FVector3f TempValue = InValue.Get<FVector3f>();
	return FVector2D(TempValue.X, TempValue.Y);
}

template<>
FRigControlValue FRigControlElementDetails_PackageValue(const FVector2D& InValue)
{
	const FVector3f TempValue(InValue.X, InValue.Y, 0.f);
	return FRigControlValue::Make<FVector3f>(TempValue);
}

template<>
FVector FRigControlElementDetails_ExtractValue(const FRigControlValue& InValue)
{
	return InValue.Get<FVector3f>();
}

template<>
FRigControlValue FRigControlElementDetails_PackageValue(const FVector& InValue)
{
	return FRigControlValue::Make<FVector3f>(InValue);
}

template<>
FRotator FRigControlElementDetails_ExtractValue(const FRigControlValue& InValue)
{
	return FRotator::MakeFromEuler(InValue.Get<FVector3f>());
}

template<>
FRigControlValue FRigControlElementDetails_PackageValue(const FRotator& InValue)
{
	return FRigControlValue::Make<FVector3f>(InValue.Euler());
}

template<>
FTransform FRigControlElementDetails_ExtractValue(const FRigControlValue& InValue)
{
	return InValue.Get<FRigControlValue::FTransform_Float>().ToTransform();
}

template<>
FRigControlValue FRigControlElementDetails_PackageValue(const FTransform& InValue)
{
	return FRigControlValue::Make<FRigControlValue::FTransform_Float>(InValue);
}

template<>
FTransformNoScale FRigControlElementDetails_ExtractValue(const FRigControlValue& InValue)
{
	return InValue.Get<FRigControlValue::FTransformNoScale_Float>().ToTransform();
}

template<>
FRigControlValue FRigControlElementDetails_PackageValue(const FTransformNoScale& InValue)
{
	return FRigControlValue::Make<FRigControlValue::FTransformNoScale_Float>(InValue);
}

template<>
FEulerTransform FRigControlElementDetails_ExtractValue(const FRigControlValue& InValue)
{
	return InValue.Get<FRigControlValue::FEulerTransform_Float>().ToTransform();
}

template<>
FRigControlValue FRigControlElementDetails_PackageValue(const FEulerTransform& InValue)
{
	return FRigControlValue::Make<FRigControlValue::FEulerTransform_Float>(InValue);
}

template<typename T>
void FRigControlElementDetails_SetupStructValueWidget(IDetailCategoryBuilder& InCategory, const TSharedRef<IPropertyUtilities>& InPropertyUtilities, ERigControlValueType InValueType, const FRigElementKey& InKey, URigHierarchy* InHierarchy)
{
	UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
	UEnum* ValueTypeEnum = StaticEnum<ERigControlValueType>();

	const FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue((int64)InValueType).ToString();
	const FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s Value"), *ValueTypeName));
	const UScriptStruct* ValueStruct = TBaseStructure<T>::Get();

	const TSharedPtr<FStructOnScope> StructToDisplay = MakeShareable(new FStructOnScope(ValueStruct));

	TWeakObjectPtr<URigHierarchy> HierarchyPtr = InHierarchy;

	const TAttribute<EVisibility> VisibilityAttribute = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([HierarchyPtr, InKey, InValueType, StructToDisplay, ValueStruct]()->EVisibility
    {
		if(HierarchyPtr.IsValid())
		{
			if(FRigControlElement* ControlElement = HierarchyPtr->Find<FRigControlElement>(InKey))
			{
				// update the struct with the current control value
				uint8* StructMemory = StructToDisplay->GetStructMemory();
				const FRigControlValue& ControlValue = HierarchyPtr->GetControlValue(ControlElement, InValueType);
				T ExtractedValue = FRigControlElementDetails_ExtractValue<T>(ControlValue);
				ValueStruct->CopyScriptStruct(StructToDisplay->GetStructMemory(), &ExtractedValue, 1);
				
				if(ControlElement->Settings.IsValueTypeEnabled(InValueType))
                {
                    return EVisibility::Visible;
                }
            }
		}
		return EVisibility::Hidden;
    }));

	IDetailPropertyRow* Row = InCategory.AddExternalStructure(StructToDisplay.ToSharedRef());
	Row->DisplayName(PropertyLabel);
	Row->ShouldAutoExpand(true);
	Row->Visibility(VisibilityAttribute);

	TSharedPtr<SWidget> NameWidget, ValueWidget;
	Row->GetDefaultWidgets(NameWidget, ValueWidget);
	
	const FSimpleDelegate OnStructContentsChangedDelegate = FSimpleDelegate::CreateLambda([HierarchyPtr, InKey, StructToDisplay, InValueType]()
	{
		if(HierarchyPtr.IsValid())
		{
            const FRigControlValue ControlValue = FRigControlElementDetails_PackageValue(*(T*)StructToDisplay->GetStructMemory());
			HierarchyPtr->SetControlValue(InKey, ControlValue, InValueType, true, true);
			if(InValueType == ERigControlValueType::Initial)
			{
                if(UControlRigBlueprint* Blueprint = RigElementDetails_GetBlueprintFromHierarchy(HierarchyPtr.Get()))
                {
                    Blueprint->Hierarchy->SetControlValue(InKey, ControlValue, InValueType, true);
                }
            }
		}
	});

	TSharedPtr<IPropertyHandle> Handle = Row->GetPropertyHandle();
	Handle->SetOnPropertyValueChanged(OnStructContentsChangedDelegate);
	Handle->SetOnChildPropertyValueChanged(OnStructContentsChangedDelegate);
}

void FRigControlElementDetails_SetupValueWidget(IDetailCategoryBuilder& InCategory, const TSharedRef<IPropertyUtilities>& InPropertyUtilities, ERigControlValueType InValueType, const FRigElementKey& InKey, URigHierarchy* InHierarchy)
{
	const FRigControlElement* ControlElement = InHierarchy->Find<FRigControlElement>(InKey);
	if(ControlElement == nullptr)
	{
		return;
	}
	
	switch(ControlElement->Settings.ControlType)
	{
		case ERigControlType::Bool:
		{
			if((InValueType == ERigControlValueType::Minimum) || (InValueType == ERigControlValueType::Maximum))
			{
				return;
			}
			FRigControlElementDetails_SetupBoolValueWidget(InCategory, InPropertyUtilities, InValueType, InKey, InHierarchy);
			break;
		}
		case ERigControlType::Integer:
		{
			FRigControlElementDetails_SetupIntegerValueWidget(InCategory, InPropertyUtilities, InValueType, InKey, InHierarchy);
			break;
		}
		case ERigControlType::Float:
		{
			FRigControlElementDetails_SetupFloatValueWidget(InCategory, InPropertyUtilities, InValueType, InKey, InHierarchy);
			break;
		}
		case ERigControlType::Vector2D:
		{
			FRigControlElementDetails_SetupStructValueWidget<FVector2D>(InCategory, InPropertyUtilities, InValueType, InKey, InHierarchy);
			break;
		}
		case ERigControlType::Position:
		case ERigControlType::Scale:
		{
			FRigControlElementDetails_SetupStructValueWidget<FVector>(InCategory, InPropertyUtilities, InValueType, InKey, InHierarchy);
			break;
		}
		case ERigControlType::Rotator:
		{
			FRigControlElementDetails_SetupStructValueWidget<FRotator>(InCategory, InPropertyUtilities, InValueType, InKey, InHierarchy);
			break;
		}
		case ERigControlType::TransformNoScale:
		{
			FRigControlElementDetails_SetupStructValueWidget<FTransformNoScale>(InCategory, InPropertyUtilities, InValueType, InKey, InHierarchy);
			break;
		}
		case ERigControlType::EulerTransform:
		{
			FRigControlElementDetails_SetupStructValueWidget<FEulerTransform>(InCategory, InPropertyUtilities, InValueType, InKey, InHierarchy);
			break;
		}
		case ERigControlType::Transform:
		{
			FRigControlElementDetails_SetupStructValueWidget<FTransform>(InCategory, InPropertyUtilities, InValueType, InKey, InHierarchy);
			break;
		}
		default:
		{
			ensure(false);
			break;
		}
	}
}

void FRigBoneElementDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FRigTransformElementDetails::CustomizeDetails(DetailBuilder);
	CustomizeTransform(DetailBuilder);
}

TArray<TSharedPtr<FString>> FRigControlElementDetails::ControlTypeList;

void FRigControlElementDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FRigTransformElementDetails::CustomizeDetails(DetailBuilder);

	ShapeNameList.Reset();
	if (BlueprintBeingCustomized)
	{
		bool bUseNameSpace = BlueprintBeingCustomized->ShapeLibraries.Num() > 1;
		for(TSoftObjectPtr<UControlRigShapeLibrary>& ShapeLibrary : BlueprintBeingCustomized->ShapeLibraries)
		{
			if (!ShapeLibrary.IsValid())
			{
				ShapeLibrary.LoadSynchronous();
			}
			if (ShapeLibrary.IsValid())
			{
				const FString NameSpace = bUseNameSpace ? ShapeLibrary->GetName() + TEXT(".") : FString();
				ShapeNameList.Add(MakeShared<FString>(NameSpace + ShapeLibrary->DefaultShape.ShapeName.ToString()));
				for (const FControlRigShapeDefinition& Shape : ShapeLibrary->Shapes)
				{
					ShapeNameList.Add(MakeShared<FString>(NameSpace + Shape.ShapeName.ToString()));
				}
			}
		}
	}

	if (HierarchyBeingCustomized == nullptr)
	{
		return;
	}

	IDetailCategoryBuilder& ControlCategory = DetailBuilder.EditCategory(TEXT("Control"), LOCTEXT("Control", "Control"));
	IDetailCategoryBuilder& ShapeCategory = DetailBuilder.EditCategory(TEXT("Shape"), LOCTEXT("Shape", "Shape"));
	IDetailCategoryBuilder& LimitsCategory = DetailBuilder.EditCategory(TEXT("Limits"), LOCTEXT("Limits", "Limits"));

	// the transform category should show up after the control, shape and limits categories
	CustomizeTransform(DetailBuilder);

	const TSharedPtr<IPropertyHandle> SettingsHandle = DetailBuilder.GetProperty(TEXT("Settings"));
	const TSharedPtr<IPropertyHandle> DisplayNameHandle = SettingsHandle->GetChildHandle(TEXT("DisplayName"));

	DetailBuilder.HideProperty(SettingsHandle);

	ControlCategory.AddCustomRow(LOCTEXT("DisplayName", "Display Name"))
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

	const TSharedRef<IPropertyUtilities> PropertyUtilities = DetailBuilder.GetPropertyUtilities();

	// when control type changes, we have to refresh detail panel
	const TSharedPtr<IPropertyHandle> ControlTypeHandle = SettingsHandle->GetChildHandle(TEXT("ControlType"));
	ControlTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda(
		[this, PropertyUtilities]()
		{
			TArray<FRigControlElement> ControlElementsInView = GetElementsInDetailsView<FRigControlElement>();
			TArray<FRigControlElement*> ControlElementsInHierarchy = GetElementsInHierarchy<FRigControlElement>();
			check(ControlElementsInView.Num() == ControlElementsInHierarchy.Num());

			if (this->HierarchyBeingCustomized && ControlElementsInHierarchy.Num() > 0)
			{
				for(int32 ControlIndex = 0; ControlIndex< ControlElementsInView.Num(); ControlIndex++)
				{
					const FRigControlElement& ViewElement = ControlElementsInView[ControlIndex];
					FRigControlElement* ControlElement = ControlElementsInHierarchy[ControlIndex];
					
					FRigControlValue ValueToSet;

					ControlElement->Settings.ControlType = ViewElement.Settings.ControlType;
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

					ObjectsBeingCustomized[ControlIndex]->SetContent<FRigControlElement>(*ControlElement);

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
				
				PropertyUtilities->ForceRefresh();
			}
		}
	));

	ControlCategory.AddProperty(ControlTypeHandle.ToSharedRef());
	ControlCategory.AddProperty(SettingsHandle->GetChildHandle(TEXT("bAnimatable")).ToSharedRef());

	// any but bool controls show the offset + shape + limits
	const bool bNeedsShapeProperties = IsAnyControlNotOfType(ERigControlType::Bool);
	if (bNeedsShapeProperties)
	{
		// setup offset
		{
			const TSharedPtr<IPropertyHandle> OffsetHandle = DetailBuilder.GetProperty(TEXT("Offset"));
			const TSharedPtr<IPropertyHandle> InitialHandle = OffsetHandle->GetChildHandle(TEXT("Initial"));
			const TSharedPtr<IPropertyHandle> LocalHandle = InitialHandle->GetChildHandle(TEXT("Local"));
			const TSharedPtr<IPropertyHandle> TransformHandle = LocalHandle->GetChildHandle(TEXT("Transform"));
			ControlCategory.AddProperty(TransformHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Offset Transform")));
		}
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
		LimitsCategory.AddProperty(LimitHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Limit Translation")));
	}

	if(IsAnyControlOfType(ERigControlType::Rotator) ||
		IsAnyControlOfType(ERigControlType::Transform) ||
		IsAnyControlOfType(ERigControlType::TransformNoScale) ||
		IsAnyControlOfType(ERigControlType::EulerTransform))
	{
		const TSharedPtr<IPropertyHandle> LimitHandle = SettingsHandle->GetChildHandle(TEXT("bLimitRotation"));
		LimitsCategory.AddProperty(LimitHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Limit Rotation")));
	}

	if(IsAnyControlOfType(ERigControlType::Scale) ||
		IsAnyControlOfType(ERigControlType::Transform) ||
		IsAnyControlOfType(ERigControlType::EulerTransform))
	{
		const TSharedPtr<IPropertyHandle> LimitHandle = SettingsHandle->GetChildHandle(TEXT("bLimitScale"));
		LimitsCategory.AddProperty(LimitHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Limit Scale")));
	}

	if(!(IsAnyControlNotOfType(ERigControlType::Integer) &&
		IsAnyControlNotOfType(ERigControlType::Float) &&
		IsAnyControlNotOfType(ERigControlType::Vector2D)))
	{
		const TSharedPtr<IPropertyHandle> PrimaryAxisHandle = SettingsHandle->GetChildHandle(TEXT("PrimaryAxis"));
		ControlCategory.AddProperty(PrimaryAxisHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Primary Axis")));
	}

	if (bNeedsShapeProperties)
	{
		const TSharedPtr<IPropertyHandle> DrawLimitsHandle = SettingsHandle->GetChildHandle(TEXT("bDrawLimits"));
		LimitsCategory.AddProperty(DrawLimitsHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Draw Limits")));
	}

	TArray<FRigControlElement> ControlElements;
	TArray<UDetailsViewWrapperObject*> ObjectPerControl;
	for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(ObjectBeingCustomized.IsValid())
		{
			if(ObjectBeingCustomized->IsChildOf<FRigControlElement>())
			{
				ControlElements.Add(ObjectBeingCustomized->GetContent<FRigControlElement>());
				ObjectPerControl.Add(ObjectBeingCustomized.Get());
			}
		}
	}

	// only setup value widgets if there is only ony control selected
	if(ControlElements.Num() == 1)
	{
		URigHierarchy* HierarchyBeingDebugged = HierarchyBeingCustomized;
		
		if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
		{
			if(!DebuggedRig->IsSetupModeEnabled())
			{
				HierarchyBeingDebugged = DebuggedRig->GetHierarchy();
			}
		}
		
		FRigControlElementDetails_SetupValueWidget(ControlCategory, PropertyUtilities, ERigControlValueType::Current, ControlElements[0].GetKey(), HierarchyBeingDebugged);

		switch (ControlElements[0].Settings.ControlType)
		{
			case ERigControlType::Bool:
			case ERigControlType::Float:
			case ERigControlType::Integer:
			case ERigControlType::Vector2D:
			{
				FRigControlElementDetails_SetupValueWidget(ControlCategory, PropertyUtilities, ERigControlValueType::Initial,ControlElements[0].GetKey(), HierarchyBeingCustomized);
				break;
			}
			default:
			{
				break;
			}
		}
		
		
		FRigControlElementDetails_SetupValueWidget(LimitsCategory, PropertyUtilities, ERigControlValueType::Minimum, ControlElements[0].GetKey(), HierarchyBeingCustomized);
		FRigControlElementDetails_SetupValueWidget(LimitsCategory, PropertyUtilities, ERigControlValueType::Maximum, ControlElements[0].GetKey(), HierarchyBeingCustomized);
	}

	if (bNeedsShapeProperties)
	{
		ShapeCategory.AddProperty(SettingsHandle->GetChildHandle(TEXT("bShapeEnabled")).ToSharedRef())
		.DisplayName(FText::FromString(TEXT("Enabled")));
		ShapeCategory.AddProperty(SettingsHandle->GetChildHandle(TEXT("bShapeVisible")).ToSharedRef())
		.DisplayName(FText::FromString(TEXT("Visible")));

		IDetailGroup& ShapeProperitesGroup = ShapeCategory.AddGroup(TEXT("Shape Properties"), LOCTEXT("ShapeProperties", "Shape Properties"));
		ShapeProperitesGroup.HeaderRow().NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("ShapeProperties", "Shape Properties"))
			.ToolTipText(LOCTEXT("ShapeProperties", "Customize the properties of the shape"))
		]
		.CopyAction(FUIAction(
			FExecuteAction::CreateSP(this, &FRigControlElementDetails::OnCopyShapeProperties)))
		.PasteAction(FUIAction(
			FExecuteAction::CreateSP(this, &FRigControlElementDetails::OnPasteShapeProperties)));
		
		{
			// setup shape transform
			{
				TSharedPtr<IPropertyHandle> ShapeHandle = DetailBuilder.GetProperty(TEXT("Shape"));
				TSharedPtr<IPropertyHandle> InitialHandle = ShapeHandle->GetChildHandle(TEXT("Initial"));
				TSharedPtr<IPropertyHandle> LocalHandle = InitialHandle->GetChildHandle(TEXT("Local"));
				ShapeTransformHandle = LocalHandle->GetChildHandle(TEXT("Transform"));
				ShapeProperitesGroup.AddPropertyRow(ShapeTransformHandle.ToSharedRef())
				.IsEnabled(TAttribute<bool>::CreateSP(this, &FRigControlElementDetails::IsShapeEnabled));
			}

			ShapeNameHandle = SettingsHandle->GetChildHandle(TEXT("ShapeName"));
			ShapeProperitesGroup.AddPropertyRow(ShapeNameHandle.ToSharedRef()).CustomWidget()
			.NameContent()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Shape")))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.IsEnabled(this, &FRigControlElementDetails::IsShapeEnabled)
			]
			.ValueContent()
			[
				SNew(SControlRigShapeNameList, ControlElements, BlueprintBeingCustomized)
				.OnGetNameListContent(this, &FRigControlElementDetails::GetShapeNameList)
				.IsEnabled(this, &FRigControlElementDetails::IsShapeEnabled)
			];

			ShapeColorHandle = SettingsHandle->GetChildHandle(TEXT("ShapeColor"));
			ShapeProperitesGroup.AddPropertyRow(ShapeColorHandle.ToSharedRef())
			.DisplayName(FText::FromString(TEXT("Color")));
		}
	}

	if(IsAnyControlOfType(ERigControlType::Integer))
	{
		const TSharedPtr<IPropertyHandle> ControlEnumHandle = SettingsHandle->GetChildHandle(TEXT("ControlEnum"));
		ControlCategory.AddProperty(ControlEnumHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Control Enum")));

		ControlEnumHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda(
			[this, PropertyUtilities, ControlElements, ObjectPerControl]()
			{
				PropertyUtilities->ForceRefresh();

				if (this->HierarchyBeingCustomized != nullptr)
				{
					for(int32 ControlIndex = 0; ControlIndex < ControlElements.Num(); ControlIndex++)
					{
						const FRigControlElement ControlInView = ControlElements[ControlIndex];
						FRigControlElement* ControlBeingCustomized = HierarchyBeingCustomized->Find<FRigControlElement>(ControlInView.GetKey());
						
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

						ObjectPerControl[ControlIndex]->SetContent<FRigControlElement>(*ControlBeingCustomized);
					}
				}
			}
		));
		
	}
	
	IDetailCategoryBuilder& AnimationCategory = DetailBuilder.EditCategory(TEXT("Animation"), LOCTEXT("Animation", "Animation"));
	AnimationCategory.AddProperty(SettingsHandle->GetChildHandle(TEXT("Customization"))->GetChildHandle(TEXT("AvailableSpaces")).ToSharedRef());
}

void FRigControlElementDetails::RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule, UClass* InClass)
{
	FRigTransformElementDetails::RegisterSectionMappings(PropertyEditorModule, InClass);

	TSharedRef<FPropertySection> ControlSection = PropertyEditorModule.FindOrCreateSection(InClass->GetFName(), "Control", LOCTEXT("Control", "Control"));
	ControlSection->AddCategory("General");
	ControlSection->AddCategory("Control");
	ControlSection->AddCategory("Animation");

	TSharedRef<FPropertySection> ShapeSection = PropertyEditorModule.FindOrCreateSection(InClass->GetFName(), "Shape", LOCTEXT("Shape", "Shape"));
	ShapeSection->AddCategory("General");
	ShapeSection->AddCategory("Shape");

	TSharedRef<FPropertySection> LimitsSection = PropertyEditorModule.FindOrCreateSection(InClass->GetFName(), "Limits", LOCTEXT("Limits", "Limits"));
	LimitsSection->AddCategory("General");
	LimitsSection->AddCategory("Limits");
}

bool FRigControlElementDetails::IsShapeEnabled() const
{
	for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
	{
		if(ObjectBeingCustomized.IsValid())
		{
			if(ObjectBeingCustomized->IsChildOf<FRigControlElement>())
			{
				const FRigControlElement ControlElement = ObjectBeingCustomized->GetContent<FRigControlElement>();
				if(ControlElement.Settings.bShapeEnabled)
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
			if(ObjectBeingCustomized->IsChildOf<FRigControlElement>())
			{
				const FRigControlElement ControlElement = ObjectBeingCustomized->GetContent<FRigControlElement>();
				
				switch (InValueType)
				{
					case ERigControlValueType::Minimum:
					case ERigControlValueType::Maximum:
					{
						if(ControlElement.Settings.bLimitTranslation || ControlElement.Settings.bLimitRotation || ControlElement.Settings.bLimitScale)
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

const TArray<TSharedPtr<FString>>& FRigControlElementDetails::GetShapeNameList() const
{
	return ShapeNameList;
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
			if(ObjectBeingCustomized->IsChildOf<FRigControlElement>())
			{
				const FRigControlElement ControlElement = ObjectBeingCustomized->GetContent<FRigControlElement>();
				if(ObjectIndex == 0)
				{
					DisplayName = ControlElement.Settings.DisplayName;
				}
				else if(DisplayName != ControlElement.Settings.DisplayName)
				{
					return ControlRigDetailsMultipleValues;
				}
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
	if(InCommitType == ETextCommit::OnCleared)
	{
		return;
	}
	
	const FName DisplayName = InNewText.IsEmpty() ? FName(NAME_None) : FName(*InNewText.ToString());
	
	for(int32 ObjectIndex = 0; ObjectIndex < ObjectsBeingCustomized.Num(); ObjectIndex++)
	{
		TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized = ObjectsBeingCustomized[ObjectIndex];
		if(ObjectBeingCustomized.IsValid())
		{
			if(ObjectBeingCustomized->IsChildOf<FRigControlElement>())
			{
				FRigControlElement ControlElement = ObjectBeingCustomized->GetContent<FRigControlElement>();
				ControlElement.Settings.DisplayName = DisplayName;
				ObjectBeingCustomized->SetContent<FRigControlElement>(ControlElement);

				if(GetHierarchy())
				{
					GetHierarchy()->SetControlSettings(ControlElement.GetKey(), ControlElement.Settings, false, false, true);
				}
			}
		}
	}
}

void FRigControlElementDetails::OnCopyShapeProperties()
{
	FString Value;
	if (ObjectsBeingCustomized.Num() > 0)
	{
		if(ObjectsBeingCustomized[0].IsValid())
		{
			if(ObjectsBeingCustomized[0]->IsChildOf<FRigControlElement>())
			{
				const FRigControlElement ControlElement = ObjectsBeingCustomized[0]->GetContent<FRigControlElement>();

				Value = FString::Printf(TEXT("(ShapeName=\"%s\",ShapeColor=%s,Transform=%s)"),
					*ControlElement.Settings.ShapeName.ToString(),
					*ControlElement.Settings.ShapeColor.ToString(),
					*ControlElement.Shape.Initial.Local.Transform.ToString());
			}
		}
	}
		
	if (!Value.IsEmpty())
	{
		// Copy.
		FPlatformApplicationMisc::ClipboardCopy(*Value);
	}
}

void FRigControlElementDetails::OnPasteShapeProperties()
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	
	FString TrimmedText = PastedText.LeftChop(1).RightChop(1);
	FString ShapeName;
	FString ShapeColorStr;
	FString TransformStr;
	bool bSuccessful = FParse::Value(*TrimmedText, TEXT("ShapeName="), ShapeName) &&
					   FParse::Value(*TrimmedText, TEXT("ShapeColor="), ShapeColorStr, false) &&
					   FParse::Value(*TrimmedText, TEXT("Transform="), TransformStr, false);

	if (bSuccessful)
	{
		FScopedTransaction Transaction(LOCTEXT("PasteShape", "Paste Shape"));
		
		// Name
		{
			ShapeNameHandle->NotifyPreChange();
			ShapeNameHandle->SetValue(ShapeName);
			ShapeNameHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		}
		
		// Color
		{
			ShapeColorHandle->NotifyPreChange();
			TArray<void*> RawDataPtrs;
			ShapeColorHandle->AccessRawData(RawDataPtrs);
			for (void* RawPtr: RawDataPtrs)
			{
				bSuccessful &= static_cast<FLinearColor*>(RawPtr)->InitFromString(ShapeColorStr);
				if (!bSuccessful)
				{
					Transaction.Cancel();
					return;
				}
			}		
			ShapeColorHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		}

		// Transform
		{
			ShapeTransformHandle->NotifyPreChange();
			TArray<void*> RawDataPtrs;
			ShapeTransformHandle->AccessRawData(RawDataPtrs);
			for (void* RawPtr: RawDataPtrs)
			{
				bSuccessful &= static_cast<FTransform*>(RawPtr)->InitFromString(TransformStr);
				if (!bSuccessful)
				{
					Transaction.Cancel();
					return;
				}
			}		
			ShapeTransformHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		}		
	}
}

void FRigNullElementDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FRigTransformElementDetails::CustomizeDetails(DetailBuilder);
	CustomizeTransform(DetailBuilder);
}

#undef LOCTEXT_NAMESPACE
