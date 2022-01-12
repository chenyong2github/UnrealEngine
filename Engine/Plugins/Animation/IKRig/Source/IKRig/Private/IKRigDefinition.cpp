// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigDefinition.h"

#include "IKRigObjectVersion.h"
#include "Engine/SkeletalMesh.h"

#if WITH_EDITOR

#include "HAL/PlatformApplicationMisc.h"

#endif

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

void UIKRigEffectorGoal::OnCopyToClipboard(ESlateTransformComponent::Type Component, EIKRigTransformType::Type TransformType)
{
	const FTransform Xfo = TransformType == EIKRigTransformType::Current ? CurrentTransform : InitialTransform;
	
	FString Content;
	switch(Component)
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
			TBaseStructure<FTransform>::Get()->ExportText(Content, &Xfo, &Xfo, nullptr, PPF_None, nullptr);
			break;
		}
	}

	if(!Content.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*Content);
	}
}

void UIKRigEffectorGoal::OnPasteFromClipboard(ESlateTransformComponent::Type Component, EIKRigTransformType::Type TransformType)
{
	FString Content;
	FPlatformApplicationMisc::ClipboardPaste(Content);

	if(Content.IsEmpty())
	{
		return;
	}

	FTransform& Xfo = TransformType == EIKRigTransformType::Current ? CurrentTransform : InitialTransform;
	
	Modify();

	class FIKRigEffectorGoalErrorPipe : public FOutputDevice
	{
	public:

		int32 NumErrors;

		FIKRigEffectorGoalErrorPipe()
			: FOutputDevice()
			, NumErrors(0)
		{
		}

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
		{
			NumErrors++;
		}
	};

	FIKRigEffectorGoalErrorPipe ErrorPipe;
				
	switch(Component)
	{
		case ESlateTransformComponent::Location:
		{
			FVector Data = Xfo.GetLocation();
			TBaseStructure<FVector>::Get()->ImportText(*Content, &Data, nullptr, PPF_None, &ErrorPipe, TBaseStructure<FVector>::Get()->GetName(), true);
			if(ErrorPipe.NumErrors == 0)
			{
				Xfo.SetLocation(Data);
			}
			break;
		}
		case ESlateTransformComponent::Rotation:
		{
			FRotator Data = Xfo.Rotator();
			TBaseStructure<FRotator>::Get()->ImportText(*Content, &Data, nullptr, PPF_None, &ErrorPipe, TBaseStructure<FRotator>::Get()->GetName(), true);
			if(ErrorPipe.NumErrors == 0)
			{
				Xfo.SetRotation(FQuat(Data));
			}
			break;
		}
		case ESlateTransformComponent::Scale:
		{
			FVector Data = Xfo.GetScale3D();
			TBaseStructure<FVector>::Get()->ImportText(*Content, &Data, nullptr, PPF_None, &ErrorPipe, TBaseStructure<FVector>::Get()->GetName(), true);
			if(ErrorPipe.NumErrors == 0)
			{
				Xfo.SetScale3D(Data);
			}
			break;
		}
		case ESlateTransformComponent::Max:
		default:
		{
			FTransform Data = Xfo;
			TBaseStructure<FTransform>::Get()->ImportText(*Content, &Data, nullptr, PPF_None, &ErrorPipe, TBaseStructure<FTransform>::Get()->GetName(), true);
			if(ErrorPipe.NumErrors == 0)
			{
				Xfo = Data;
			}
			break;
		}
	}
}

bool UIKRigEffectorGoal::TransformDiffersFromDefault(
	ESlateTransformComponent::Type Component,
	TSharedPtr<IPropertyHandle> PropertyHandle) const
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

void UIKRigEffectorGoal::ResetTransformToDefault(
	ESlateTransformComponent::Type Component,
	TSharedPtr<IPropertyHandle> PropertyHandle)
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
