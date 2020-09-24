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
#include "SEnumCombobox.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"

#define LOCTEXT_NAMESPACE "ControlRigElementDetails"

void FRigElementKeyDetails::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	BlueprintBeingCustomized = nullptr;

	TArray<UObject*> Objects;
	InStructPropertyHandle->GetOuterObjects(Objects);
	for (UObject* Object : Objects)
	{
		if (Object->IsA<UControlRigBlueprint>())
		{
			BlueprintBeingCustomized = Cast<UControlRigBlueprint>(Object);
			if (BlueprintBeingCustomized)
			{
				break;
			}
		}
	}

	if (BlueprintBeingCustomized == nullptr)
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
					BlueprintBeingCustomized = Cast<UControlRigBlueprint>(Rig->GetClass()->ClassGeneratedBy);
					if (BlueprintBeingCustomized)
					{
						break;
					}
				}
			}

			if (BlueprintBeingCustomized)
			{
				break;
			}
		}
	}

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
	TSharedPtr<FString> CurrentlySelected = GetCurrentlySelectedItem(InStructOnScope, InProperty, InNameList);
	Widget->SetSelectedItem(CurrentlySelected);
}

void FRigElementDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.HideProperty(TEXT("Index"), FRigElement::StaticStruct());
	DetailBuilder.HideProperty(TEXT("Name"), FRigElement::StaticStruct());

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
						ContainerBeingCustomized = &BlueprintBeingCustomized->HierarchyContainer;
						if (UControlRig* DebuggedControlRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
						{
							if (!DebuggedControlRig->IsSetupModeEnabled())
							{
								ContainerBeingCustomized = DebuggedControlRig->GetHierarchy();
							}
						}
						break;
					}
				}
			}

			if (ContainerBeingCustomized)
			{
				ElementKeyBeingCustomized = ((const FRigElement*)StructBeingCustomized->GetStructMemory())->GetElementKey();
				break;
			}
		}
	}
}

void FRigElementDetails::SetName(const FText& InNewText, ETextCommit::Type InCommitType)
{
	FRigHierarchyContainer* Hierarchy = nullptr;
	if (BlueprintBeingCustomized)
	{
		Hierarchy = &BlueprintBeingCustomized->HierarchyContainer;
	}
	else
	{
		Hierarchy = GetHierarchy();
	}

	if (Hierarchy)
	{
		switch (ElementKeyBeingCustomized.Type)
		{
			case ERigElementType::Bone:
			{
				ElementKeyBeingCustomized.Name = Hierarchy->BoneHierarchy.Rename(ElementKeyBeingCustomized.Name, *InNewText.ToString());
				break;
			}
			case ERigElementType::Control:
			{
				ElementKeyBeingCustomized.Name = Hierarchy->ControlHierarchy.Rename(ElementKeyBeingCustomized.Name, *InNewText.ToString());
				break;
			}
			case ERigElementType::Space:
			{
				ElementKeyBeingCustomized.Name = Hierarchy->SpaceHierarchy.Rename(ElementKeyBeingCustomized.Name, *InNewText.ToString());
				break;
			}
			case ERigElementType::Curve:
			{
				ElementKeyBeingCustomized.Name = Hierarchy->CurveContainer.Rename(ElementKeyBeingCustomized.Name, *InNewText.ToString());
				break;
			}
			default:
			{
				ensure(false);
				break;
			}
		}
	}
}

float FRigElementDetails::GetTransformComponent(const FTransform& InTransform, ERigElementDetailsTransformComponent InComponent)
{
	switch (InComponent)
	{
		case ERigElementDetailsTransformComponent::TranslationX:
		{
			return InTransform.GetTranslation().X;
		}
		case ERigElementDetailsTransformComponent::TranslationY:
		{
			return InTransform.GetTranslation().Y;
		}
		case ERigElementDetailsTransformComponent::TranslationZ:
		{
			return InTransform.GetTranslation().Z;
		}
		case ERigElementDetailsTransformComponent::RotationRoll:
		{
			return InTransform.GetRotation().Rotator().Roll;
		}
		case ERigElementDetailsTransformComponent::RotationPitch:
		{
			return InTransform.GetRotation().Rotator().Pitch;
		}
		case ERigElementDetailsTransformComponent::RotationYaw:
		{
			return InTransform.GetRotation().Rotator().Yaw;
		}
		case ERigElementDetailsTransformComponent::ScaleX:
		{
			return InTransform.GetScale3D().X;
		}
		case ERigElementDetailsTransformComponent::ScaleY:
		{
			return InTransform.GetScale3D().Y;
		}
		case ERigElementDetailsTransformComponent::ScaleZ:
		{
			return InTransform.GetScale3D().Z;
		}
	}
	return 0.f;
}

void FRigElementDetails::SetTransformComponent(FTransform& OutTransform, ERigElementDetailsTransformComponent InComponent, float InNewValue)
{
	switch (InComponent)
	{
		case ERigElementDetailsTransformComponent::TranslationX:
		{
			FVector Translation = OutTransform.GetTranslation();
			Translation.X = InNewValue;
			OutTransform.SetTranslation(Translation);
			break;
		}
		case ERigElementDetailsTransformComponent::TranslationY:
		{
			FVector Translation = OutTransform.GetTranslation();
			Translation.Y = InNewValue;
			OutTransform.SetTranslation(Translation);
			break;
		}
		case ERigElementDetailsTransformComponent::TranslationZ:
		{
			FVector Translation = OutTransform.GetTranslation();
			Translation.Z = InNewValue;
			OutTransform.SetTranslation(Translation);
			break;
		}
		case ERigElementDetailsTransformComponent::RotationRoll:
		{
			FRotator Rotator = OutTransform.Rotator();
			Rotator.Roll = InNewValue;
			OutTransform.SetRotation(FQuat(Rotator));
			break;
		}
		case ERigElementDetailsTransformComponent::RotationPitch:
		{
			FRotator Rotator = OutTransform.Rotator();
			Rotator.Pitch = InNewValue;
			OutTransform.SetRotation(FQuat(Rotator));
			break;
		}
		case ERigElementDetailsTransformComponent::RotationYaw:
		{
			FRotator Rotator = OutTransform.Rotator();
			Rotator.Yaw = InNewValue;
			OutTransform.SetRotation(FQuat(Rotator));
			break;
		}
		case ERigElementDetailsTransformComponent::ScaleX:
		{
			FVector Scale = OutTransform.GetScale3D();
			Scale.X = InNewValue;
			OutTransform.SetScale3D(Scale);
			break;
		}
		case ERigElementDetailsTransformComponent::ScaleY:
		{
			FVector Scale = OutTransform.GetScale3D();
			Scale.Y = InNewValue;
			OutTransform.SetScale3D(Scale);
			break;
		}
		case ERigElementDetailsTransformComponent::ScaleZ:
		{
			FVector Scale = OutTransform.GetScale3D();
			Scale.Z = InNewValue;
			OutTransform.SetScale3D(Scale);
			break;
		}
	}
}

