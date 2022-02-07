// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigElementDetails.h"
#include "Widgets/SWidget.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SCheckBox.h"
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

#define LOCTEXT_NAMESPACE "ControlRigElementDetails"

static const FText ControlRigDetailsMultipleValues = LOCTEXT("MultipleValues", "Multiple Values");

struct FRigElementTransformWidgetSettings
{
	FRigElementTransformWidgetSettings()
	: RotationRepresentation(MakeShareable(new ESlateRotationRepresentation::Type(ESlateRotationRepresentation::Rotator)))
	, IsComponentRelative(MakeShareable(new UE::Math::TVector<float>(1.f, 1.f, 1.f)))
	, IsScaleLocked(TSharedPtr<bool>(new bool(false)))
	{
	}

	TSharedPtr<ESlateRotationRepresentation::Type> RotationRepresentation;
	TSharedRef<UE::Math::TVector<float>> IsComponentRelative;
	TSharedPtr<bool> IsScaleLocked;

	static FRigElementTransformWidgetSettings& FindOrAdd(
		ERigControlValueType InValueType,
		ERigTransformElementDetailsTransform::Type InTransformType,
		const SAdvancedTransformInputBox<FEulerTransform>::FArguments& WidgetArgs)
	{
		uint32 Hash = GetTypeHash(WidgetArgs._ConstructLocation);
		Hash = HashCombine(Hash, GetTypeHash(WidgetArgs._ConstructRotation));
		Hash = HashCombine(Hash, GetTypeHash(WidgetArgs._ConstructScale));
		Hash = HashCombine(Hash, GetTypeHash(WidgetArgs._AllowEditRotationRepresentation));
		Hash = HashCombine(Hash, GetTypeHash(WidgetArgs._DisplayScaleLock));
		Hash = HashCombine(Hash, GetTypeHash(InValueType));
		Hash = HashCombine(Hash, GetTypeHash(InTransformType));
		return sSettings.FindOrAdd(Hash);
	}

	static TMap<uint32, FRigElementTransformWidgetSettings> sSettings;
};

TMap<uint32, FRigElementTransformWidgetSettings> FRigElementTransformWidgetSettings::sSettings;


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
				.ToolTipText(NSLOCTEXT("ControlRigElementDetails", "ObjectGraphPin_Use_Tooltip", "Use item selected"))
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
				.ToolTipText(NSLOCTEXT("ControlRigElementDetails", "ObjectGraphPin_Browse_Tooltip", "Select in hierarchy"))
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

URigHierarchy* FRigBaseElementDetails::GetHierarchyBeingDebugged() const
{
	URigHierarchy* HierarchyBeingDebugged = HierarchyBeingCustomized;
		
	if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
	{
		if(!DebuggedRig->IsSetupModeEnabled())
		{
			HierarchyBeingDebugged = DebuggedRig->GetHierarchy();
		}
	}

	return HierarchyBeingDebugged;
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

TSharedPtr<TArray<ERigTransformElementDetailsTransform::Type>> FRigTransformElementDetails::PickedTransforms;

void FRigTransformElementDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FRigBaseElementDetails::CustomizeDetails(DetailBuilder);
}

void FRigTransformElementDetails::RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule, UClass* InClass)
{
	FRigBaseElementDetails::RegisterSectionMappings(PropertyEditorModule, InClass);
	
	TSharedRef<FPropertySection> TransformSection = PropertyEditorModule.FindOrCreateSection(InClass->GetFName(), "Transform", LOCTEXT("Transform", "Transform"));
	TransformSection->AddCategory("General");
	TransformSection->AddCategory("Value");
	TransformSection->AddCategory("Transform");
}

