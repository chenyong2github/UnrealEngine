// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigElementDetails.h"
#include "Widgets/SWidget.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SRotatorInputBox.h"
#include "Editor/SControlRigGizmoNameList.h"

#define LOCTEXT_NAMESPACE "ControlRigElementDetails"

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
	if (FRigHierarchyContainer* Hierarchy = GetHierarchy())
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
}

void FRigControlDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FRigElementDetails::CustomizeDetails(DetailBuilder);

	DetailBuilder.HideProperty(TEXT("ControlType"));
	DetailBuilder.HideProperty(TEXT("ParentName"));
	DetailBuilder.HideProperty(TEXT("SpaceName"));
	DetailBuilder.HideProperty(TEXT("InitialValue"));
	DetailBuilder.HideProperty(TEXT("Value"));
	DetailBuilder.HideProperty(TEXT("GizmoName"));

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

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("FRigElement"), LOCTEXT("ControlCategory", "Control"));
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

	if (ContainerBeingCustomized == nullptr || !ElementKeyBeingCustomized)
	{
		return;
	}

	FRigControl& Control = ContainerBeingCustomized->ControlHierarchy[ElementKeyBeingCustomized.Name];
	switch (Control.ControlType)
	{
		case ERigControlType::Transform:
		{
			TArray<FString> Labels = { TEXT("Initial"), TEXT("Current") };
			for(FString Label : Labels)
			{
				bool bIsInitial = Label == TEXT("Initial");
				Category.AddCustomRow(FText::FromString(Label))
				.NameContent()
				.VAlign(VAlign_Top)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Label))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				.MinDesiredWidth(125.0f * 3.0f) // copied from FComponentTransformDetails
				.MaxDesiredWidth(125.0f * 3.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					[
						SNew(SVectorInputBox)
						.AllowSpin(true)
						.bColorAxisLabels(true)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.X(this, &FRigControlDetails::GetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::TranslationX)
						.Y(this, &FRigControlDetails::GetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::TranslationY)
						.Z(this, &FRigControlDetails::GetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::TranslationZ)
						.OnXCommitted(this, &FRigControlDetails::SetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::TranslationX)
						.OnYCommitted(this, &FRigControlDetails::SetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::TranslationY)
						.OnZCommitted(this, &FRigControlDetails::SetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::TranslationZ)
					]
					+ SVerticalBox::Slot()
					[
						SNew(SRotatorInputBox)
						.AllowSpin(true)
						.bColorAxisLabels(true)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.Roll(this, &FRigControlDetails::GetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::RotationRoll)
						.Pitch(this, &FRigControlDetails::GetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::RotationPitch)
						.Yaw(this, &FRigControlDetails::GetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::RotationYaw)
						.OnRollCommitted(this, &FRigControlDetails::SetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::RotationRoll)
						.OnPitchCommitted(this, &FRigControlDetails::SetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::RotationPitch)
						.OnYawCommitted(this, &FRigControlDetails::SetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::RotationYaw)
					]
					+ SVerticalBox::Slot()
					[
						SNew(SVectorInputBox)
						.AllowSpin(true)
						.bColorAxisLabels(true)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.X(this, &FRigControlDetails::GetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::ScaleX)
						.Y(this, &FRigControlDetails::GetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::ScaleY)
						.Z(this, &FRigControlDetails::GetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::ScaleZ)
						.OnXCommitted(this, &FRigControlDetails::SetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::ScaleX)
						.OnYCommitted(this, &FRigControlDetails::SetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::ScaleY)
						.OnZCommitted(this, &FRigControlDetails::SetComponentValue, bIsInitial, ERigElementDetailsTransformComponent::ScaleZ)
					]
				];
			}
			break;
		}
		default:
		{
			ensure(false);
			break;
		}
	}

	Category.AddCustomRow(FText::FromString(TEXT("GizmoName")))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString(TEXT("Gizmo Name")))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SControlRigGizmoNameList, &Control, BlueprintBeingCustomized)
		.OnGetNameListContent(this, &FRigControlDetails::GetGizmoNameList)
	];
}

TOptional<float> FRigControlDetails::GetComponentValue(bool bInitial, ERigElementDetailsTransformComponent Component) const
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
				case ERigControlType::Transform:
				{
					FTransform Transform = bInitial ? Control.InitialValue.Get<FTransform>() : Control.Value.Get<FTransform>();
					return GetTransformComponent(Transform, Component);
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

void FRigControlDetails::SetComponentValue(float InNewValue, ETextCommit::Type InCommitType, bool bInitial, ERigElementDetailsTransformComponent Component)
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
				case ERigControlType::Transform:
				{
					FTransform Transform = Value.Get<FTransform>();
					SetTransformComponent(Transform, Component, InNewValue);
					Value.Set<FTransform>(Transform);
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
				if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
				{
					((FRigControlHierarchy*)&DebuggedRig->GetControlHierarchy())->SetInitialValue(ElementKeyBeingCustomized.Name, Value);
				}
			}
			else
			{
				ContainerBeingCustomized->ControlHierarchy.SetValue(ElementKeyBeingCustomized.Name, Value);
				if (UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
				{
					((FRigControlHierarchy*)&DebuggedRig->GetControlHierarchy())->SetValue(ElementKeyBeingCustomized.Name, Value);
				}
			}
		}
	}
}

const TArray<TSharedPtr<FString>>& FRigControlDetails::GetGizmoNameList() const
{
	return GizmoNameList;
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