float FRigElementDetails::GetEulerTransformComponent(const FEulerTransform& InTransform, ERigElementDetailsTransformComponent InComponent)
{
	switch (InComponent)
	{
		case ERigElementDetailsTransformComponent::TranslationX:
		{
			return InTransform.Location.X;
		}
		case ERigElementDetailsTransformComponent::TranslationY:
		{
			return InTransform.Location.Y;
		}
		case ERigElementDetailsTransformComponent::TranslationZ:
		{
			return InTransform.Location.Z;
		}
		case ERigElementDetailsTransformComponent::RotationRoll:
		{
			return InTransform.Rotation.Roll;
		}
		case ERigElementDetailsTransformComponent::RotationPitch:
		{
			return InTransform.Rotation.Pitch;
		}
		case ERigElementDetailsTransformComponent::RotationYaw:
		{
			return InTransform.Rotation.Yaw;
		}
		case ERigElementDetailsTransformComponent::ScaleX:
		{
			return InTransform.Scale.X;
		}
		case ERigElementDetailsTransformComponent::ScaleY:
		{
			return InTransform.Scale.Y;
		}
		case ERigElementDetailsTransformComponent::ScaleZ:
		{
			return InTransform.Scale.Z;
		}
	}
	return 0.f;
}

void FRigElementDetails::SetEulerTransformComponent(FEulerTransform& OutTransform, ERigElementDetailsTransformComponent InComponent, float InNewValue)
{
	switch (InComponent)
	{
		case ERigElementDetailsTransformComponent::TranslationX:
		{
			FVector Translation = OutTransform.Location;
			Translation.X = InNewValue;
			OutTransform.Location = Translation;
			break;
		}
		case ERigElementDetailsTransformComponent::TranslationY:
		{
			FVector Translation = OutTransform.Location;
			Translation.Y = InNewValue;
			OutTransform.Location = Translation;
			break;
		}
		case ERigElementDetailsTransformComponent::TranslationZ:
		{
			FVector Translation = OutTransform.Location;
			Translation.Z = InNewValue;
			OutTransform.Location = Translation;
			break;
		}
		case ERigElementDetailsTransformComponent::RotationRoll:
		{
			FRotator Rotator = OutTransform.Rotation;
			Rotator.Roll = InNewValue;
			OutTransform.Rotation = Rotator;
			break;
		}
		case ERigElementDetailsTransformComponent::RotationPitch:
		{
			FRotator Rotator = OutTransform.Rotation;
			Rotator.Pitch = InNewValue;
			OutTransform.Rotation = Rotator;
			break;
		}
		case ERigElementDetailsTransformComponent::RotationYaw:
		{
			FRotator Rotator = OutTransform.Rotation;
			Rotator.Yaw = InNewValue;
			OutTransform.Rotation = Rotator;
			break;
		}
		case ERigElementDetailsTransformComponent::ScaleX:
		{
			FVector Scale = OutTransform.Scale;
			Scale.X = InNewValue;
			OutTransform.Scale = Scale;
			break;
		}
		case ERigElementDetailsTransformComponent::ScaleY:
		{
			FVector Scale = OutTransform.Scale;
			Scale.Y = InNewValue;
			OutTransform.Scale = Scale;
			break;
		}
		case ERigElementDetailsTransformComponent::ScaleZ:
		{
			FVector Scale = OutTransform.Scale;
			Scale.Z = InNewValue;
			OutTransform.Scale = Scale;
			break;
		}
	}
}


void FRigBoneDetails::CustomizeDetails( IDetailLayoutBuilder& DetailBuilder )
{
	FRigElementDetails::CustomizeDetails(DetailBuilder);
	DetailBuilder.HideProperty(TEXT("ParentName"));

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("FRigElement"), LOCTEXT("BoneCategory", "Bone"));
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
		.Text(this, &FRigElementDetails::GetName)
		.OnTextCommitted(this, &FRigElementDetails::SetName)
	];

	// setup influences modifier
	{
		FName InfluencesEventName = FRigUnit_BeginExecution::EventName;
		FRigInfluenceMap InfluenceMap = BlueprintBeingCustomized->Influences.FindOrAdd(InfluencesEventName).Inverse();
		FRigInfluenceEntryModifier TempModifier = InfluenceMap.GetEntryModifier(ElementKeyBeingCustomized);
		InfluenceModifier = MakeShareable(new FRigInfluenceEntryModifier(TempModifier));
		InfluenceModifierStruct = MakeShareable(new FStructOnScope(FRigInfluenceEntryModifier::StaticStruct(), (uint8*)InfluenceModifier.Get()));
		InfluenceModifierStruct->SetPackage(BlueprintBeingCustomized->GetOutermost());

		IDetailCategoryBuilder& InversionCategory = DetailBuilder.EditCategory(TEXT("Inversion"), LOCTEXT("InversionCategory", "Inversion"));
		FProperty* AffectedListProperty = FRigInfluenceEntryModifier::StaticStruct()->FindPropertyByName(TEXT("AffectedList"));

		IDetailPropertyRow* AffectedListRow = InversionCategory.AddExternalStructureProperty(InfluenceModifierStruct, AffectedListProperty->GetFName());
		AffectedListRow->ShouldAutoExpand(true);
		AffectedListRow->DisplayName(LOCTEXT("AffectingElements", "Affecting Elements"));

		FSimpleDelegate OnInfluencesChangedDelegate = FSimpleDelegate::CreateSP(this, &FRigBoneDetails::OnAffectedListChanged, AffectedListProperty, DetailBuilder.GetPropertyUtilities());

		TSharedPtr<IPropertyHandle> AffectedListHandle = AffectedListRow->GetPropertyHandle();
		AffectedListHandle->SetOnPropertyValueChanged(OnInfluencesChangedDelegate);
		AffectedListHandle->SetOnChildPropertyValueChanged(OnInfluencesChangedDelegate);
	}
}

void FRigBoneDetails::OnStructContentsChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	FPropertyChangedEvent ChangeEvent(InProperty, EPropertyChangeType::ValueSet);
	PropertyUtilities->NotifyFinishedChangingProperties(ChangeEvent);
}

void FRigBoneDetails::OnAffectedListChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	if (BlueprintBeingCustomized != nullptr && InfluenceModifier.IsValid())
	{
		BlueprintBeingCustomized->Modify();

		FName InfluencesEventName = FRigUnit_BeginExecution::EventName;
		FRigInfluenceMap InverseInfluenceMap = BlueprintBeingCustomized->Influences.FindOrAdd(InfluencesEventName).Inverse();
		InverseInfluenceMap.SetEntryModifier(ElementKeyBeingCustomized, *InfluenceModifier);
		BlueprintBeingCustomized->Influences.FindOrAdd(InfluencesEventName) = InverseInfluenceMap.Inverse();
	}

	OnStructContentsChanged(InProperty, PropertyUtilities);
}

TArray<TSharedPtr<FString>> FRigControlDetails::ControlTypeList;

void FRigControlDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FRigElementDetails::CustomizeDetails(DetailBuilder);

	DetailBuilder.HideProperty(TEXT("ControlType"));
	DetailBuilder.HideProperty(TEXT("DisplayName"));
	DetailBuilder.HideProperty(TEXT("ParentName"));
	DetailBuilder.HideProperty(TEXT("SpaceName"));
	DetailBuilder.HideProperty(TEXT("OffsetTransform"));
	DetailBuilder.HideProperty(TEXT("InitialValue"));
	DetailBuilder.HideProperty(TEXT("Value"));
	DetailBuilder.HideProperty(TEXT("bLimitTranslation"));
	DetailBuilder.HideProperty(TEXT("bLimitRotation"));
	DetailBuilder.HideProperty(TEXT("bLimitScale"));
	DetailBuilder.HideProperty(TEXT("bDrawLimits"));
	DetailBuilder.HideProperty(TEXT("MinimumValue"));
	DetailBuilder.HideProperty(TEXT("MaximumValue"));
	DetailBuilder.HideProperty(TEXT("bIsTransientControl"));
	DetailBuilder.HideProperty(TEXT("ControlEnum"));

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

	IDetailCategoryBuilder& ControlCategory = DetailBuilder.EditCategory(TEXT("Control"), LOCTEXT("ControlCategory", "Control"));
	IDetailCategoryBuilder& LimitsCategory = DetailBuilder.EditCategory(TEXT("Limits"), LOCTEXT("LimitsCategory", "Limits"));
	IDetailCategoryBuilder& GizmoCategory = DetailBuilder.EditCategory(TEXT("Gizmo"), LOCTEXT("GizmoCategory", "Gizmo"));

	ControlCategory.InitiallyCollapsed(false);
	LimitsCategory.InitiallyCollapsed(false);
	GizmoCategory.InitiallyCollapsed(false);

	ControlCategory.AddCustomRow(FText::FromString(TEXT("Name")))
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
		.Text(this, &FRigElementDetails::GetName)
		.OnTextCommitted(this, &FRigElementDetails::SetName)
	];

	ControlCategory.AddCustomRow(FText::FromString(TEXT("DisplayName")))
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
		.Text(this, &FRigControlDetails::GetDisplayName)
		.OnTextCommitted(this, &FRigControlDetails::SetDisplayName, DetailBuilder.GetPropertyUtilities())
	];

	if (ContainerBeingCustomized == nullptr || !ElementKeyBeingCustomized)
	{
		return;
	}

	if (ControlTypeList.Num() == 0)
	{
		UEnum* Enum = StaticEnum<ERigControlType>();
		for (int64 Index = 0; Index < Enum->GetMaxEnumValue(); Index++)
		{
			ControlTypeList.Add(MakeShared<FString>(Enum->GetDisplayNameTextByValue(Index).ToString()));
		}
	}

	UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
	UEnum* ValueTypeEnum = StaticEnum<ERigControlValueType>();
	FRigControl& Control = ContainerBeingCustomized->ControlHierarchy[ElementKeyBeingCustomized.Name];

	// when control type changes, we have to refresh detail panel
	TSharedRef<IPropertyHandle> ControlType = DetailBuilder.GetProperty(TEXT("ControlType"));
	ControlType->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda(
		[this, &DetailBuilder]()
		{
			DetailBuilder.ForceRefreshDetails();

			if (this->ContainerBeingCustomized && this->ElementKeyBeingCustomized.IsValid())
			{
				FRigControl& Control = this->ContainerBeingCustomized->ControlHierarchy[this->ElementKeyBeingCustomized.Name];

				switch (Control.ControlType)
				{
					case ERigControlType::Bool:
					{
						Control.Value = Control.InitialValue = FRigControlValue::Make<bool>(false);
						break;
					}
					case ERigControlType::Float:
					{
						Control.Value = Control.InitialValue = FRigControlValue::Make<float>(0.f);
						Control.bLimitTranslation = true;
						Control.MinimumValue = FRigControlValue::Make<float>(0.f);
						Control.MaximumValue = FRigControlValue::Make<float>(1.f);
						break;
					}
					case ERigControlType::Integer:
					{
						Control.Value = Control.InitialValue = FRigControlValue::Make<int32>(0);
						Control.bLimitTranslation = true;
						Control.MinimumValue = FRigControlValue::Make<int32>(0);
						Control.MaximumValue = FRigControlValue::Make<int32>(10);
						break;
					}
					case ERigControlType::Vector2D:
					{
						Control.Value = Control.InitialValue = FRigControlValue::Make<FVector2D>(FVector2D::ZeroVector);
						Control.bLimitTranslation = true;
						Control.MinimumValue = FRigControlValue::Make<FVector2D>(FVector2D::ZeroVector);
						Control.MaximumValue = FRigControlValue::Make<FVector2D>(FVector2D(1.f, 1.f));
						break;
					}
					case ERigControlType::Position:
					{
						Control.Value = Control.InitialValue = FRigControlValue::Make<FVector>(FVector::ZeroVector);
						Control.MinimumValue = FRigControlValue::Make<FVector>(-FVector::OneVector);
						Control.MaximumValue = FRigControlValue::Make<FVector>(FVector::OneVector);
						break;
					}
					case ERigControlType::Scale:
					{
						Control.Value = Control.InitialValue = FRigControlValue::Make<FVector>(FVector::OneVector);
						Control.MinimumValue = FRigControlValue::Make<FVector>(FVector::ZeroVector);
						Control.MaximumValue = FRigControlValue::Make<FVector>(FVector::OneVector);
						break;
					}
					case ERigControlType::Rotator:
					{
						Control.Value = Control.InitialValue = FRigControlValue::Make<FRotator>(FRotator::ZeroRotator);
						Control.MinimumValue = FRigControlValue::Make<FRotator>(FRotator::ZeroRotator);
						Control.MaximumValue = FRigControlValue::Make<FRotator>(FRotator(180.f, 180.f, 180.f));
						break;
					}
					case ERigControlType::Transform:
					{
						Control.Value = Control.InitialValue = Control.MinimumValue = Control.MaximumValue = FRigControlValue::Make<FTransform>(FTransform::Identity);
						break;
					}
					case ERigControlType::TransformNoScale:
					{
						FTransformNoScale Identity = FTransform::Identity;
						// Helge todo FTransformNoScale::Identity appears to be no good?
						Control.Value = Control.InitialValue = Control.MinimumValue = Control.MaximumValue = FRigControlValue::Make<FTransformNoScale>(Identity);
						break;
					}
					case ERigControlType::EulerTransform:
					{
						FEulerTransform Identity = FEulerTransform::Identity;
						// Helge todo FTransformNoScale::Identity appears to be no good?
						Control.Value = Control.InitialValue = Control.MinimumValue = Control.MaximumValue = FRigControlValue::Make<FEulerTransform>(Identity);
						break;
					}
					default:
					{
						ensure(false);
						break;
					}
				}

				if (this->ContainerBeingCustomized != &this->BlueprintBeingCustomized->HierarchyContainer)
				{
					this->BlueprintBeingCustomized->HierarchyContainer.ControlHierarchy[Control.Index] = Control;
				}
				this->BlueprintBeingCustomized->PropagateHierarchyFromBPToInstances(false, false);
				this->ContainerBeingCustomized->ControlHierarchy.OnControlUISettingsChanged.Broadcast(this->ContainerBeingCustomized, this->ElementKeyBeingCustomized);
			}
		}
	));

	ControlCategory.AddCustomRow(FText::FromString(TEXT("ControlType")))
	.NameContent()
	[
		ControlType->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		ControlType->CreatePropertyValueWidget()
	];

	if(Control.ControlType != ERigControlType::Bool)
	{
		TSharedRef<IPropertyHandle> OffsetTransform = DetailBuilder.GetProperty(TEXT("OffsetTransform"));
		OffsetTransform->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda(
			[this, OffsetTransform, &DetailBuilder]()
			{
				this->BlueprintBeingCustomized->PropagatePropertyFromBPToInstances(this->GetElementKey(), OffsetTransform->GetProperty());
			}
		));

		uint8* ValuePtr = (uint8*)&Control.OffsetTransform;
		const UStruct* ValueStruct = TBaseStructure<FTransform>::Get();

		TSharedPtr<FStructOnScope> StructToDisplay = MakeShareable(new FStructOnScope(ValueStruct, ValuePtr));
		FProperty* Property = OffsetTransform->GetProperty();

		IDetailPropertyRow* Row = ControlCategory.AddExternalStructure(StructToDisplay);
		Row->DisplayName(OffsetTransform->GetPropertyDisplayName());
		Row->ToolTip(Property->GetToolTipText());
		Row->ShouldAutoExpand(true);
		Row->IsEnabled(true);

		FSimpleDelegate OnStructContentsChangedDelegate = FSimpleDelegate::CreateSP(this, &FRigControlDetails::OnStructContentsChanged, Property, DetailBuilder.GetPropertyUtilities());

		TSharedPtr<IPropertyHandle> Handle = Row->GetPropertyHandle();
		Handle->SetOnPropertyValueChanged(OnStructContentsChangedDelegate);
		Handle->SetOnChildPropertyValueChanged(OnStructContentsChangedDelegate);
	}

	FString ControlTypeName = ControlTypeEnum->GetDisplayNameTextByValue((int64)Control.ControlType).ToString();

	switch (Control.ControlType)
	{
		case ERigControlType::Bool:
		{
			for(int64 ValueTypeIndex = 0; ValueTypeIndex < ValueTypeEnum->GetMaxEnumValue(); ValueTypeIndex++)
			{
				ERigControlValueType ValueType = (ERigControlValueType)ValueTypeIndex;

				if (ValueType == ERigControlValueType::Minimum ||
					ValueType == ERigControlValueType::Maximum)
				{
					continue;
				}

				FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue(ValueTypeIndex).ToString();
				FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s %s"), *ValueTypeName, *ControlTypeName));

				ControlCategory.AddCustomRow(PropertyLabel)
				.NameContent()
				.VAlign(VAlign_Top)
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
						.IsChecked(this, &FRigControlDetails::GetComponentValueBool, ValueType == ERigControlValueType::Initial)
						.OnCheckStateChanged(this, &FRigControlDetails::SetComponentValueBool, ValueType == ERigControlValueType::Initial, DetailBuilder.GetPropertyUtilities())
					]
				];
			}
			break;
		}
		case ERigControlType::Float:
		{
			for(int64 ValueTypeIndex = 0; ValueTypeIndex < ValueTypeEnum->GetMaxEnumValue(); ValueTypeIndex++)
			{
				ERigControlValueType ValueType = (ERigControlValueType)ValueTypeIndex;
				FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue(ValueTypeIndex).ToString();
				FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s %s"), *ValueTypeName, *ControlTypeName));

				if (ValueType == ERigControlValueType::Minimum)
				{
					LimitsCategory.AddCustomRow(FText::FromString(TEXT("Limit")))
					.NameContent()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Limit")))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
					.ValueContent()
					[
						DetailBuilder.GetProperty(TEXT("bLimitTranslation"))->CreatePropertyValueWidget()
					];

					LimitsCategory.AddCustomRow(FText::FromString(TEXT("bDrawLimits")))
					.NameContent()
					[
						DetailBuilder.GetProperty(TEXT("bDrawLimits"))->CreatePropertyNameWidget()
					]
					.ValueContent()
					[
						DetailBuilder.GetProperty(TEXT("bDrawLimits"))->CreatePropertyValueWidget()
					];
				}

				IDetailCategoryBuilder& Category = (ValueType == ERigControlValueType::Minimum || ValueType == ERigControlValueType::Maximum) ? LimitsCategory : ControlCategory;
				Category.AddCustomRow(PropertyLabel)
				.NameContent()
				.VAlign(VAlign_Top)
				[
					SNew(STextBlock)
					.Text(PropertyLabel)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.IsEnabled(this, &FRigControlDetails::IsEnabled, ValueType)
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
						.IsEnabled(this, &FRigControlDetails::IsEnabled, ValueType)
						.Value(this, &FRigControlDetails::GetComponentValueFloat, ValueType, ERigElementDetailsTransformComponent::TranslationX)
						.OnValueChanged(this, &FRigControlDetails::SetComponentValueFloat, ValueType, ERigElementDetailsTransformComponent::TranslationX, DetailBuilder.GetPropertyUtilities())
					]
				];
			}
			break;
		}
		case ERigControlType::Integer:
		{
			for(int64 ValueTypeIndex = 0; ValueTypeIndex < ValueTypeEnum->GetMaxEnumValue(); ValueTypeIndex++)
			{
				ERigControlValueType ValueType = (ERigControlValueType)ValueTypeIndex;
				FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue(ValueTypeIndex).ToString();
				FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s %s"), *ValueTypeName, *ControlTypeName));

				// skip min and max if this is an enum
				if (Control.ControlEnum != nullptr &&
					(
						ValueType == ERigControlValueType::Minimum ||
						ValueType == ERigControlValueType::Maximum
						))
				{
					continue;
				}

				if (ValueType == ERigControlValueType::Minimum)
				{
					LimitsCategory.AddCustomRow(FText::FromString(TEXT("Limit")))
					.NameContent()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Limit")))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
					.ValueContent()
					[
						DetailBuilder.GetProperty(TEXT("bLimitTranslation"))->CreatePropertyValueWidget()
					];

					LimitsCategory.AddCustomRow(FText::FromString(TEXT("bDrawLimits")))
					.NameContent()
					[
						DetailBuilder.GetProperty(TEXT("bDrawLimits"))->CreatePropertyNameWidget()
					]
					.ValueContent()
					[
						DetailBuilder.GetProperty(TEXT("bDrawLimits"))->CreatePropertyValueWidget()
					];
				}

				IDetailCategoryBuilder& Category = (ValueType == ERigControlValueType::Minimum || ValueType == ERigControlValueType::Maximum) ? LimitsCategory : ControlCategory;

				if (Control.ControlEnum)
				{
					Category.AddCustomRow(PropertyLabel)
					.NameContent()
					.VAlign(VAlign_Top)
					[
						SNew(STextBlock)
						.Text(PropertyLabel)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.IsEnabled(this, &FRigControlDetails::IsEnabled, ValueType)
					]
					.ValueContent()
					.MinDesiredWidth(125.0f * 3.0f) // copied from FComponentTransformDetails
					.MaxDesiredWidth(125.0f * 3.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						[
							SNew(SEnumComboBox, Control.ControlEnum)
							.IsEnabled(this, &FRigControlDetails::IsEnabled, ValueType)
							.OnEnumSelectionChanged(SEnumComboBox::FOnEnumSelectionChanged::CreateSP(this, &FRigControlDetails::OnControlEnumChanged, ValueType, DetailBuilder.GetPropertyUtilities()))
							.CurrentValue(this, &FRigControlDetails::GetControlEnumValue, ValueType)
							.Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
						]
					];
				}
				else
				{
					Category.AddCustomRow(PropertyLabel)
					.NameContent()
					.VAlign(VAlign_Top)
					[
						SNew(STextBlock)
						.Text(PropertyLabel)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.IsEnabled(this, &FRigControlDetails::IsEnabled, ValueType)
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
							.IsEnabled(this, &FRigControlDetails::IsEnabled, ValueType)
							.Value(this, &FRigControlDetails::GetComponentValueInteger, ValueType)
							.OnValueChanged(this, &FRigControlDetails::SetComponentValueInteger, ValueType, DetailBuilder.GetPropertyUtilities())
						]
					];
				}
			}
			break;
		}
		case ERigControlType::Vector2D:
		case ERigControlType::Position:
		case ERigControlType::Scale:
		case ERigControlType::Rotator:
		case ERigControlType::Transform:
		case ERigControlType::TransformNoScale:
		case ERigControlType::EulerTransform:
		{
			for(int64 ValueTypeIndex = 0; ValueTypeIndex < ValueTypeEnum->GetMaxEnumValue(); ValueTypeIndex++)
			{
				ERigControlValueType ValueType = (ERigControlValueType)ValueTypeIndex;
				FString ValueTypeName = ValueTypeEnum->GetDisplayNameTextByValue(ValueTypeIndex).ToString();
				FText PropertyLabel = FText::FromString(FString::Printf(TEXT("%s %s"), *ValueTypeName, *ControlTypeName));

				if (ValueType == ERigControlValueType::Minimum)
				{
					FText LimitTranslationLabel = FText::FromString(TEXT("Limit"));
					FText LimiRotationLabel = LimitTranslationLabel;
					bool bShowLimitRotation = Control.ControlType == ERigControlType::Rotator;

					if (Control.ControlType == ERigControlType::Transform ||
						Control.ControlType == ERigControlType::TransformNoScale)
					{
						LimitTranslationLabel = FText::FromString(TEXT("Limit Translation"));
						LimiRotationLabel = FText::FromString(TEXT("Limit Rotation"));
						bShowLimitRotation = true;
					}

					if (Control.ControlType != ERigControlType::Rotator)
					{
						LimitsCategory.AddCustomRow(LimitTranslationLabel)
						.NameContent()
						[
							SNew(STextBlock)
							.Text(LimitTranslationLabel)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						]
						.ValueContent()
						[
							DetailBuilder.GetProperty(TEXT("bLimitTranslation"))->CreatePropertyValueWidget()
						];
					}


					if (bShowLimitRotation)
					{
						LimitsCategory.AddCustomRow(LimiRotationLabel)
						.NameContent()
						[
							SNew(STextBlock)
							.Text(LimiRotationLabel)
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
						.ValueContent()
						[
							DetailBuilder.GetProperty(TEXT("bLimitRotation"))->CreatePropertyValueWidget()
						];
					}

					LimitsCategory.AddCustomRow(FText::FromString(TEXT("bDrawLimits")))
					.NameContent()
					[
						DetailBuilder.GetProperty(TEXT("bDrawLimits"))->CreatePropertyNameWidget()
					]
					.ValueContent()
					[
						DetailBuilder.GetProperty(TEXT("bDrawLimits"))->CreatePropertyValueWidget()
					];
				}

				uint8* ValuePtr = (uint8*)&Control.GetValue(ValueType);

				const UStruct* ValueStruct = nullptr;
				switch (Control.ControlType)
				{
					case ERigControlType::Vector2D:
					{
						ValueStruct = TBaseStructure<FVector2D>::Get();
						break;
					}
					case ERigControlType::Position:
					case ERigControlType::Scale:
					{
						ValueStruct = TBaseStructure<FVector>::Get();
						break;
					}
					case ERigControlType::Rotator:
					{
						ValueStruct = TBaseStructure<FRotator>::Get();
						break;
					}
					case ERigControlType::Transform:
					{
						ValueStruct = TBaseStructure<FTransform>::Get();
						break;
					}
					case ERigControlType::TransformNoScale:
					{
						ValueStruct = TBaseStructure<FTransformNoScale>::Get();
						break;
					}
					case ERigControlType::EulerTransform:
					{
						ValueStruct = TBaseStructure<FEulerTransform>::Get();
						break;
					}
					default:
					{
						checkNoEntry();
					}
				}

				TSharedPtr<FStructOnScope> StructToDisplay = MakeShareable(new FStructOnScope(ValueStruct, ValuePtr));

				IDetailCategoryBuilder& Category = (ValueType == ERigControlValueType::Minimum || ValueType == ERigControlValueType::Maximum) ? LimitsCategory : ControlCategory;
				FProperty* Property = FRigControl::FindPropertyForValueType(ValueType);

				IDetailPropertyRow* Row = Category.AddExternalStructure(StructToDisplay);
				Row->DisplayName(PropertyLabel);
				Row->ToolTip(Property->GetToolTipText());
				Row->ShouldAutoExpand(true);
				Row->IsEnabled(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FRigControlDetails::IsEnabled, ValueType)));

				FSimpleDelegate OnStructContentsChangedDelegate = FSimpleDelegate::CreateSP(this, &FRigControlDetails::OnStructContentsChanged, Property, DetailBuilder.GetPropertyUtilities());

				TSharedPtr<IPropertyHandle> Handle = Row->GetPropertyHandle();
				Handle->SetOnPropertyValueChanged(OnStructContentsChangedDelegate);
				Handle->SetOnChildPropertyValueChanged(OnStructContentsChangedDelegate);
			}
			break;
		}
		default:
		{
			ensure(false);
			break;
		}
	}


	switch (Control.ControlType)
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
			DetailBuilder.HideProperty(TEXT("bGizmoEnabled"));
			DetailBuilder.HideProperty(TEXT("bGizmoVisible"));
			DetailBuilder.HideProperty(TEXT("GizmoName"));

			GizmoCategory.AddCustomRow(FText::FromString(TEXT("bGizmoEnabled")))
			.NameContent()
			[
				DetailBuilder.GetProperty(TEXT("bGizmoEnabled"))->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				DetailBuilder.GetProperty(TEXT("bGizmoEnabled"))->CreatePropertyValueWidget()
			];

			GizmoCategory.AddCustomRow(FText::FromString(TEXT("GizmoName")))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Gizmo Name")))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.IsEnabled(this, &FRigControlDetails::IsGizmoEnabled)
			]
			.ValueContent()
			[
				SNew(SControlRigGizmoNameList, &Control, BlueprintBeingCustomized)
				.OnGetNameListContent(this, &FRigControlDetails::GetGizmoNameList)
				.IsEnabled(this, &FRigControlDetails::IsGizmoEnabled)
			];

			GizmoCategory.AddCustomRow(FText::FromString(TEXT("bGizmoVisible")))
			.NameContent()
			[
				DetailBuilder.GetProperty(TEXT("bGizmoVisible"))->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				DetailBuilder.GetProperty(TEXT("bGizmoVisible"))->CreatePropertyValueWidget()
			];

			break;
		}
		default:
		{
			DetailBuilder.HideProperty(TEXT("bGizmoEnabled"));
			DetailBuilder.HideProperty(TEXT("bGizmoVisible"));
			DetailBuilder.HideProperty(TEXT("GizmoName"));
			DetailBuilder.HideProperty(TEXT("GizmoTransform"));
			DetailBuilder.HideProperty(TEXT("GizmoColor"));
			break;
		}
	}

	switch (Control.ControlType)
	{
		case ERigControlType::Float:
		case ERigControlType::Integer:
		case ERigControlType::Vector2D:
		{
			break;
		}
		default:
		{
			DetailBuilder.HideProperty(TEXT("PrimaryAxis"));
			break;
		}
	}

	if (Control.ControlType == ERigControlType::Integer)
	{
		TSharedRef<IPropertyHandle> ControlEnum = DetailBuilder.GetProperty(TEXT("ControlEnum"));
		ControlEnum->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda(
			[this, &DetailBuilder]()
			{
				DetailBuilder.ForceRefreshDetails();

				if (this->ContainerBeingCustomized != nullptr && this->ElementKeyBeingCustomized)
				{
					int32 Index = ContainerBeingCustomized->ControlHierarchy.GetIndex(ElementKeyBeingCustomized.Name);
					if (Index != INDEX_NONE)
					{
						UEnum* ControlEnum = ContainerBeingCustomized->ControlHierarchy[Index].ControlEnum;
						if (ControlEnum != nullptr)
						{
							int32 Maximum = (int32)ControlEnum->GetMaxEnumValue() - 1;
							ContainerBeingCustomized->ControlHierarchy[Index].MinimumValue.Set<int32>(0);
							ContainerBeingCustomized->ControlHierarchy[Index].MaximumValue.Set<int32>(Maximum);

							FRigControlValue InitialValue = ContainerBeingCustomized->ControlHierarchy[Index].InitialValue;
							FRigControlValue Value = ContainerBeingCustomized->ControlHierarchy[Index].Value;

							ContainerBeingCustomized->ControlHierarchy[Index].ApplyLimits(InitialValue);
							ContainerBeingCustomized->ControlHierarchy[Index].ApplyLimits(Value);

							ContainerBeingCustomized->ControlHierarchy.SetValue(ElementKeyBeingCustomized.Name, InitialValue, ERigControlValueType::Initial);
							ContainerBeingCustomized->ControlHierarchy.SetValue(ElementKeyBeingCustomized.Name, Value, ERigControlValueType::Current);

							if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
							{
								FRigControlHierarchy& DebuggedControlHierarchy = *((FRigControlHierarchy*)&DebuggedRig->GetControlHierarchy());
								DebuggedControlHierarchy[Index].MinimumValue.Set<int32>(0);
								DebuggedControlHierarchy[Index].MaximumValue.Set<int32>(Maximum);
								DebuggedControlHierarchy.SetValue(ElementKeyBeingCustomized.Name, InitialValue, ERigControlValueType::Initial);
								DebuggedControlHierarchy.SetValue(ElementKeyBeingCustomized.Name, Value, ERigControlValueType::Current);
							}
						}
					}
				}
			}
		));

		ControlCategory.AddCustomRow(FText::FromString(TEXT("ControlEnum")))
		.NameContent()
		[
			ControlEnum->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			ControlEnum->CreatePropertyValueWidget()
		];
	}

	// setup influences modifier
	{
		FName InfluencesEventName = FRigUnit_BeginExecution::EventName;
		FRigInfluenceMap& InfluenceMap = BlueprintBeingCustomized->Influences.FindOrAdd(InfluencesEventName);
		FRigInfluenceEntryModifier TempModifier = InfluenceMap.GetEntryModifier(ElementKeyBeingCustomized);
		InfluenceModifier = MakeShareable(new FRigInfluenceEntryModifier(TempModifier));
		InfluenceModifierStruct = MakeShareable(new FStructOnScope(FRigInfluenceEntryModifier::StaticStruct(), (uint8*)InfluenceModifier.Get()));
		InfluenceModifierStruct->SetPackage(BlueprintBeingCustomized->GetOutermost());

		IDetailCategoryBuilder& InversionCategory = DetailBuilder.EditCategory(TEXT("Inversion"), LOCTEXT("InversionCategory", "Inversion"));
		FProperty* AffectedListProperty = FRigInfluenceEntryModifier::StaticStruct()->FindPropertyByName(TEXT("AffectedList"));

		IDetailPropertyRow* AffectedListRow = InversionCategory.AddExternalStructureProperty(InfluenceModifierStruct, AffectedListProperty->GetFName());
		AffectedListRow->ShouldAutoExpand(true);
		AffectedListRow->DisplayName(LOCTEXT("AffectedElements", "Affected Elements"));

		FSimpleDelegate OnInfluencesChangedDelegate = FSimpleDelegate::CreateSP(this, &FRigControlDetails::OnAffectedListChanged, AffectedListProperty, DetailBuilder.GetPropertyUtilities());

		TSharedPtr<IPropertyHandle> AffectedListHandle = AffectedListRow->GetPropertyHandle();
		AffectedListHandle->SetOnPropertyValueChanged(OnInfluencesChangedDelegate);
		AffectedListHandle->SetOnChildPropertyValueChanged(OnInfluencesChangedDelegate);
	}
}

