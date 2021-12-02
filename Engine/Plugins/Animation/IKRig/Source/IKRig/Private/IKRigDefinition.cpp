// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigDefinition.h"

#include "IKRigObjectVersion.h"
#include "Engine/SkeletalMesh.h"

#define LOCTEXT_NAMESPACE	"IKRigDefinition"

#if WITH_EDITOR

TOptional<FTransform::FReal> UIKRigEffectorGoal::GetNumericValue(
	ESlateTransformComponent::Type Component,
	ESlateRotationRepresentation::Type Representation,
	ESlateTransformSubComponent::Type SubComponent,
	EIKRigTransformType::Type TransformType) const
{
	switch(TransformType)
	{
		case EIKRigTransformType::Current:
		{
			return SAdvancedTransformInputBox<FTransform>::GetNumericValueFromTransform(
				CurrentTransform,
				Component,
				Representation,
				SubComponent);
		}
		case EIKRigTransformType::Reference:
		{
			return SAdvancedTransformInputBox<FTransform>::GetNumericValueFromTransform(
				InitialTransform,
				Component,
				Representation,
				SubComponent);
		}
		default:
		{
			break;
		}
	}
	return TOptional<FTransform::FReal>();
}

void UIKRigEffectorGoal::OnNumericValueChanged(ESlateTransformComponent::Type Component,
	ESlateRotationRepresentation::Type Representation, ESlateTransformSubComponent::Type SubComponent,
	FTransform::FReal Value, ETextCommit::Type CommitType, EIKRigTransformType::Type TransformType)
{
	Modify();

	switch(TransformType)
	{
		case EIKRigTransformType::Current:
		{
			SAdvancedTransformInputBox<FTransform>::ApplyNumericValueChange(
				CurrentTransform,
				Value,
				Component,
				Representation,
				SubComponent);
			break;
		}
		case EIKRigTransformType::Reference:
		{
			SAdvancedTransformInputBox<FTransform>::ApplyNumericValueChange(
				InitialTransform,
				Value,
				Component,
				Representation,
				SubComponent);
			break;
		}
		default:
		{
			break;
		}
	}
}

bool UIKRigEffectorGoal::TransformDiffersFromDefault(TSharedPtr<IPropertyHandle> PropertyHandle,
	ESlateTransformComponent::Type Component) const
{
	if(PropertyHandle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(UIKRigEffectorGoal, CurrentTransform))
	{
		switch(Component)
		{
			case ESlateTransformComponent::Location:
			{
				return !(CurrentTransform.GetLocation() - InitialTransform.GetLocation()).IsNearlyZero();
			}
			case ESlateTransformComponent::Rotation:
			{
				return !(CurrentTransform.Rotator() - InitialTransform.Rotator()).IsNearlyZero();
			}
			case ESlateTransformComponent::Scale:
			{
				return !(CurrentTransform.GetScale3D() - InitialTransform.GetScale3D()).IsNearlyZero();
			}
		}
	}
	return false;
}

void UIKRigEffectorGoal::ResetTransformToDefault(TSharedPtr<IPropertyHandle> PropertyHandle,
	ESlateTransformComponent::Type Component)
{
	switch(Component)
	{
		case ESlateTransformComponent::Location:
		{
			CurrentTransform.SetLocation(InitialTransform.GetLocation());
			break;
		}
		case ESlateTransformComponent::Rotation:
		{
			CurrentTransform.SetRotation(InitialTransform.GetRotation());
			break;
		}
		case ESlateTransformComponent::Scale:
		{
			CurrentTransform.SetScale3D(InitialTransform.GetScale3D());
			break;
		}
	}
}

#endif

FBoneChain* FRetargetDefinition::GetEditableBoneChainByName(FName ChainName)
{
	for (FBoneChain& Chain : BoneChains)
	{
		if (Chain.ChainName == ChainName)
		{
			return &Chain;
		}
	}
	
	return nullptr;
}

void UIKRigDefinition::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FIKRigObjectVersion::GUID);
}

const FBoneChain* UIKRigDefinition::GetRetargetChainByName(FName ChainName) const
{
	for (const FBoneChain& Chain : RetargetDefinition.BoneChains)
	{
		if (Chain.ChainName == ChainName)
		{
			return &Chain;
		}
	}
	
	return nullptr;
}

void UIKRigDefinition::SetPreviewMesh(USkeletalMesh* PreviewMesh, bool bMarkAsDirty)
{	
	PreviewSkeletalMesh = PreviewMesh;
}

USkeletalMesh* UIKRigDefinition::GetPreviewMesh() const
{
	return PreviewSkeletalMesh.Get();
}

#undef LOCTEXT_NAMESPACE