void FRigTransformElementDetails::CustomizeTransform(IDetailLayoutBuilder& DetailBuilder)
{
	URigHierarchy* HierarchyBeingDebugged = HierarchyBeingCustomized;
	if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
	{
 		if(!DebuggedRig->IsSetupModeEnabled())
		{
			HierarchyBeingDebugged = DebuggedRig->GetHierarchy();
		}
	}

	TArray<FRigElementKey> Keys = GetElementKeys();
	Keys = HierarchyBeingDebugged->SortKeys(Keys);
	const bool bAllControls = !IsAnyElementNotOfType(ERigElementType::Control) && !IsAnyControlOfType(ERigControlType::Bool);
	bool bShowLimits = false;
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

		bShowLimits = !IsAnyControlNotOfType(ERigControlType::EulerTransform);

		if(bShowLimits)
		{
			TransformTypes.Append({
				ERigTransformElementDetailsTransform::Minimum,
				ERigTransformElementDetailsTransform::Maximum
			});
			ButtonLabels.Append({
				LOCTEXT("Min", "Min"),
				LOCTEXT("Max", "Max")
			});
			ButtonTooltips.Append({
				LOCTEXT("ValueMinimumTooltip", "The minimum limit(s) for the control"),
				LOCTEXT("ValueMaximumTooltip", "The maximum limit(s) for the control")
			});
		}
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
		const ERigTransformElementDetailsTransform::Type CurrentTransformType = TransformTypes[Index];

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

	if(!PickedTransforms.IsValid())
	{
		PickedTransforms = MakeShareable(new TArray<ERigTransformElementDetailsTransform::Type>({ERigTransformElementDetailsTransform::Current}));
	}

	TSharedPtr<SSegmentedControl<ERigTransformElementDetailsTransform::Type>> TransformChoiceWidget =
		SSegmentedControl<ERigTransformElementDetailsTransform::Type>::Create(
			TransformTypes,
			ButtonLabels,
			ButtonTooltips,
			*PickedTransforms.Get(),
			true,
			SSegmentedControl<ERigTransformElementDetailsTransform::Type>::FOnValuesChanged::CreateLambda(
				[](TArray<ERigTransformElementDetailsTransform::Type> NewSelection)
				{
					(*FRigTransformElementDetails::PickedTransforms.Get()) = NewSelection;
				}
			)
		);

	IDetailCategoryBuilder& TransformCategory = DetailBuilder.EditCategory(TEXT("Transform"), LOCTEXT("Transform", "Transform"));
	AddChoiceWidgetRow(TransformCategory, FText::FromString(TEXT("TransformType")), TransformChoiceWidget.ToSharedRef());


	SAdvancedTransformInputBox<FEulerTransform>::FArguments TransformWidgetArgs = SAdvancedTransformInputBox<FEulerTransform>::FArguments()
	.DisplayToggle(false)
	.DisplayRelativeWorld(true)
	.Font(IDetailLayoutBuilder::GetDetailFont());

	for(int32 Index = 0; Index < ButtonLabels.Num(); Index++)
	{
		const ERigTransformElementDetailsTransform::Type CurrentTransformType = TransformTypes[Index];
		ERigControlValueType CurrentValueType = ERigControlValueType::Current;
		switch(CurrentTransformType)
		{
			case ERigTransformElementDetailsTransform::Initial:
			{
				CurrentValueType = ERigControlValueType::Initial;
				break;
			}
			case ERigTransformElementDetailsTransform::Minimum:
			{
				CurrentValueType = ERigControlValueType::Minimum;
				break;
			}
			case ERigTransformElementDetailsTransform::Maximum:
			{
				CurrentValueType = ERigControlValueType::Maximum;
				break;
			}
		}

		TransformWidgetArgs.Visibility_Lambda([TransformChoiceWidget, Index]() -> EVisibility
		{
			return TransformChoiceWidget->HasValue((ERigTransformElementDetailsTransform::Type)Index) ? EVisibility::Visible : EVisibility::Collapsed;
		});

		TransformWidgetArgs.IsEnabled(bTransformsEnabled[Index]);

		CreateEulerTransformValueWidgetRow(
			HierarchyBeingDebugged,
			Keys,
			TransformWidgetArgs,
			TransformCategory,
			ButtonLabels[Index],
			ButtonTooltips[Index],
			CurrentTransformType,
			CurrentValueType);
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

TSharedPtr<TArray<ERigControlValueType>> FRigControlElementDetails::PickedValueTypes;

void FRigTransformElementDetails::CreateEulerTransformValueWidgetRow(
	URigHierarchy* HierarchyBeingDebugged,
	const TArray<FRigElementKey>& Keys,
	SAdvancedTransformInputBox<FEulerTransform>::FArguments TransformWidgetArgs,
	IDetailCategoryBuilder& CategoryBuilder,
	const FText& Label,
	const FText& Tooltip,
	ERigTransformElementDetailsTransform::Type CurrentTransformType,
	ERigControlValueType ValueType)
{
	const FRigElementTransformWidgetSettings& Settings = FRigElementTransformWidgetSettings::FindOrAdd(ValueType, CurrentTransformType, TransformWidgetArgs);
	TSharedRef<UE::Math::TVector<float>> IsComponentRelativeStorage = Settings.IsComponentRelative;
	
	TransformWidgetArgs.OnGetIsComponentRelative_Lambda(
		[IsComponentRelativeStorage](ESlateTransformComponent::Type InComponent)
		{
			return IsComponentRelativeStorage->operator[]((int32)InComponent) > 0.f;
		})
	.OnIsComponentRelativeChanged_Lambda(
		[IsComponentRelativeStorage](ESlateTransformComponent::Type InComponent, bool bIsRelative)
		{
			IsComponentRelativeStorage->operator[]((int32)InComponent) = bIsRelative ? 1.f : 0.f;
		});

	const TSharedPtr<ESlateRotationRepresentation::Type> RotationRepresentationStorage = Settings.RotationRepresentation;
	TransformWidgetArgs.RotationRepresentation(RotationRepresentationStorage);
	
	auto IsComponentRelative = [TransformWidgetArgs](int32 Component) -> bool
	{
		if(TransformWidgetArgs._OnGetIsComponentRelative.IsBound())
		{
			return TransformWidgetArgs._OnGetIsComponentRelative.Execute((ESlateTransformComponent::Type)Component);
		}
		return true;
	};

	auto ConformComponentRelative = [TransformWidgetArgs, IsComponentRelative](int32 Component)
	{
		if(TransformWidgetArgs._OnIsComponentRelativeChanged.IsBound())
		{
			bool bRelative = IsComponentRelative(Component);
			TransformWidgetArgs._OnIsComponentRelativeChanged.Execute(ESlateTransformComponent::Location, bRelative);
			TransformWidgetArgs._OnIsComponentRelativeChanged.Execute(ESlateTransformComponent::Rotation, bRelative);
			TransformWidgetArgs._OnIsComponentRelativeChanged.Execute(ESlateTransformComponent::Scale, bRelative);
		}
	};

	TransformWidgetArgs.IsScaleLocked(Settings.IsScaleLocked);

	switch(CurrentTransformType)
	{
		case ERigTransformElementDetailsTransform::Minimum:
		case ERigTransformElementDetailsTransform::Maximum:
		{
			TransformWidgetArgs.AllowEditRotationRepresentation(false);
			TransformWidgetArgs.DisplayRelativeWorld(false);
			TransformWidgetArgs.DisplayToggle(true);
			TransformWidgetArgs.OnGetToggleChecked_Lambda([Keys, HierarchyBeingDebugged, ValueType]
				(
					ESlateTransformComponent::Type Component,
					ESlateRotationRepresentation::Type RotationRepresentation,
					ESlateTransformSubComponent::Type SubComponent
				) -> ECheckBoxState
				{
					TOptional<bool> FirstValue;

					for(const FRigElementKey& Key : Keys)
					{
						if(const FRigControlElement* ControlElement = HierarchyBeingDebugged->Find<FRigControlElement>(Key))
						{
							TOptional<bool> Value;

							switch(ControlElement->Settings.ControlType)
							{
								case ERigControlType::Position:
								case ERigControlType::Rotator:
								case ERigControlType::Scale:
								{
									if(ControlElement->Settings.LimitEnabled.Num() == 3)
									{
										const int32 Index = ControlElement->Settings.ControlType == ERigControlType::Rotator ?
											int32(SubComponent) - int32(ESlateTransformSubComponent::Pitch) :
											int32(SubComponent) - int32(ESlateTransformSubComponent::X);

										Value = ControlElement->Settings.LimitEnabled[Index].GetForValueType(ValueType);
									}
									break;
								}
								case ERigControlType::EulerTransform:
								{
									if(ControlElement->Settings.LimitEnabled.Num() == 9)
									{
										switch(Component)
										{
											case ESlateTransformComponent::Location:
											{
												switch(SubComponent)
												{
													case ESlateTransformSubComponent::X:
													{
														Value = ControlElement->Settings.LimitEnabled[0].GetForValueType(ValueType);
														break;
													}
													case ESlateTransformSubComponent::Y:
													{
														Value = ControlElement->Settings.LimitEnabled[1].GetForValueType(ValueType);
														break;
													}
													case ESlateTransformSubComponent::Z:
													{
														Value = ControlElement->Settings.LimitEnabled[2].GetForValueType(ValueType);
														break;
													}
													default:
													{
														break;
													}
												}
												break;
											}
											case ESlateTransformComponent::Rotation:
											{
												switch(SubComponent)
												{
													case ESlateTransformSubComponent::Pitch:
													{
														Value = ControlElement->Settings.LimitEnabled[3].GetForValueType(ValueType);
														break;
													}
													case ESlateTransformSubComponent::Yaw:
													{
														Value = ControlElement->Settings.LimitEnabled[4].GetForValueType(ValueType);
														break;
													}
													case ESlateTransformSubComponent::Roll:
													{
														Value = ControlElement->Settings.LimitEnabled[5].GetForValueType(ValueType);
														break;
													}
													default:
													{
														break;
													}
												}
												break;
											}
											case ESlateTransformComponent::Scale:
											{
												switch(SubComponent)
												{
													case ESlateTransformSubComponent::X:
													{
														Value = ControlElement->Settings.LimitEnabled[6].GetForValueType(ValueType);
														break;
													}
													case ESlateTransformSubComponent::Y:
													{
														Value = ControlElement->Settings.LimitEnabled[7].GetForValueType(ValueType);
														break;
													}
													case ESlateTransformSubComponent::Z:
													{
														Value = ControlElement->Settings.LimitEnabled[8].GetForValueType(ValueType);
														break;
													}
													default:
													{
														break;
													}
												}
												break;
											}
										}
									}
									break;
								}
							}

							if(Value.IsSet())
							{
								if(FirstValue.IsSet())
								{
									if(FirstValue.GetValue() != Value.GetValue())
									{
										return ECheckBoxState::Undetermined;
									}
								}
								else
								{
									FirstValue = Value;
								}
							}
						}
					}

					if(!ensure(FirstValue.IsSet()))
					{
						return ECheckBoxState::Undetermined;
					}
					return FirstValue.GetValue() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				});
				
			TransformWidgetArgs.OnToggleChanged_Lambda([ValueType, Keys, this]
			(
				ESlateTransformComponent::Type Component,
				ESlateRotationRepresentation::Type RotationRepresentation,
				ESlateTransformSubComponent::Type SubComponent,
				ECheckBoxState CheckState
			)
			{
				if(CheckState == ECheckBoxState::Undetermined)
				{
					return;
				}

				const bool Value = CheckState == ECheckBoxState::Checked;

				FScopedTransaction Transaction(LOCTEXT("ChangeLimitToggle", "Change Limit Toggle"));
				HierarchyBeingCustomized->Modify();

				for(const FRigElementKey& Key : Keys)
				{
					if(FRigControlElement* ControlElement = HierarchyBeingCustomized->Find<FRigControlElement>(Key))
					{
						switch(ControlElement->Settings.ControlType)
						{
							case ERigControlType::Position:
							case ERigControlType::Rotator:
							case ERigControlType::Scale:
							{
								if(ControlElement->Settings.LimitEnabled.Num() == 3)
								{
									const int32 Index = ControlElement->Settings.ControlType == ERigControlType::Rotator ?
										int32(SubComponent) - int32(ESlateTransformSubComponent::Pitch) :
										int32(SubComponent) - int32(ESlateTransformSubComponent::X);

									ControlElement->Settings.LimitEnabled[Index].SetForValueType(ValueType, Value);
								}
								break;
							}
							case ERigControlType::EulerTransform:
							{
								if(ControlElement->Settings.LimitEnabled.Num() == 9)
								{
									switch(Component)
									{
										case ESlateTransformComponent::Location:
										{
											switch(SubComponent)
											{
												case ESlateTransformSubComponent::X:
												{
													ControlElement->Settings.LimitEnabled[0].SetForValueType(ValueType, Value);
													break;
												}
												case ESlateTransformSubComponent::Y:
												{
													ControlElement->Settings.LimitEnabled[1].SetForValueType(ValueType, Value);
													break;
												}
												case ESlateTransformSubComponent::Z:
												{
													ControlElement->Settings.LimitEnabled[2].SetForValueType(ValueType, Value);
													break;
												}
												default:
												{
													break;
												}
											}
											break;
										}
										case ESlateTransformComponent::Rotation:
										{
											switch(SubComponent)
											{
												case ESlateTransformSubComponent::Pitch:
												{
													ControlElement->Settings.LimitEnabled[3].SetForValueType(ValueType, Value);
													break;
												}
												case ESlateTransformSubComponent::Yaw:
												{
													ControlElement->Settings.LimitEnabled[4].SetForValueType(ValueType, Value);
													break;
												}
												case ESlateTransformSubComponent::Roll:
												{
													ControlElement->Settings.LimitEnabled[5].SetForValueType(ValueType, Value);
													break;
												}
												default:
												{
													break;
												}
											}
											break;
										}
										case ESlateTransformComponent::Scale:
										{
											switch(SubComponent)
											{
												case ESlateTransformSubComponent::X:
												{
													ControlElement->Settings.LimitEnabled[6].SetForValueType(ValueType, Value);
													break;
												}
												case ESlateTransformSubComponent::Y:
												{
													ControlElement->Settings.LimitEnabled[7].SetForValueType(ValueType, Value);
													break;
												}
												case ESlateTransformSubComponent::Z:
												{
													ControlElement->Settings.LimitEnabled[8].SetForValueType(ValueType, Value);
													break;
												}
												default:
												{
													break;
												}
											}
											break;
										}
									}
								}
								break;
							}
						}
						
						HierarchyBeingCustomized->SetControlSettings(ControlElement, ControlElement->Settings, true, true, true);
					}
				}
			});
			break;
		}
		default:
		{
			TransformWidgetArgs.AllowEditRotationRepresentation(true);
			TransformWidgetArgs.DisplayRelativeWorld(true);
			TransformWidgetArgs.DisplayToggle(false);
			TransformWidgetArgs._OnGetToggleChecked.Unbind();
			TransformWidgetArgs._OnToggleChanged.Unbind();
			break;
		}
	}

	auto GetRelativeAbsoluteTransforms = [CurrentTransformType, Keys, HierarchyBeingDebugged](
		const FRigElementKey& Key,
		ERigTransformElementDetailsTransform::Type InTransformType = ERigTransformElementDetailsTransform::Max
		) -> TPair<FEulerTransform, FEulerTransform>
	{
		if(InTransformType == ERigTransformElementDetailsTransform::Max)
		{
			InTransformType = CurrentTransformType;
		}

		FEulerTransform RelativeTransform = FEulerTransform::Identity;
		FEulerTransform AbsoluteTransform = FEulerTransform::Identity;

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
				const ERigControlType ControlType = ControlElement->Settings.ControlType;

				if(InTransformType == ERigTransformElementDetailsTransform::Offset)
				{
					RelativeTransform = HierarchyBeingDebugged->GetControlOffsetTransform(ControlElement, ERigTransformType::InitialLocal);
					AbsoluteTransform = HierarchyBeingDebugged->GetControlOffsetTransform(ControlElement, ERigTransformType::InitialGlobal);
				}
				else if(InTransformType == ERigTransformElementDetailsTransform::Minimum)
				{
					switch(ControlType)
					{
						case ERigControlType::Position:
						{
							const FVector Data = 
								(FVector)HierarchyBeingDebugged->GetControlValue(ControlElement, ERigControlValueType::Minimum)
								.Get<FVector3f>();
							AbsoluteTransform = RelativeTransform = FEulerTransform(Data, FRotator::ZeroRotator, FVector::OneVector);
							break;
						}
						case ERigControlType::Rotator:
						{
							const FVector Data = 
								(FVector)HierarchyBeingDebugged->GetControlValue(ControlElement, ERigControlValueType::Minimum)
								.Get<FVector3f>();
							FRotator Rotator = FRotator::MakeFromEuler(Data);
							AbsoluteTransform = RelativeTransform = FEulerTransform(FVector::ZeroVector, Rotator, FVector::OneVector);
							break;
						}
						case ERigControlType::Scale:
						{
							const FVector Data = 
								(FVector)HierarchyBeingDebugged->GetControlValue(ControlElement, ERigControlValueType::Minimum)
								.Get<FVector3f>();
							AbsoluteTransform = RelativeTransform = FEulerTransform(FVector::ZeroVector, FRotator::ZeroRotator, Data);
							break;
						}
						case ERigControlType::EulerTransform:
						{
							const FRigControlValue::FEulerTransform_Float EulerTransform = 
								HierarchyBeingDebugged->GetControlValue(ControlElement, ERigControlValueType::Minimum)
								.Get<FRigControlValue::FEulerTransform_Float>();
							AbsoluteTransform = RelativeTransform = EulerTransform.ToTransform();
							break;
						}
					}
				}
				else if(InTransformType == ERigTransformElementDetailsTransform::Maximum)
				{
					switch(ControlType)
					{
						case ERigControlType::Position:
						{
							const FVector Data = 
								(FVector)HierarchyBeingDebugged->GetControlValue(ControlElement, ERigControlValueType::Maximum)
								.Get<FVector3f>();
							AbsoluteTransform = RelativeTransform = FEulerTransform(Data, FRotator::ZeroRotator, FVector::OneVector);
							break;
						}
						case ERigControlType::Rotator:
						{
							const FVector Data = 
								(FVector)HierarchyBeingDebugged->GetControlValue(ControlElement, ERigControlValueType::Maximum)
								.Get<FVector3f>();
							FRotator Rotator = FRotator::MakeFromEuler(Data);
							AbsoluteTransform = RelativeTransform = FEulerTransform(FVector::ZeroVector, Rotator, FVector::OneVector);
							break;
						}
						case ERigControlType::Scale:
						{
							const FVector Data = 
								(FVector)HierarchyBeingDebugged->GetControlValue(ControlElement, ERigControlValueType::Maximum)
								.Get<FVector3f>();
							AbsoluteTransform = RelativeTransform = FEulerTransform(FVector::ZeroVector, FRotator::ZeroRotator, Data);
							break;
						}
						case ERigControlType::EulerTransform:
						{
							const FRigControlValue::FEulerTransform_Float EulerTransform = 
								HierarchyBeingDebugged->GetControlValue(ControlElement, ERigControlValueType::Maximum)
								.Get<FRigControlValue::FEulerTransform_Float>();
							AbsoluteTransform = RelativeTransform = EulerTransform.ToTransform();
							break;
						}
					}
				}
			}
		}

		return TPair<FTransform, FTransform>(RelativeTransform, AbsoluteTransform);
	};

	
	auto GetCombinedTransform = [IsComponentRelative, GetRelativeAbsoluteTransforms](
		const FRigElementKey& Key,
		ERigTransformElementDetailsTransform::Type InTransformType = ERigTransformElementDetailsTransform::Max
		) -> FEulerTransform
	{
		const TPair<FEulerTransform, FEulerTransform> TransformPair = GetRelativeAbsoluteTransforms(Key, InTransformType);
		const FEulerTransform RelativeTransform = TransformPair.Key;
		const FEulerTransform AbsoluteTransform = TransformPair.Value;

		FEulerTransform Xfo;
		Xfo.SetLocation((IsComponentRelative(0)) ? RelativeTransform.GetLocation() : AbsoluteTransform.GetLocation());
		Xfo.SetRotation((IsComponentRelative(1)) ? RelativeTransform.GetRotation() : AbsoluteTransform.GetRotation());
		Xfo.SetScale3D((IsComponentRelative(2)) ? RelativeTransform.GetScale3D() : AbsoluteTransform.GetScale3D());

		return Xfo;
	};

	auto GetSingleTransform = [GetRelativeAbsoluteTransforms](
		const FRigElementKey& Key,
		bool bIsRelative,
		ERigTransformElementDetailsTransform::Type InTransformType = ERigTransformElementDetailsTransform::Max
		) -> FEulerTransform
	{
		const TPair<FEulerTransform, FEulerTransform> TransformPair = GetRelativeAbsoluteTransforms(Key, InTransformType);
		const FEulerTransform RelativeTransform = TransformPair.Key;
		const FEulerTransform AbsoluteTransform = TransformPair.Value;
		return bIsRelative ? RelativeTransform : AbsoluteTransform;
	};

	auto SetSingleTransform = [CurrentTransformType, GetRelativeAbsoluteTransforms, this, HierarchyBeingDebugged](
		const FRigElementKey& Key,
		FEulerTransform InTransform,
		bool bIsRelative,
		bool bSetupUndoRedo)
	{
		const bool bCurrent = CurrentTransformType == ERigTransformElementDetailsTransform::Current; 
		const bool bInitial = CurrentTransformType == ERigTransformElementDetailsTransform::Initial; 

		bool bSetupModeEnabled = false;
		if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
		{
			bSetupModeEnabled = DebuggedRig->IsSetupModeEnabled();
		}

		TArray<URigHierarchy*> HierarchiesToUpdate;
		HierarchiesToUpdate.Add(HierarchyBeingDebugged);
		if(!bCurrent || bSetupModeEnabled)
		{
			HierarchiesToUpdate.Add(HierarchyBeingCustomized);
		}

		for(URigHierarchy* HierarchyToUpdate : HierarchiesToUpdate)
		{
			if(bInitial || CurrentTransformType == ERigTransformElementDetailsTransform::Current)
			{
				if(bIsRelative)
				{
					HierarchyToUpdate->SetLocalTransform(Key, InTransform, bInitial, true, bSetupUndoRedo);
				}
				else
				{
					HierarchyToUpdate->SetGlobalTransform(Key, InTransform, bInitial, true, bSetupUndoRedo);
				}
			}
			else
			{
				if(FRigControlElement* ControlElement = HierarchyToUpdate->Find<FRigControlElement>(Key))
				{
					const ERigControlType ControlType = ControlElement->Settings.ControlType;

					if(CurrentTransformType == ERigTransformElementDetailsTransform::Offset)
					{
						if(!bIsRelative)
						{
							const FTransform ParentTransform = HierarchyToUpdate->GetParentTransform(Key, bInitial);
							InTransform = FTransform(InTransform).GetRelativeTransform(ParentTransform);
						}
						HierarchyToUpdate->SetControlOffsetTransform(Key, InTransform, true, true, bSetupUndoRedo);
					}
					else if(CurrentTransformType == ERigTransformElementDetailsTransform::Minimum)
					{
						switch(ControlType)
						{
							case ERigControlType::Position:
							{
								const FRigControlValue Value = FRigControlValue::Make<FVector3f>((FVector3f)InTransform.GetLocation());
								HierarchyToUpdate->SetControlValue(ControlElement, Value, ERigControlValueType::Minimum, bSetupUndoRedo, true);
								break;
							}
							case ERigControlType::Rotator:
							{
								const FVector3f Euler = (FVector3f)InTransform.Rotator().Euler();
								const FRigControlValue Value = FRigControlValue::Make<FVector3f>(Euler);
								HierarchyToUpdate->SetControlValue(ControlElement, Value, ERigControlValueType::Minimum, bSetupUndoRedo, true);
								break;
							}
							case ERigControlType::Scale:
							{
								const FRigControlValue Value = FRigControlValue::Make<FVector3f>((FVector3f)InTransform.GetScale3D());
								HierarchyToUpdate->SetControlValue(ControlElement, Value, ERigControlValueType::Minimum, bSetupUndoRedo, true);
								break;
							}
							case ERigControlType::EulerTransform:
							{
								const FRigControlValue Value = FRigControlValue::Make<FRigControlValue::FEulerTransform_Float>(InTransform);
								HierarchyToUpdate->SetControlValue(ControlElement, Value, ERigControlValueType::Minimum, bSetupUndoRedo, true);
								break;
							}
						}
					}
					else if(CurrentTransformType == ERigTransformElementDetailsTransform::Maximum)
					{
						switch(ControlType)
						{
							case ERigControlType::Position:
							{
								const FRigControlValue Value = FRigControlValue::Make<FVector3f>((FVector3f)InTransform.GetLocation());
								HierarchyToUpdate->SetControlValue(ControlElement, Value, ERigControlValueType::Maximum, bSetupUndoRedo, true);
								break;
							}
							case ERigControlType::Rotator:
							{
								const FVector3f Euler = (FVector3f)InTransform.Rotator().Euler();
								const FRigControlValue Value = FRigControlValue::Make<FVector3f>(Euler);
								HierarchyToUpdate->SetControlValue(ControlElement, Value, ERigControlValueType::Maximum, bSetupUndoRedo, true);
								break;
							}
							case ERigControlType::Scale:
							{
								const FRigControlValue Value = FRigControlValue::Make<FVector3f>((FVector3f)InTransform.GetScale3D());
								HierarchyToUpdate->SetControlValue(ControlElement, Value, ERigControlValueType::Maximum, bSetupUndoRedo, true);
								break;
							}
							case ERigControlType::EulerTransform:
							{
								const FRigControlValue Value = FRigControlValue::Make<FRigControlValue::FEulerTransform_Float>(InTransform);
								HierarchyToUpdate->SetControlValue(ControlElement, Value, ERigControlValueType::Maximum, bSetupUndoRedo, true);
								break;
							}
						}
					}
				}
			}
		}
	};

	TransformWidgetArgs.OnGetNumericValue_Lambda([Keys, GetCombinedTransform](
		ESlateTransformComponent::Type Component,
		ESlateRotationRepresentation::Type Representation,
		ESlateTransformSubComponent::Type SubComponent) -> TOptional<FVector::FReal>
	{
		TOptional<FVector::FReal> FirstValue;

		for(int32 Index = 0; Index < Keys.Num(); Index++)
		{
			const FRigElementKey& Key = Keys[Index];
			FEulerTransform Xfo = GetCombinedTransform(Key);

			TOptional<FVector::FReal> CurrentValue = SAdvancedTransformInputBox<FEulerTransform>::GetNumericValueFromTransform(Xfo, Component, Representation, SubComponent);
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
					return TOptional<FVector::FReal>();
				}
			}
		}
		
		return FirstValue;
	});

	TransformWidgetArgs.OnNumericValueChanged_Lambda(
	[
		Keys,
		this,
		IsComponentRelative,
		GetSingleTransform,
		SetSingleTransform,
		CurrentTransformType
	](
		ESlateTransformComponent::Type Component,
		ESlateRotationRepresentation::Type Representation,
		ESlateTransformSubComponent::Type SubComponent,
		FVector::FReal InNumericValue)
	{
		const bool bIsRelative = IsComponentRelative((int32)Component);

		for(const FRigElementKey& Key : Keys)
		{
			FEulerTransform Transform = GetSingleTransform(Key, bIsRelative);
			FEulerTransform PreviousTransform = Transform;
			SAdvancedTransformInputBox<FEulerTransform>::ApplyNumericValueChange(Transform, InNumericValue, Component, Representation, SubComponent);

			if(!FRigControlElementDetails::Equals(Transform, PreviousTransform))
			{
				if(!SliderTransaction.IsValid())
				{
					SliderTransaction = MakeShareable(new FScopedTransaction(NSLOCTEXT("ControlRigElementDetails", "ChangeNumericValue", "Change Numeric Value")));
					HierarchyBeingCustomized->Modify();
				}
							
				SetSingleTransform(Key, Transform, bIsRelative, false);
			}
		}
	});

	TransformWidgetArgs.OnNumericValueCommitted_Lambda(
	[
		Keys,
		this,
		IsComponentRelative,
		GetSingleTransform,
		SetSingleTransform,
		CurrentTransformType
	](
		ESlateTransformComponent::Type Component,
		ESlateRotationRepresentation::Type Representation,
		ESlateTransformSubComponent::Type SubComponent,
		FVector::FReal InNumericValue,
		ETextCommit::Type InCommitType)
	{
		const bool bIsRelative = IsComponentRelative((int32)Component);

		{
			FScopedTransaction Transaction(LOCTEXT("ChangeNumericValue", "Change Numeric Value"));
			if(!SliderTransaction.IsValid())
			{
				HierarchyBeingCustomized->Modify();
			}
			
			for(const FRigElementKey& Key : Keys)
			{
				FEulerTransform Transform = GetSingleTransform(Key, bIsRelative);
				SAdvancedTransformInputBox<FEulerTransform>::ApplyNumericValueChange(Transform, InNumericValue, Component, Representation, SubComponent);
				SetSingleTransform(Key, Transform, bIsRelative, true);
			}
		}

		SliderTransaction.Reset();
	});

	TransformWidgetArgs.OnCopyToClipboard_Lambda([Keys, IsComponentRelative, ConformComponentRelative, GetSingleTransform](
		ESlateTransformComponent::Type InComponent
		)
	{
		if(Keys.Num() == 0)
		{
			return;
		}

		// make sure that we use the same relative setting on all components when copying
		ConformComponentRelative(0);
		const bool bIsRelative = IsComponentRelative(0); 

		const FRigElementKey& FirstKey = Keys[0];
		FEulerTransform Xfo = GetSingleTransform(FirstKey, bIsRelative);

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
				TBaseStructure<FEulerTransform>::Get()->ExportText(Content, &Xfo, &Xfo, nullptr, PPF_None, nullptr);
				break;
			}
		}

		if(!Content.IsEmpty())
		{
			FPlatformApplicationMisc::ClipboardCopy(*Content);
		}
	});

	TransformWidgetArgs.OnPasteFromClipboard_Lambda([this, Keys, IsComponentRelative, ConformComponentRelative, GetSingleTransform, SetSingleTransform](
		ESlateTransformComponent::Type InComponent
		)
	{
		if(Keys.Num() == 0)
		{
			return;
		}
		
		
		// make sure that we use the same relative setting on all components when pasting
		ConformComponentRelative(0);
		const bool bIsRelative = IsComponentRelative(0); 

		FString Content;
		FPlatformApplicationMisc::ClipboardPaste(Content);

		if(Content.IsEmpty())
		{
			return;
		}

		FScopedTransaction Transaction(LOCTEXT("PasteTransform", "Paste Transform"));
		HierarchyBeingCustomized->Modify();

		for(const FRigElementKey& Key : Keys)
		{
			FEulerTransform Xfo = GetSingleTransform(Key, bIsRelative);
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
						TBaseStructure<FEulerTransform>::Get()->ImportText(*Content, &Xfo, nullptr, PPF_None, &ErrorPipe, TBaseStructure<FEulerTransform>::Get()->GetName(), true);
						break;
					}
				}

				if(ErrorPipe.NumErrors == 0)
				{
					SetSingleTransform(Key, Xfo, bIsRelative, true);
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
			const FEulerTransform CurrentTransform = GetSingleTransform(Key, true);
			FEulerTransform DefaultTransform;

			switch(CurrentTransformType)
			{
				case ERigTransformElementDetailsTransform::Current:
				{
					DefaultTransform = GetSingleTransform(Key, true, ERigTransformElementDetailsTransform::Initial);
					break;
				}
				default:
				{
					DefaultTransform = FEulerTransform::Identity; 
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

	TransformWidgetArgs.OnResetToDefault_Lambda([this, CurrentTransformType, Keys, GetSingleTransform, SetSingleTransform](
		ESlateTransformComponent::Type InComponent)
	{
		FScopedTransaction Transaction(LOCTEXT("ResetTransformToDefault", "Reset Transform to Default"));
		HierarchyBeingCustomized->Modify();

		for(const FRigElementKey& Key : Keys)
		{
			FEulerTransform CurrentTransform = GetSingleTransform(Key, true);
			FEulerTransform DefaultTransform;

			switch(CurrentTransformType)
			{
				case ERigTransformElementDetailsTransform::Current:
				{
					DefaultTransform = GetSingleTransform(Key, true, ERigTransformElementDetailsTransform::Initial);
					break;
				}
				default:
				{
					DefaultTransform = FEulerTransform::Identity; 
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

			SetSingleTransform(Key, CurrentTransform, true, true);
		}
	});

	SAdvancedTransformInputBox<FEulerTransform>::ConstructGroupedTransformRows(
		CategoryBuilder, 
		Label, 
		Tooltip, 
		TransformWidgetArgs);
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

	if (HierarchyBeingCustomized == nullptr)
	{
		return;
	}

	ControlElements.Reset();
	ObjectPerControl.Reset();;
	
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

	CustomizeControl(DetailBuilder);
	CustomizeValue(DetailBuilder);
	CustomizeTransform(DetailBuilder);
	CustomizeShape(DetailBuilder);
}

void FRigControlElementDetails::CustomizeValue(IDetailLayoutBuilder& DetailBuilder)
{
	if(ControlElements.Num() == 0)
	{
		return;
	}

	// only show this section if all controls are the same type
	const ERigControlType ControlType = ControlElements[0].Settings.ControlType;
	for(const FRigControlElement& ControlElement : ControlElements)
	{
		if(ControlElement.Settings.ControlType != ControlType)
		{
			return;
		}
	}

	// transforms don't show their value here - instead they are shown in the transform section
	if(ControlType == ERigControlType::EulerTransform ||
		ControlType == ERigControlType::Transform ||
		ControlType == ERigControlType::TransformNoScale)
	{
		return;
	}
	
	TArray<FText> Labels = {
		LOCTEXT("Initial", "Initial"),
		LOCTEXT("Current", "Current")
	};
	TArray<FText> Tooltips = {
		LOCTEXT("ValueInitialTooltip", "The initial animation value of the control"),
		LOCTEXT("ValueCurrentTooltip", "The current animation value of the control")
	};
	TArray<ERigControlValueType> ValueTypes = {
		ERigControlValueType::Initial,
		ERigControlValueType::Current
	};

	// bool doesn't have limits,
	// transform types already got filtered out earlier.
	// integers with enums don't have limits either
	if(ControlType != ERigControlType::Bool &&
		(ControlType != ERigControlType::Integer || ControlElements[0].Settings.ControlEnum.IsNull()))
	{
		Labels.Append({
			LOCTEXT("Min", "Min"),
			LOCTEXT("Max", "Max")
		});
		Tooltips.Append({
			LOCTEXT("ValueMinimumTooltip", "The minimum limit(s) for the control"),
			LOCTEXT("ValueMaximumTooltip", "The maximum limit(s) for the control")
		});
		ValueTypes.Append({
			ERigControlValueType::Minimum,
			ERigControlValueType::Maximum
		});
	}
	
	IDetailCategoryBuilder& ValueCategory = DetailBuilder.EditCategory(TEXT("Value"), LOCTEXT("Value", "Value"));

	if(!PickedValueTypes.IsValid())
	{
		PickedValueTypes = MakeShareable(new TArray<ERigControlValueType>({ERigControlValueType::Current}));
	}

	TSharedPtr<SSegmentedControl<ERigControlValueType>> ValueTypeChoiceWidget =
		SSegmentedControl<ERigControlValueType>::Create(
			ValueTypes,
			Labels,
			Tooltips,
			*PickedValueTypes.Get(),
			true,
			SSegmentedControl<ERigControlValueType>::FOnValuesChanged::CreateLambda(
				[](TArray<ERigControlValueType> NewSelection)
				{
					(*FRigControlElementDetails::PickedValueTypes.Get()) = NewSelection;
				}
			)
		);

	AddChoiceWidgetRow(ValueCategory, FText::FromString(TEXT("ValueType")), ValueTypeChoiceWidget.ToSharedRef());


	TSharedRef<UE::Math::TVector<float>> IsComponentRelative = MakeShareable(new UE::Math::TVector<float>(1.f, 1.f, 1.f));

	for(int32 Index=0; Index < ValueTypes.Num(); Index++)
	{
		const ERigControlValueType ValueType = ValueTypes[Index];
		
		const TAttribute<EVisibility> VisibilityAttribute =
			TAttribute<EVisibility>::CreateLambda([ValueType, ValueTypeChoiceWidget]()-> EVisibility
			{
				return ValueTypeChoiceWidget->HasValue(ValueType) ? EVisibility::Visible : EVisibility::Collapsed; 
			});
		
		switch(ControlType)
		{
			case ERigControlType::Bool:
			{
				CreateBoolValueWidgetRow(ValueCategory, Labels[Index], Tooltips[Index], ValueType, VisibilityAttribute);
				break;
			}
			case ERigControlType::Float:
			{
				CreateFloatValueWidgetRow(ValueCategory, Labels[Index], Tooltips[Index], ValueType, VisibilityAttribute);
				break;
			}
			case ERigControlType::Integer:
			{
				bool bIsEnum = false;
				const TArray<FRigElementKey> Keys = GetElementKeys();
				for(const FRigElementKey& Key : Keys)
				{
					if(const FRigControlElement* ControlElement = HierarchyBeingCustomized->Find<FRigControlElement>(Key))
					{
						if(!ControlElement->Settings.ControlEnum.IsNull())
						{
							bIsEnum = true;
							break;
						}
					}
				}

				if(bIsEnum)
				{
					CreateEnumValueWidgetRow(ValueCategory, Labels[Index], Tooltips[Index], ValueType, VisibilityAttribute);
				}
				else
				{
					CreateIntegerValueWidgetRow(ValueCategory, Labels[Index], Tooltips[Index], ValueType, VisibilityAttribute);
				}
				break;
			}
			case ERigControlType::Vector2D:
			{
				CreateVector2DValueWidgetRow(ValueCategory, Labels[Index], Tooltips[Index], ValueType, VisibilityAttribute);
				break;
			}
			case ERigControlType::Position:
			case ERigControlType::Rotator:
			case ERigControlType::Scale:
			{
				ERigTransformElementDetailsTransform::Type CurrentTransformType = ERigTransformElementDetailsTransform::Current;
				switch(ValueType)
				{
					case ERigControlValueType::Initial:
					{
						CurrentTransformType = ERigTransformElementDetailsTransform::Initial;
						break;
					}
					case ERigControlValueType::Minimum:
					{
						CurrentTransformType = ERigTransformElementDetailsTransform::Minimum;
						break;
					}
					case ERigControlValueType::Maximum:
					{
						CurrentTransformType = ERigTransformElementDetailsTransform::Maximum;
						break;
					}
				}

				SAdvancedTransformInputBox<FEulerTransform>::FArguments TransformWidgetArgs = SAdvancedTransformInputBox<FEulerTransform>::FArguments()
				.DisplayToggle(false)
				.DisplayRelativeWorld(true)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.AllowEditRotationRepresentation(false)
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

				TransformWidgetArgs.Visibility(VisibilityAttribute);

				TransformWidgetArgs.ConstructLocation(ControlType == ERigControlType::Position);
				TransformWidgetArgs.ConstructRotation(ControlType == ERigControlType::Rotator);
				TransformWidgetArgs.ConstructScale(ControlType == ERigControlType::Scale);

				CreateEulerTransformValueWidgetRow(
					GetHierarchyBeingDebugged(),
					GetElementKeys(),
					TransformWidgetArgs,
					ValueCategory,
					Labels[Index],
					Tooltips[Index],
					CurrentTransformType,
					ValueType);
				break;
			}
			default:
			{
				break;
			}
		}
	}
}

void FRigControlElementDetails::CustomizeControl(IDetailLayoutBuilder& DetailBuilder)
{
	const TSharedPtr<IPropertyHandle> SettingsHandle = DetailBuilder.GetProperty(TEXT("Settings"));
	DetailBuilder.HideProperty(SettingsHandle);

	IDetailCategoryBuilder& ControlCategory = DetailBuilder.EditCategory(TEXT("Control"), LOCTEXT("Control", "Control"));

	const TSharedPtr<IPropertyHandle> DisplayNameHandle = SettingsHandle->GetChildHandle(TEXT("DisplayName"));
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
				this->HierarchyBeingCustomized->Modify();
				
				for(int32 ControlIndex = 0; ControlIndex< ControlElementsInView.Num(); ControlIndex++)
				{
					const FRigControlElement& ViewElement = ControlElementsInView[ControlIndex];
					FRigControlElement* ControlElement = ControlElementsInHierarchy[ControlIndex];
					
					FRigControlValue ValueToSet;

					ControlElement->Settings.ControlType = ViewElement.Settings.ControlType;
					ControlElement->Settings.LimitEnabled.Reset();

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
							ControlElement->Settings.SetupLimitArrayForType(true);
							ControlElement->Settings.MinimumValue = FRigControlValue::Make<float>(0.f);
							ControlElement->Settings.MaximumValue = FRigControlValue::Make<float>(100.f);
							break;
						}
						case ERigControlType::Integer:
						{
							ValueToSet = FRigControlValue::Make<int32>(0);
							ControlElement->Settings.SetupLimitArrayForType(true);
							ControlElement->Settings.MinimumValue = FRigControlValue::Make<int32>(0);
							ControlElement->Settings.MaximumValue = FRigControlValue::Make<int32>(100);
							break;
						}
						case ERigControlType::Vector2D:
						{
							ValueToSet = FRigControlValue::Make<FVector2D>(FVector2D::ZeroVector);
							ControlElement->Settings.SetupLimitArrayForType(true);
							ControlElement->Settings.MinimumValue = FRigControlValue::Make<FVector2D>(FVector2D::ZeroVector);
							ControlElement->Settings.MaximumValue = FRigControlValue::Make<FVector2D>(FVector2D(100.f, 100.f));
							break;
						}
						case ERigControlType::Position:
						{
							ValueToSet = FRigControlValue::Make<FVector>(FVector::ZeroVector);
							ControlElement->Settings.SetupLimitArrayForType(false);
							ControlElement->Settings.MinimumValue = FRigControlValue::Make<FVector>(-FVector::OneVector);
							ControlElement->Settings.MaximumValue = FRigControlValue::Make<FVector>(FVector::OneVector);
							break;
						}
						case ERigControlType::Scale:
						{
							ValueToSet = FRigControlValue::Make<FVector>(FVector::OneVector);
							ControlElement->Settings.SetupLimitArrayForType(false);
							ControlElement->Settings.MinimumValue = FRigControlValue::Make<FVector>(FVector::ZeroVector);
							ControlElement->Settings.MaximumValue = FRigControlValue::Make<FVector>(FVector::OneVector);
							break;
						}
						case ERigControlType::Rotator:
						{
							ValueToSet = FRigControlValue::Make<FRotator>(FRotator::ZeroRotator);
							ControlElement->Settings.SetupLimitArrayForType(false, false);
							ControlElement->Settings.MinimumValue = FRigControlValue::Make<FRotator>(FRotator::ZeroRotator);
							ControlElement->Settings.MaximumValue = FRigControlValue::Make<FRotator>(FRotator(180.f, 180.f, 180.f));
							break;
						}
						case ERigControlType::Transform:
						{
							ValueToSet = FRigControlValue::Make<FTransform>(FTransform::Identity);
							ControlElement->Settings.SetupLimitArrayForType(false, false, false);
							ControlElement->Settings.MinimumValue = ValueToSet;
							ControlElement->Settings.MaximumValue = ValueToSet;
							break;
						}
						case ERigControlType::TransformNoScale:
						{
							FTransformNoScale Identity = FTransform::Identity;
							ValueToSet = FRigControlValue::Make<FTransformNoScale>(Identity);
							ControlElement->Settings.SetupLimitArrayForType(false, false, false);
							ControlElement->Settings.MinimumValue = ValueToSet;
							ControlElement->Settings.MaximumValue = ValueToSet;
							break;
						}
						case ERigControlType::EulerTransform:
						{
							FEulerTransform Identity = FEulerTransform::Identity;
							ValueToSet = FRigControlValue::Make<FEulerTransform>(Identity);
							ControlElement->Settings.SetupLimitArrayForType(false, false, false);
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

					this->HierarchyBeingCustomized->SetControlSettings(ControlElement, ControlElement->Settings, true, true, true);
					this->HierarchyBeingCustomized->SetControlValue(ControlElement, ValueToSet, ERigControlValueType::Initial, true, false, true);
					this->HierarchyBeingCustomized->SetControlValue(ControlElement, ValueToSet, ERigControlValueType::Current, true, false, true);

					ObjectsBeingCustomized[ControlIndex]->SetContent<FRigControlElement>(*ControlElement);

					if (this->HierarchyBeingCustomized != this->BlueprintBeingCustomized->Hierarchy)
					{
						if(FRigControlElement* OtherControlElement = this->BlueprintBeingCustomized->Hierarchy->Find<FRigControlElement>(ControlElement->GetKey()))
						{
							OtherControlElement->Settings = ControlElement->Settings;
							this->BlueprintBeingCustomized->Hierarchy->SetControlSettings(OtherControlElement, OtherControlElement->Settings, true, true, true);
							this->BlueprintBeingCustomized->Hierarchy->SetControlValue(OtherControlElement, ValueToSet, ERigControlValueType::Initial, true);
							this->BlueprintBeingCustomized->Hierarchy->SetControlValue(OtherControlElement, ValueToSet, ERigControlValueType::Current, true);
						}
					}
				}
				
				PropertyUtilities->ForceRefresh();
			}
		}
	));

	ControlCategory.AddProperty(ControlTypeHandle.ToSharedRef());

	if(!(IsAnyControlNotOfType(ERigControlType::Integer) &&
		IsAnyControlNotOfType(ERigControlType::Float) &&
		IsAnyControlNotOfType(ERigControlType::Vector2D)))
	{
		const TSharedPtr<IPropertyHandle> PrimaryAxisHandle = SettingsHandle->GetChildHandle(TEXT("PrimaryAxis"));
		ControlCategory.AddProperty(PrimaryAxisHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Primary Axis")));
	}

	if(IsAnyControlOfType(ERigControlType::Integer))
	{
		const TSharedPtr<IPropertyHandle> ControlEnumHandle = SettingsHandle->GetChildHandle(TEXT("ControlEnum"));
		ControlCategory.AddProperty(ControlEnumHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Control Enum")));

		ControlEnumHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda(
			[this, PropertyUtilities]()
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
							ControlBeingCustomized->Settings.LimitEnabled.Reset();
							ControlBeingCustomized->Settings.LimitEnabled.Add(true);
							HierarchyBeingCustomized->SetControlSettings(ControlBeingCustomized, ControlBeingCustomized->Settings, true, true, true);

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
									DebuggedHierarchy->SetControlSettings(DebuggedControlElement, DebuggedControlElement->Settings, true, true, true);

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

	ControlCategory.AddProperty(SettingsHandle->GetChildHandle(TEXT("bAnimatable")).ToSharedRef());

	const TSharedPtr<IPropertyHandle> CustomizationHandle = SettingsHandle->GetChildHandle(TEXT("Customization"));
	const TSharedPtr<IPropertyHandle> AvailableSpacesHandle = CustomizationHandle->GetChildHandle(TEXT("AvailableSpaces"));
	ControlCategory.AddProperty(AvailableSpacesHandle.ToSharedRef());

	TArray<FRigElementKey> Keys = GetElementKeys();
	URigHierarchy* HierarchyBeingDebugged = GetHierarchyBeingDebugged();

	const TSharedPtr<IPropertyHandle> DrawLimitsHandle = SettingsHandle->GetChildHandle(TEXT("bDrawLimits"));
	
	ControlCategory
	.AddProperty(DrawLimitsHandle.ToSharedRef()).DisplayName(FText::FromString(TEXT("Draw Limits")))
	.IsEnabled(TAttribute<bool>::CreateLambda([Keys, HierarchyBeingDebugged]() -> bool
	{
		for(const FRigElementKey& Key : Keys)
		{
			if(const FRigControlElement* ControlElement = HierarchyBeingDebugged->Find<FRigControlElement>(Key))
			{
				if(ControlElement->Settings.LimitEnabled.Contains(FRigControlLimitEnabled(true, true)))
				{
					return true;
				}
			}
		}
		return false;
	}));
}

void FRigControlElementDetails::CustomizeShape(IDetailLayoutBuilder& DetailBuilder)
{
	// bools don't have shapes
	if(IsAnyControlOfType(ERigControlType::Bool))
	{
		return;
	}

	TSharedPtr<IPropertyHandle> ShapeHandle = DetailBuilder.GetProperty(TEXT("Shape"));
	TSharedPtr<IPropertyHandle> InitialHandle = ShapeHandle->GetChildHandle(TEXT("Initial"));
	TSharedPtr<IPropertyHandle> LocalHandle = InitialHandle->GetChildHandle(TEXT("Local"));
	ShapeTransformHandle = LocalHandle->GetChildHandle(TEXT("Transform"));
	
	ShapeNameList.Reset();
	if (BlueprintBeingCustomized)
	{
		const bool bUseNameSpace = BlueprintBeingCustomized->ShapeLibraries.Num() > 1;
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

	IDetailCategoryBuilder& ShapeCategory = DetailBuilder.EditCategory(TEXT("Shape"), LOCTEXT("Shape", "Shape"));

	const TSharedPtr<IPropertyHandle> SettingsHandle = DetailBuilder.GetProperty(TEXT("Settings"));

	ShapeCategory.AddProperty(SettingsHandle->GetChildHandle(TEXT("bShapeEnabled")).ToSharedRef())
	.DisplayName(FText::FromString(TEXT("Enabled")));
	ShapeCategory.AddProperty(SettingsHandle->GetChildHandle(TEXT("bShapeVisible")).ToSharedRef())
	.DisplayName(FText::FromString(TEXT("Visible")));

	IDetailGroup& ShapePropertiesGroup = ShapeCategory.AddGroup(TEXT("Shape Properties"), LOCTEXT("ShapeProperties", "Shape Properties"));
	ShapePropertiesGroup.HeaderRow().NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("ShapeProperties", "Shape Properties"))
		.ToolTipText(LOCTEXT("ShapePropertiesTooltip", "Customize the properties of the shape"))
	]
	.CopyAction(FUIAction(
		FExecuteAction::CreateSP(this, &FRigControlElementDetails::OnCopyShapeProperties)))
	.PasteAction(FUIAction(
		FExecuteAction::CreateSP(this, &FRigControlElementDetails::OnPasteShapeProperties)));
	
	// setup shape transform
	SAdvancedTransformInputBox<FEulerTransform>::FArguments TransformWidgetArgs = SAdvancedTransformInputBox<FEulerTransform>::FArguments()
	.DisplayToggle(false)
	.DisplayRelativeWorld(false)
	.Font(IDetailLayoutBuilder::GetDetailFont());

	TArray<FRigElementKey> Keys = GetElementKeys();
	URigHierarchy* HierarchyBeingDebugged = GetHierarchyBeingDebugged();
	Keys = HierarchyBeingDebugged->SortKeys(Keys);

	auto GetShapeTransform = [HierarchyBeingDebugged](
		const FRigElementKey& Key
		) -> FEulerTransform
	{
		if(FRigControlElement* ControlElement = HierarchyBeingDebugged->Find<FRigControlElement>(Key))
		{
			return HierarchyBeingDebugged->GetControlShapeTransform(ControlElement, ERigTransformType::InitialLocal);
		}
		return FEulerTransform::Identity;
	};

	auto SetShapeTransform = [this](
		const FRigElementKey& Key,
		const FEulerTransform& InTransform,
		bool bSetupUndo
		)
	{
		if(FRigControlElement* ControlElement = HierarchyBeingCustomized->Find<FRigControlElement>(Key))
		{
			HierarchyBeingCustomized->SetControlShapeTransform(ControlElement, InTransform, ERigTransformType::InitialLocal, bSetupUndo, true, bSetupUndo);
		}
	};

	TransformWidgetArgs.OnGetNumericValue_Lambda([Keys, HierarchyBeingDebugged, GetShapeTransform](
		ESlateTransformComponent::Type Component,
		ESlateRotationRepresentation::Type Representation,
		ESlateTransformSubComponent::Type SubComponent) -> TOptional<FVector::FReal>
	{
		TOptional<FVector::FReal> FirstValue;

		for(int32 Index = 0; Index < Keys.Num(); Index++)
		{
			const FRigElementKey& Key = Keys[Index];
			FEulerTransform Xfo = GetShapeTransform(Key);

			TOptional<FVector::FReal> CurrentValue = SAdvancedTransformInputBox<FEulerTransform>::GetNumericValueFromTransform(Xfo, Component, Representation, SubComponent);
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
					return TOptional<FVector::FReal>();
				}
			}
		}
		
		return FirstValue;
	});

	TransformWidgetArgs.OnNumericValueChanged_Lambda(
	[
		Keys,
		this,
		GetShapeTransform,
		SetShapeTransform
	](
		ESlateTransformComponent::Type Component,
		ESlateRotationRepresentation::Type Representation,
		ESlateTransformSubComponent::Type SubComponent,
		FVector::FReal InNumericValue)
	{
		for(const FRigElementKey& Key : Keys)
		{
			FEulerTransform Transform = GetShapeTransform(Key);
			FEulerTransform PreviousTransform = Transform;
			SAdvancedTransformInputBox<FEulerTransform>::ApplyNumericValueChange(Transform, InNumericValue, Component, Representation, SubComponent);

			if(!FRigControlElementDetails::Equals(Transform, PreviousTransform))
			{
				if(!SliderTransaction.IsValid())
				{
					SliderTransaction = MakeShareable(new FScopedTransaction(NSLOCTEXT("ControlRigElementDetails", "ChangeNumericValue", "Change Numeric Value")));
					HierarchyBeingCustomized->Modify();
				}
				SetShapeTransform(Key, Transform, false);
			}
		}
	});

	TransformWidgetArgs.OnNumericValueCommitted_Lambda(
	[
		Keys,
		this,
		GetShapeTransform,
		SetShapeTransform
	](
		ESlateTransformComponent::Type Component,
		ESlateRotationRepresentation::Type Representation,
		ESlateTransformSubComponent::Type SubComponent,
		FVector::FReal InNumericValue,
		ETextCommit::Type InCommitType)
	{
		{
			FScopedTransaction Transaction(LOCTEXT("ChangeNumericValue", "Change Numeric Value"));
			this->HierarchyBeingCustomized->Modify();

			for(const FRigElementKey& Key : Keys)
			{
				FEulerTransform Transform = GetShapeTransform(Key);
				FEulerTransform PreviousTransform = Transform;
				SAdvancedTransformInputBox<FEulerTransform>::ApplyNumericValueChange(Transform, InNumericValue, Component, Representation, SubComponent);
				if(!FRigControlElementDetails::Equals(Transform, PreviousTransform))
				{
					SetShapeTransform(Key, Transform, true);
				}
			}
		}
		SliderTransaction.Reset();
	});

	TransformWidgetArgs.OnCopyToClipboard_Lambda([Keys, GetShapeTransform](
		ESlateTransformComponent::Type InComponent
		)
	{
		if(Keys.Num() == 0)
		{
			return;
		}

		const FRigElementKey& FirstKey = Keys[0];
		FEulerTransform Xfo = GetShapeTransform(FirstKey);

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
				TBaseStructure<FEulerTransform>::Get()->ExportText(Content, &Xfo, &Xfo, nullptr, PPF_None, nullptr);
				break;
			}
		}

		if(!Content.IsEmpty())
		{
			FPlatformApplicationMisc::ClipboardCopy(*Content);
		}
	});

	TransformWidgetArgs.OnPasteFromClipboard_Lambda([Keys, GetShapeTransform, SetShapeTransform, this](
		ESlateTransformComponent::Type InComponent
		)
	{
		if(Keys.Num() == 0)
		{
			return;
		}

		FString Content;
		FPlatformApplicationMisc::ClipboardPaste(Content);

		if(Content.IsEmpty())
		{
			return;
		}

		FScopedTransaction Transaction(LOCTEXT("PasteTransform", "Paste Transform"));
		HierarchyBeingCustomized->Modify();

		for(const FRigElementKey& Key : Keys)
		{
			FEulerTransform Xfo = GetShapeTransform(Key);
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
						TBaseStructure<FEulerTransform>::Get()->ImportText(*Content, &Xfo, nullptr, PPF_None, &ErrorPipe, TBaseStructure<FEulerTransform>::Get()->GetName(), true);
						break;
					}
				}

				if(ErrorPipe.NumErrors == 0)
				{
					SetShapeTransform(Key, Xfo, true);
				}
			}
		}
	});

	TransformWidgetArgs.DiffersFromDefault_Lambda([
		Keys,
		GetShapeTransform
	](
		ESlateTransformComponent::Type InComponent) -> bool
	{
		for(const FRigElementKey& Key : Keys)
		{
			const FEulerTransform CurrentTransform = GetShapeTransform(Key);
			static const FEulerTransform DefaultTransform = FEulerTransform::Identity;

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

	TransformWidgetArgs.OnResetToDefault_Lambda([Keys, GetShapeTransform, SetShapeTransform, this](
		ESlateTransformComponent::Type InComponent)
	{
		FScopedTransaction Transaction(LOCTEXT("ResetTransformToDefault", "Reset Transform to Default"));
		HierarchyBeingCustomized->Modify();

		for(const FRigElementKey& Key : Keys)
		{
			FEulerTransform CurrentTransform = GetShapeTransform(Key);
			static const FEulerTransform DefaultTransform = FEulerTransform::Identity; 

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

			SetShapeTransform(Key, CurrentTransform, true);
		}
	});

	SAdvancedTransformInputBox<FEulerTransform>::ConstructGroupedTransformRows(
		ShapeCategory, 
		LOCTEXT("ShapeTransform", "Shape Transform"), 
		LOCTEXT("ShapeTransformTooltip", "The relative transform of the shape under the control"),
		TransformWidgetArgs);

	ShapeNameHandle = SettingsHandle->GetChildHandle(TEXT("ShapeName"));
	ShapePropertiesGroup.AddPropertyRow(ShapeNameHandle.ToSharedRef()).CustomWidget()
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
	ShapePropertiesGroup.AddPropertyRow(ShapeColorHandle.ToSharedRef())
	.DisplayName(FText::FromString(TEXT("Color")));
}

void FRigControlElementDetails::RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule, UClass* InClass)
{
	FRigTransformElementDetails::RegisterSectionMappings(PropertyEditorModule, InClass);

	TSharedRef<FPropertySection> ControlSection = PropertyEditorModule.FindOrCreateSection(InClass->GetFName(), "Control", LOCTEXT("Control", "Control"));
	ControlSection->AddCategory("General");
	ControlSection->AddCategory("Control");
	ControlSection->AddCategory("Value");

	TSharedRef<FPropertySection> ShapeSection = PropertyEditorModule.FindOrCreateSection(InClass->GetFName(), "Shape", LOCTEXT("Shape", "Shape"));
	ShapeSection->AddCategory("General");
	ShapeSection->AddCategory("Shape");
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

				if(HierarchyBeingCustomized)
				{
					FScopedTransaction Transaction(LOCTEXT("SetDisplayName", "SetDisplayName"));
					HierarchyBeingCustomized->SetControlSettings(ControlElement.GetKey(), ControlElement.Settings, true, true, true);
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

void FRigControlElementDetails::CreateBoolValueWidgetRow(
	IDetailCategoryBuilder& CategoryBuilder,
	const FText& Label,
	const FText& Tooltip,
	ERigControlValueType ValueType,
	TAttribute<EVisibility> Visibility)
{
	const bool bCurrent = ValueType == ERigControlValueType::Current;
	const bool bInitial = ValueType == ERigControlValueType::Initial;

	TArray<FRigElementKey> Keys = GetElementKeys();
	URigHierarchy* HierarchyBeingDebugged = GetHierarchyBeingDebugged();
	URigHierarchy* HierarchyToChange = bCurrent ? HierarchyBeingDebugged : HierarchyBeingCustomized;
	Keys = HierarchyBeingDebugged->SortKeys(Keys);

	const static TCHAR* TrueText = TEXT("True");
	const static TCHAR* FalseText = TEXT("False");

	CategoryBuilder.AddCustomRow(Label)
	.Visibility(Visibility)
	.NameContent()
	[
		SNew(STextBlock)
		.Text(Label)
		.ToolTipText(Tooltip)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([ValueType, Keys, HierarchyBeingDebugged]() -> ECheckBoxState
		{
			const bool FirstValue = HierarchyBeingDebugged->GetControlValue<bool>(Keys[0], ValueType);
			for(int32 Index = 1; Index < Keys.Num(); Index++)
			{
				const bool SecondValue = HierarchyBeingDebugged->GetControlValue<bool>(Keys[Index], ValueType);
				if(FirstValue != SecondValue)
				{
					return ECheckBoxState::Undetermined;
				}
			}
			return FirstValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([ValueType, Keys, HierarchyToChange](ECheckBoxState NewState)
		{
			if(NewState == ECheckBoxState::Undetermined)
			{
				return;
			}

			const bool Value = NewState == ECheckBoxState::Checked;
			FScopedTransaction Transaction(LOCTEXT("ChangeValue", "Change Value"));
			HierarchyToChange->Modify();
			for(const FRigElementKey& Key : Keys)
			{
				HierarchyToChange->SetControlValue(Key, FRigControlValue::Make<bool>(Value), ValueType, true, true); 
			}
		})
	]
	.CopyAction(FUIAction(
	FExecuteAction::CreateLambda([ValueType, Keys, HierarchyBeingDebugged]()
		{
			const bool FirstValue = HierarchyBeingDebugged->GetControlValue<bool>(Keys[0], ValueType);
			FPlatformApplicationMisc::ClipboardCopy(FirstValue ? TrueText : FalseText);
		}),
		FCanExecuteAction())
	)
	.PasteAction(FUIAction(
		FExecuteAction::CreateLambda([ValueType, Keys, HierarchyToChange]()
		{
			FString Content;
			FPlatformApplicationMisc::ClipboardPaste(Content);

			const bool Value = FToBoolHelper::FromCStringWide(*Content);
			FScopedTransaction Transaction(LOCTEXT("ChangeValue", "Change Value"));
			HierarchyToChange->Modify();
			for(const FRigElementKey& Key : Keys)
			{
				HierarchyToChange->SetControlValue(Key, FRigControlValue::Make<bool>(Value), ValueType, true, true); 
			}
		}),
		FCanExecuteAction())
	)
	.OverrideResetToDefault(FResetToDefaultOverride::Create(
		TAttribute<bool>::CreateLambda([ValueType, Keys, HierarchyBeingDebugged]() -> bool
		{
			const bool FirstValue = HierarchyBeingDebugged->GetControlValue<bool>(Keys[0], ValueType);
			const bool ReferenceValue = ValueType == ERigControlValueType::Initial ? false :
				HierarchyBeingDebugged->GetControlValue<bool>(Keys[0], ERigControlValueType::Initial);

			return FirstValue != ReferenceValue;
		}),
		FSimpleDelegate::CreateLambda([ValueType, Keys, HierarchyToChange]()
		{
			FScopedTransaction Transaction(LOCTEXT("ResetValueToDefault", "Reset Value To Default"));
			HierarchyToChange->Modify();
			for(const FRigElementKey& Key : Keys)
			{
				const bool ReferenceValue = ValueType == ERigControlValueType::Initial ? false :
					HierarchyToChange->GetControlValue<bool>(Keys[0], ERigControlValueType::Initial);
				HierarchyToChange->SetControlValue(Key, FRigControlValue::Make<bool>(ReferenceValue), ValueType, true, true); 
			}
		})
	));
}

void FRigControlElementDetails::CreateFloatValueWidgetRow(
	IDetailCategoryBuilder& CategoryBuilder,
	const FText& Label,
	const FText& Tooltip,
	ERigControlValueType ValueType,
	TAttribute<EVisibility> Visibility)
{
	CreateNumericValueWidgetRow<float>(CategoryBuilder, Label, Tooltip, ValueType, Visibility);
}

void FRigControlElementDetails::CreateIntegerValueWidgetRow(
	IDetailCategoryBuilder& CategoryBuilder,
	const FText& Label,
	const FText& Tooltip,
	ERigControlValueType ValueType,
	TAttribute<EVisibility> Visibility)
{
	CreateNumericValueWidgetRow<int32>(CategoryBuilder, Label, Tooltip, ValueType, Visibility);
}

void FRigControlElementDetails::CreateEnumValueWidgetRow(IDetailCategoryBuilder& CategoryBuilder, const FText& Label,
	const FText& Tooltip, ERigControlValueType ValueType, TAttribute<EVisibility> Visibility)
{
	const bool bCurrent = ValueType == ERigControlValueType::Current;
	const bool bInitial = ValueType == ERigControlValueType::Initial;
	
	TArray<FRigElementKey> Keys = GetElementKeys();
	URigHierarchy* HierarchyBeingDebugged = GetHierarchyBeingDebugged();
	URigHierarchy* HierarchyToChange = bCurrent ? HierarchyBeingDebugged : HierarchyBeingCustomized;
	Keys = HierarchyBeingDebugged->SortKeys(Keys);

	UEnum* Enum = nullptr;
	for(const FRigElementKey& Key : Keys)
	{
		if(const FRigControlElement* ControlElement = HierarchyBeingCustomized->Find<FRigControlElement>(Key))
		{
			Enum = ControlElement->Settings.ControlEnum.Get();
			if(Enum)
			{
				break;
			}
		}
	}

	check(Enum != nullptr);

	CategoryBuilder.AddCustomRow(Label)
	.Visibility(Visibility)
	.NameContent()
	[
		SNew(STextBlock)
		.Text(Label)
		.ToolTipText(Tooltip)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SEnumComboBox, Enum)
		.CurrentValue_Lambda([ValueType, Keys, HierarchyBeingDebugged]() -> int32
		{
			const int32 FirstValue = HierarchyBeingDebugged->GetControlValue<int32>(Keys[0], ValueType);
			for(int32 Index = 1; Index < Keys.Num(); Index++)
			{
				const int32 SecondValue = HierarchyBeingDebugged->GetControlValue<int32>(Keys[Index], ValueType);
				if(FirstValue != SecondValue)
				{
					return INDEX_NONE;
				}
			}
			return FirstValue;
		})
		.OnEnumSelectionChanged_Lambda([ValueType, Keys, HierarchyToChange](int32 NewSelection, ESelectInfo::Type)
		{
			if(NewSelection == INDEX_NONE)
			{
				return;
			}

			FScopedTransaction Transaction(LOCTEXT("ChangeValue", "Change Value"));
			HierarchyToChange->Modify();
			for(const FRigElementKey& Key : Keys)
			{
				HierarchyToChange->SetControlValue(Key, FRigControlValue::Make<int32>(NewSelection), ValueType, true, true); 
			}
		})
		.Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
	]
	.CopyAction(FUIAction(
	FExecuteAction::CreateLambda([ValueType, Keys, HierarchyBeingDebugged]()
		{
			const int32 FirstValue = HierarchyBeingDebugged->GetControlValue<int32>(Keys[0], ValueType);
			FPlatformApplicationMisc::ClipboardCopy(*FString::FromInt(FirstValue));
		}),
		FCanExecuteAction())
	)
	.PasteAction(FUIAction(
		FExecuteAction::CreateLambda([ValueType, Keys, HierarchyToChange]()
		{
			FString Content;
			FPlatformApplicationMisc::ClipboardPaste(Content);
			if(!Content.IsNumeric())
			{
				return;
			}

			const int32 Value = FCString::Atoi(*Content);
			FScopedTransaction Transaction(LOCTEXT("ChangeValue", "Change Value"));
			HierarchyToChange->Modify();

			for(const FRigElementKey& Key : Keys)
			{
				HierarchyToChange->SetControlValue(Key, FRigControlValue::Make<int32>(Value), ValueType, true, true); 
			}
		}),
		FCanExecuteAction())
	)
	.OverrideResetToDefault(FResetToDefaultOverride::Create(
		TAttribute<bool>::CreateLambda([ValueType, Keys, HierarchyBeingDebugged]() -> bool
		{
			const int32 FirstValue = HierarchyBeingDebugged->GetControlValue<int32>(Keys[0], ValueType);
			const int32 ReferenceValue = ValueType == ERigControlValueType::Initial ? 0 :
				HierarchyBeingDebugged->GetControlValue<int32>(Keys[0], ERigControlValueType::Initial);

			return FirstValue != ReferenceValue;
		}),
		FSimpleDelegate::CreateLambda([ValueType, Keys, HierarchyToChange]()
		{
			FScopedTransaction Transaction(LOCTEXT("ResetValueToDefault", "Reset Value To Default"));
			HierarchyToChange->Modify();
			for(const FRigElementKey& Key : Keys)
			{
				const int32 ReferenceValue = ValueType == ERigControlValueType::Initial ? 0 :
					HierarchyToChange->GetControlValue<int32>(Keys[0], ERigControlValueType::Initial);
				HierarchyToChange->SetControlValue(Key, FRigControlValue::Make<int32>(ReferenceValue), ValueType, true, true); 
			}
		})
	));
}

void FRigControlElementDetails::CreateVector2DValueWidgetRow(
	IDetailCategoryBuilder& CategoryBuilder, 
	const FText& Label, 
	const FText& Tooltip, 
	ERigControlValueType ValueType,
	TAttribute<EVisibility> Visibility)
{
	const bool bCurrent = ValueType == ERigControlValueType::Current;
	const bool bInitial = ValueType == ERigControlValueType::Initial;
	const bool bShowToggle = (ValueType == ERigControlValueType::Minimum) || (ValueType == ERigControlValueType::Maximum);
	
	TArray<FRigElementKey> Keys = GetElementKeys();
	URigHierarchy* HierarchyBeingDebugged = GetHierarchyBeingDebugged();
	URigHierarchy* HierarchyToChange = bCurrent ? HierarchyBeingDebugged : HierarchyBeingCustomized;
	Keys = HierarchyBeingDebugged->SortKeys(Keys);

	using SNumericVector2DInputBox = SNumericVectorInputBox<float, FVector2f, 2>;
	TSharedPtr<SNumericVector2DInputBox> VectorInputBox;
	
	FDetailWidgetRow& WidgetRow = CategoryBuilder.AddCustomRow(Label);
	TAttribute<ECheckBoxState> ToggleXChecked, ToggleYChecked;
	FOnCheckStateChanged OnToggleXChanged, OnToggleYChanged;

	if(bShowToggle)
	{
		auto ToggleChecked = [ValueType, Keys, HierarchyBeingDebugged](int32 Index) -> ECheckBoxState
		{
			TOptional<bool> FirstValue;

			for(const FRigElementKey& Key : Keys)
			{
				if(const FRigControlElement* ControlElement = HierarchyBeingDebugged->Find<FRigControlElement>(Key))
				{
					if(ControlElement->Settings.LimitEnabled.Num() == 2)
					{
						const bool Value = ControlElement->Settings.LimitEnabled[Index].GetForValueType(ValueType);
						if(FirstValue.IsSet())
						{
							if(FirstValue.GetValue() != Value)
							{
								return ECheckBoxState::Undetermined;
							}
						}
						else
						{
							FirstValue = Value;
						}
					}
				}
			}

			if(!ensure(FirstValue.IsSet()))
			{
				return ECheckBoxState::Undetermined;
			}

			return FirstValue.GetValue() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		};
		
		ToggleXChecked = TAttribute<ECheckBoxState>::CreateLambda([ToggleChecked]() -> ECheckBoxState
		{
			return ToggleChecked(0);
		});

		ToggleYChecked = TAttribute<ECheckBoxState>::CreateLambda([ToggleChecked]() -> ECheckBoxState
		{
			return ToggleChecked(1);
		});

		auto OnToggleChanged = [ValueType, Keys, HierarchyToChange](ECheckBoxState InValue, int32 Index)
		{
			if(InValue == ECheckBoxState::Undetermined)
			{
				return;
			}
					
			FScopedTransaction Transaction(LOCTEXT("ChangeLimitToggle", "Change Limit Toggle"));
			HierarchyToChange->Modify();

			for(const FRigElementKey& Key : Keys)
			{
				if(FRigControlElement* ControlElement = HierarchyToChange->Find<FRigControlElement>(Key))
				{
					if(ControlElement->Settings.LimitEnabled.Num() == 2)
					{
						ControlElement->Settings.LimitEnabled[Index].SetForValueType(ValueType, InValue == ECheckBoxState::Checked);
						HierarchyToChange->SetControlSettings(ControlElement, ControlElement->Settings, true, true, true);
					}
				}
			}
		};

		OnToggleXChanged = FOnCheckStateChanged::CreateLambda([OnToggleChanged](ECheckBoxState InValue)
		{
			OnToggleChanged(InValue, 0);
		});

		OnToggleYChanged = FOnCheckStateChanged::CreateLambda([OnToggleChanged](ECheckBoxState InValue)
		{
			OnToggleChanged(InValue, 1);
		});
	}

	auto GetValue = [ValueType, Keys, HierarchyBeingDebugged](int32 Component) -> TOptional<float>
	{
		const float FirstValue = HierarchyBeingDebugged->GetControlValue<FVector3f>(Keys[0], ValueType).Component(Component);
		for(int32 Index = 1; Index < Keys.Num(); Index++)
		{
			const float SecondValue = HierarchyBeingDebugged->GetControlValue<FVector3f>(Keys[Index], ValueType).Component(Component);
			if(FirstValue != SecondValue)
			{
				return TOptional<float>();
			}
		}
		return FirstValue;
	};

	auto OnValueChanged = [ValueType, Keys, HierarchyBeingDebugged, HierarchyToChange, this]
		(TOptional<float> InValue, ETextCommit::Type InCommitType, bool bSetupUndo, int32 Component)
		{
			if(!InValue.IsSet())
			{
				return;
			}

			const float Value = InValue.GetValue();
		
			for(const FRigElementKey& Key : Keys)
			{
				FVector3f Vector = HierarchyBeingDebugged->GetControlValue<FVector3f>(Key, ValueType);
				if(!FMath::IsNearlyEqual(Vector.Component(Component), Value))
				{
					if(!SliderTransaction.IsValid())
					{
						SliderTransaction = MakeShareable(new FScopedTransaction(NSLOCTEXT("ControlRigElementDetails", "ChangeValue", "Change Value")));
						HierarchyToChange->Modify();
					}
					Vector.Component(Component) = Value;
					HierarchyToChange->SetControlValue(Key, FRigControlValue::Make<FVector3f>(Vector), ValueType, bSetupUndo, bSetupUndo);
				};
			}

			if(bSetupUndo)
			{
				SliderTransaction.Reset();
			}
	};

	WidgetRow
	.Visibility(Visibility)
	.NameContent()
	[
		SNew(STextBlock)
		.Text(Label)
		.ToolTipText(Tooltip)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SAssignNew(VectorInputBox, SNumericVector2DInputBox)
        .Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
        .AllowSpin(ValueType == ERigControlValueType::Current || ValueType == ERigControlValueType::Initial)
		.SpinDelta(0.01f)
		.X_Lambda([GetValue]() -> TOptional<float>
        {
			return GetValue(0);
        })
        .Y_Lambda([GetValue]() -> TOptional<float>
		{
			return GetValue(1);
		})
		.OnXChanged_Lambda([OnValueChanged](TOptional<float> InValue)
		{
			OnValueChanged(InValue, ETextCommit::Default, false, 0);
		})
		.OnYChanged_Lambda([OnValueChanged](TOptional<float> InValue)
		{
			OnValueChanged(InValue, ETextCommit::Default, false, 1);
		})
		.OnXCommitted_Lambda([OnValueChanged](TOptional<float> InValue, ETextCommit::Type InCommitType)
		{
			OnValueChanged(InValue, InCommitType, true, 0);
		})
		.OnYCommitted_Lambda([OnValueChanged](TOptional<float> InValue, ETextCommit::Type InCommitType)
		{
			OnValueChanged(InValue, InCommitType, true, 1);
		})
		 .DisplayToggle(bShowToggle)
		 .ToggleXChecked(ToggleXChecked)
		 .ToggleYChecked(ToggleYChecked)
		 .OnToggleXChanged(OnToggleXChanged)
		 .OnToggleYChanged(OnToggleYChanged)
	]
	.CopyAction(FUIAction(
	FExecuteAction::CreateLambda([ValueType, Keys, HierarchyBeingDebugged]()
		{
			const FVector3f Data3 = HierarchyBeingDebugged->GetControlValue<FVector3f>(Keys[0], ValueType);
			const FVector2f Data(Data3.X, Data3.Y);
			FString Content = Data.ToString();
			FPlatformApplicationMisc::ClipboardCopy(*Content);
		}),
		FCanExecuteAction())
	)
	.PasteAction(FUIAction(
		FExecuteAction::CreateLambda([ValueType, Keys, HierarchyToChange]()
		{
			FString Content;
			FPlatformApplicationMisc::ClipboardPaste(Content);
			if(Content.IsEmpty())
			{
				return;
			}

			FVector2f Data = FVector2f::ZeroVector;
			Data.InitFromString(Content);

			FVector3f Data3(Data.X, Data.Y, 0);

			FScopedTransaction Transaction(NSLOCTEXT("ControlRigElementDetails", "ChangeValue", "Change Value"));
			HierarchyToChange->Modify();
			
			for(const FRigElementKey& Key : Keys)
			{
				HierarchyToChange->SetControlValue(Key, FRigControlValue::Make<FVector3f>(Data3), ValueType, true, true); 
			}
		}),
		FCanExecuteAction())
	);

	if(ValueType == ERigControlValueType::Current || ValueType == ERigControlValueType::Initial)
	{
		WidgetRow.OverrideResetToDefault(FResetToDefaultOverride::Create(
			TAttribute<bool>::CreateLambda([ValueType, Keys, HierarchyBeingDebugged]() -> bool
			{
				const FVector3f FirstValue = HierarchyBeingDebugged->GetControlValue<FVector3f>(Keys[0], ValueType);
				const FVector3f ReferenceValue = ValueType == ERigControlValueType::Initial ? FVector3f::ZeroVector :
					HierarchyBeingDebugged->GetControlValue<FVector3f>(Keys[0], ERigControlValueType::Initial);

				return !(FirstValue - ReferenceValue).IsNearlyZero();
			}),
			FSimpleDelegate::CreateLambda([ValueType, Keys, HierarchyToChange]()
			{
				FScopedTransaction Transaction(LOCTEXT("ResetValueToDefault", "Reset Value To Default"));
				HierarchyToChange->Modify();
				
				for(const FRigElementKey& Key : Keys)
				{
					const FVector3f ReferenceValue = ValueType == ERigControlValueType::Initial ? FVector3f::ZeroVector :
						HierarchyToChange->GetControlValue<FVector3f>(Keys[0], ERigControlValueType::Initial);
					HierarchyToChange->SetControlValue(Key, FRigControlValue::Make<FVector3f>(ReferenceValue), ValueType, true, true); 
				}
			})
		));
	}
}

void FRigNullElementDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FRigTransformElementDetails::CustomizeDetails(DetailBuilder);
	CustomizeTransform(DetailBuilder);
}

#undef LOCTEXT_NAMESPACE