void FRigControlDetails::OnStructContentsChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	FPropertyChangedEvent ChangeEvent(InProperty, EPropertyChangeType::ValueSet);
	PropertyUtilities->NotifyFinishedChangingProperties(ChangeEvent);
}

void FRigControlDetails::OnAffectedListChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	if (BlueprintBeingCustomized != nullptr && InfluenceModifier.IsValid())
	{
		BlueprintBeingCustomized->Modify();

		FName InfluencesEventName = FRigUnit_BeginExecution::EventName;
		FRigInfluenceMap& InfluenceMap = BlueprintBeingCustomized->Influences.FindOrAdd(InfluencesEventName);
		InfluenceMap.SetEntryModifier(ElementKeyBeingCustomized, *InfluenceModifier);
	}

	OnStructContentsChanged(InProperty, PropertyUtilities);
}

int32 FRigControlDetails::GetControlEnumValue(ERigControlValueType InValueType) const
{
	TOptional<int32> Value = GetComponentValueInteger(InValueType);
	if (Value.IsSet())
	{
		return Value.GetValue();
	}
	return 0;
}

void FRigControlDetails::OnControlEnumChanged(int32 InValue, ESelectInfo::Type InSelectInfo, ERigControlValueType InValueType, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	if (ContainerBeingCustomized != nullptr && ElementKeyBeingCustomized)
	{
		int32 Index = ContainerBeingCustomized->ControlHierarchy.GetIndex(ElementKeyBeingCustomized.Name);
		if (Index != INDEX_NONE)
		{
			FRigControlValue Value = FRigControlValue::Make<int32>(InValue);
			ContainerBeingCustomized->ControlHierarchy.SetValue(ElementKeyBeingCustomized.Name, Value, InValueType);
			OnStructContentsChanged(FRigControl::FindPropertyForValueType(InValueType), PropertyUtilities);
		}
	}
}

FText FRigControlDetails::GetDisplayName() const
{
	if (ContainerBeingCustomized != nullptr && ElementKeyBeingCustomized)
	{
		int32 Index = ContainerBeingCustomized->ControlHierarchy.GetIndex(ElementKeyBeingCustomized.Name);
		if (Index != INDEX_NONE)
		{
			FRigControl Control = ContainerBeingCustomized->ControlHierarchy[ElementKeyBeingCustomized.Name];
			if (Control.DisplayName.IsNone())
			{
				return FText();
			}
			return FText::FromName(Control.GetDisplayName());
		}
	}
	return FText();
}

void FRigControlDetails::SetDisplayName(const FText& InNewText, ETextCommit::Type InCommitType, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	if (ContainerBeingCustomized != nullptr && ElementKeyBeingCustomized)
	{
		int32 Index = ContainerBeingCustomized->ControlHierarchy.GetIndex(ElementKeyBeingCustomized.Name);
		if (Index != INDEX_NONE)
		{
			FRigControl& Control = ContainerBeingCustomized->ControlHierarchy[ElementKeyBeingCustomized.Name];

			FString NewDisplayName = InNewText.ToString().TrimStartAndEnd();
			if (NewDisplayName.IsEmpty())
			{
				Control.DisplayName = FName(NAME_None);
			}
			else
			{
				Control.DisplayName = *NewDisplayName;
			}

			OnStructContentsChanged(FRigControl::StaticStruct()->FindPropertyByName(TEXT("DisplayName")), PropertyUtilities);
		}
	}
}

ECheckBoxState FRigControlDetails::GetComponentValueBool(bool bInitial) const
{
	if (ContainerBeingCustomized != nullptr && ElementKeyBeingCustomized)
	{
		int32 Index = ContainerBeingCustomized->ControlHierarchy.GetIndex(ElementKeyBeingCustomized.Name);
		if (Index != INDEX_NONE)
		{
			FRigControl Control = ContainerBeingCustomized->ControlHierarchy[ElementKeyBeingCustomized.Name];
			if (!bInitial)
			{
				if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
				{
					Control = DebuggedRig->GetControlHierarchy()[Index];
				}
			}

			switch (Control.ControlType)
			{
				case ERigControlType::Bool:
				{
					bool Value = bInitial ? Control.InitialValue.Get<bool>() : Control.Value.Get<bool>();
					return Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}
				default:
				{
					ensure(false);
					break;
				}
			}
		}
	}
	return ECheckBoxState::Unchecked;
}

void FRigControlDetails::SetComponentValueBool(ECheckBoxState InNewValue, bool bInitial, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	if (ContainerBeingCustomized != nullptr && ElementKeyBeingCustomized)
	{
		int32 Index = ContainerBeingCustomized->ControlHierarchy.GetIndex(ElementKeyBeingCustomized.Name);
		if (Index != INDEX_NONE)
		{
			const FRigControl& Control = ContainerBeingCustomized->ControlHierarchy[ElementKeyBeingCustomized.Name];
			FRigControlValue Value = bInitial ? Control.InitialValue : Control.Value;

			switch (Control.ControlType)
			{
				case ERigControlType::Bool:
				{
					Value.Set<bool>(InNewValue == ECheckBoxState::Checked);
					break;
				}
				default:
				{
					ensure(false);
					break;
				}
			}

			if (bInitial)
			{
				ContainerBeingCustomized->ControlHierarchy.SetInitialValue(ElementKeyBeingCustomized.Name, Value);
				OnStructContentsChanged(FRigControl::FindPropertyForValueType(ERigControlValueType::Initial), PropertyUtilities);
			}
			else
			{
				ContainerBeingCustomized->ControlHierarchy.SetValue(ElementKeyBeingCustomized.Name, Value);
				OnStructContentsChanged(FRigControl::FindPropertyForValueType(ERigControlValueType::Current), PropertyUtilities);
			}
		}
	}
}

TOptional<float> FRigControlDetails::GetComponentValueFloat(ERigControlValueType InValueType, ERigElementDetailsTransformComponent Component) const
{
	if (ContainerBeingCustomized != nullptr && ElementKeyBeingCustomized)
	{
		int32 Index = ContainerBeingCustomized->ControlHierarchy.GetIndex(ElementKeyBeingCustomized.Name);
		if (Index != INDEX_NONE)
		{
			FRigControl Control = ContainerBeingCustomized->ControlHierarchy[ElementKeyBeingCustomized.Name];
			if (InValueType == ERigControlValueType::Current)
			{
				if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
				{
					Control = DebuggedRig->GetControlHierarchy()[Index];
				}
			}

			switch (Control.ControlType)
			{
				case ERigControlType::Float:
				{
					return Control.GetValue(InValueType).Get<float>();
				}
				case ERigControlType::Integer:
				{
					return (float)Control.GetValue(InValueType).Get<int32>();
				}
				case ERigControlType::Vector2D:
				{
					switch (Component)
					{
						case ERigElementDetailsTransformComponent::TranslationX:
						{
							return Control.GetValue(InValueType).Get<FVector2D>().X;
						}
						case ERigElementDetailsTransformComponent::TranslationY:
						{
							return Control.GetValue(InValueType).Get<FVector2D>().Y;
						}
						default:
						{
							break;
						}
					}
					break;
				}
				case ERigControlType::Position:
				{
					switch (Component)
					{
						case ERigElementDetailsTransformComponent::TranslationX:
						{
							return Control.GetValue(InValueType).Get<FVector>().X;
						}
						case ERigElementDetailsTransformComponent::TranslationY:
						{
							return Control.GetValue(InValueType).Get<FVector>().Y;
						}
						case ERigElementDetailsTransformComponent::TranslationZ:
						{
							return Control.GetValue(InValueType).Get<FVector>().Z;
						}
						default:
						{
							break;
						}
					}
					break;
				}
				case ERigControlType::Scale:
				{
					switch (Component)
					{
						case ERigElementDetailsTransformComponent::ScaleX:
						{
							return Control.GetValue(InValueType).Get<FVector>().X;
						}
						case ERigElementDetailsTransformComponent::ScaleY:
						{
							return Control.GetValue(InValueType).Get<FVector>().Y;
						}
						case ERigElementDetailsTransformComponent::ScaleZ:
						{
							return Control.GetValue(InValueType).Get<FVector>().Z;
						}
						default:
						{
							break;
						}
					}
					break;
				}
				case ERigControlType::Rotator:
				{
					switch (Component)
					{
						case ERigElementDetailsTransformComponent::RotationPitch:
						{
							return Control.GetValue(InValueType).Get<FRotator>().Pitch;
						}
						case ERigElementDetailsTransformComponent::RotationYaw:
						{
							return Control.GetValue(InValueType).Get<FRotator>().Yaw;
						}
						case ERigElementDetailsTransformComponent::RotationRoll:
						{
							return Control.GetValue(InValueType).Get<FRotator>().Roll;
						}
						default:
						{
							break;
						}
					}
					break;
				}
				case ERigControlType::Transform:
				{
					FTransform Transform = Control.GetValue(InValueType).Get<FTransform>();
					return GetTransformComponent(Transform, Component);
				}
				case ERigControlType::TransformNoScale:
				{
					FTransform Transform = Control.GetValue(InValueType).Get<FTransformNoScale>();
					return GetTransformComponent(Transform, Component);
				}
				case ERigControlType::EulerTransform:
				{
					FEulerTransform Transform = Control.GetValue(InValueType).Get<FEulerTransform>();
					return GetEulerTransformComponent(Transform, Component);
				}
				case ERigControlType::Bool:
				{
					break;
				}
				default:
				{
					ensure(false);
					break;
				}
			}
		}
	}
	return 0.f;
}

void FRigControlDetails::SetComponentValueFloat(float InNewValue, ETextCommit::Type InCommitType, ERigControlValueType InValueType, ERigElementDetailsTransformComponent Component, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	SetComponentValueFloat(InNewValue, InValueType, Component, PropertyUtilities);
}

void FRigControlDetails::SetComponentValueFloat(float InNewValue, ERigControlValueType InValueType, ERigElementDetailsTransformComponent Component, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	if (ContainerBeingCustomized != nullptr && ElementKeyBeingCustomized)
	{
		int32 Index = ContainerBeingCustomized->ControlHierarchy.GetIndex(ElementKeyBeingCustomized.Name);
		if (Index != INDEX_NONE)
		{
			const FRigControl& Control = ContainerBeingCustomized->ControlHierarchy[ElementKeyBeingCustomized.Name];
			FRigControlValue Value = Control.GetValue(InValueType);

			switch (Control.ControlType)
			{
				case ERigControlType::Float:
				{
					Value.Set<float>(InNewValue);
					break;
				}
				case ERigControlType::Vector2D:
				{
					FVector2D Local = Value.Get<FVector2D>();
					switch (Component)
					{
						case ERigElementDetailsTransformComponent::TranslationX:
						{
							Local.X = InNewValue;
							break;
						}
						case ERigElementDetailsTransformComponent::TranslationY:
						{
							Local.Y = InNewValue;
							break;
						}
						default:
						{
							break;
						}
					}
					Value.Set<FVector2D>(Local);
					break;
				}
				case ERigControlType::Position:
				{
					FVector Local = Value.Get<FVector>();
					switch (Component)
					{
						case ERigElementDetailsTransformComponent::TranslationX:
						{
							Local.X = InNewValue;
							break;
						}
						case ERigElementDetailsTransformComponent::TranslationY:
						{
							Local.Y = InNewValue;
							break;
						}
						case ERigElementDetailsTransformComponent::TranslationZ:
						{
							Local.Z = InNewValue;
							break;
						}
						default:
						{
							break;
						}
					}
					Value.Set<FVector>(Local);
					break;
				}
				case ERigControlType::Scale:
				{
					FVector Local = Value.Get<FVector>();
					switch (Component)
					{
						case ERigElementDetailsTransformComponent::ScaleX:
						{
							Local.X = InNewValue;
							break;
						}
						case ERigElementDetailsTransformComponent::ScaleY:
						{
							Local.Y = InNewValue;
							break;
						}
						case ERigElementDetailsTransformComponent::ScaleZ:
						{
							Local.Z = InNewValue;
							break;
						}
						default:
						{
							break;
						}
					}
					Value.Set<FVector>(Local);
					break;
				}
				case ERigControlType::Rotator:
				{
					FRotator Local = Value.Get<FRotator>();
					switch (Component)
					{
						case ERigElementDetailsTransformComponent::RotationPitch:
						{
							Local.Pitch = InNewValue;
							break;
						}
						case ERigElementDetailsTransformComponent::RotationYaw:
						{
							Local.Yaw = InNewValue;
							break;
						}
						case ERigElementDetailsTransformComponent::RotationRoll:
						{
							Local.Roll = InNewValue;
							break;
						}
						default:
						{
							break;
						}
					}
					Value.Set<FRotator>(Local);
					break;
				}
				case ERigControlType::Transform:
				{
					FTransform Transform = Value.Get<FTransform>();
					SetTransformComponent(Transform, Component, InNewValue);
					Value.Set<FTransform>(Transform);
					break;
				}
				case ERigControlType::TransformNoScale:
				{
					FTransform Transform = Value.Get<FTransformNoScale>();
					SetTransformComponent(Transform, Component, InNewValue);
					Value.Set<FTransformNoScale>(Transform);
					break;
				}
				case ERigControlType::EulerTransform:
				{
					FEulerTransform Transform = Value.Get<FEulerTransform>();
					SetEulerTransformComponent(Transform, Component, InNewValue);
					Value.Set<FEulerTransform>(Transform);
					break;
				}
				case ERigControlType::Bool:
				{
					return;
				}
				default:
				{
					ensure(false);
					break;
				}
			}

			ContainerBeingCustomized->ControlHierarchy.SetValue(ElementKeyBeingCustomized.Name, Value, InValueType);
			OnStructContentsChanged(FRigControl::FindPropertyForValueType(InValueType), PropertyUtilities);
		}
	}
}

TOptional<int32> FRigControlDetails::GetComponentValueInteger(ERigControlValueType InValueType) const
{
	if (ContainerBeingCustomized != nullptr && ElementKeyBeingCustomized)
	{
		int32 Index = ContainerBeingCustomized->ControlHierarchy.GetIndex(ElementKeyBeingCustomized.Name);
		if (Index != INDEX_NONE)
		{
			FRigControl Control = ContainerBeingCustomized->ControlHierarchy[ElementKeyBeingCustomized.Name];
			if (InValueType == ERigControlValueType::Current)
			{
				if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
				{
					Control = DebuggedRig->GetControlHierarchy()[Index];
				}
			}

			switch (Control.ControlType)
			{
				case ERigControlType::Integer:
				{
					return Control.GetValue(InValueType).Get<int32>();
				}
				default:
				{
					break;
				}
			}
		}
	}
	return 0;
}

void FRigControlDetails::SetComponentValueInteger(int32 InNewValue, ERigControlValueType InValueType, const TSharedRef<IPropertyUtilities> PropertyUtilities)
{
	if (ContainerBeingCustomized != nullptr && ElementKeyBeingCustomized)
	{
		int32 Index = ContainerBeingCustomized->ControlHierarchy.GetIndex(ElementKeyBeingCustomized.Name);
		if (Index != INDEX_NONE)
		{
			const FRigControl& Control = ContainerBeingCustomized->ControlHierarchy[ElementKeyBeingCustomized.Name];
			FRigControlValue Value = Control.GetValue(InValueType);

			switch (Control.ControlType)
			{
				case ERigControlType::Integer:
				{
					Value.Set<int32>(InNewValue);
					break;
				}
				default:
				{
					ensure(false);
					break;
				}
			}

			ContainerBeingCustomized->ControlHierarchy.SetValue(ElementKeyBeingCustomized.Name, Value, InValueType);
			OnStructContentsChanged(FRigControl::FindPropertyForValueType(InValueType), PropertyUtilities);
		}
	}
}

bool FRigControlDetails::IsGizmoEnabled() const
{
	if (ContainerBeingCustomized != nullptr && ElementKeyBeingCustomized)
	{
		int32 Index = ContainerBeingCustomized->ControlHierarchy.GetIndex(ElementKeyBeingCustomized.Name);
		if (Index != INDEX_NONE)
		{
			const FRigControl& Control = ContainerBeingCustomized->ControlHierarchy[ElementKeyBeingCustomized.Name];
			return Control.bGizmoEnabled;
		}
	}
	return false;
}

bool FRigControlDetails::IsEnabled(ERigControlValueType InValueType) const
{
	switch (InValueType)
	{
		case ERigControlValueType::Minimum:
		case ERigControlValueType::Maximum:
		{
			if (ContainerBeingCustomized != nullptr && ElementKeyBeingCustomized)
			{
				int32 Index = ContainerBeingCustomized->ControlHierarchy.GetIndex(ElementKeyBeingCustomized.Name);
				if (Index != INDEX_NONE)
				{
					const FRigControl& Control = ContainerBeingCustomized->ControlHierarchy[ElementKeyBeingCustomized.Name];
					return Control.bLimitTranslation || Control.bLimitRotation || Control.bLimitScale;
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

const TArray<TSharedPtr<FString>>& FRigControlDetails::GetGizmoNameList() const
{
	return GizmoNameList;
}

const TArray<TSharedPtr<FString>>& FRigControlDetails::GetControlTypeList() const
{
	return ControlTypeList;
}

void FRigSpaceDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FRigElementDetails::CustomizeDetails(DetailBuilder);

	DetailBuilder.HideProperty(TEXT("SpaceType"));
	DetailBuilder.HideProperty(TEXT("ParentName"));

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("FRigElement"), LOCTEXT("SpaceCategory", "Space"));
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
		.Text(this, &FRigElementDetails::GetName)
		.OnTextCommitted(this, &FRigElementDetails::SetName)
	];
}

#undef LOCTEXT_NAMESPACE
