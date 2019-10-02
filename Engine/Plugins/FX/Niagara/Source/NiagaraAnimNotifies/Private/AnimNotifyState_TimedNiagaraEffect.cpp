// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimNotifyState_TimedNiagaraEffect.h"

#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"

#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"

UAnimNotifyState_TimedNiagaraEffect::UAnimNotifyState_TimedNiagaraEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Template = nullptr;
	LocationOffset.Set(0.0f, 0.0f, 0.0f);
	RotationOffset = FRotator(0.0f, 0.0f, 0.0f);
}

UFXSystemComponent* UAnimNotifyState_TimedNiagaraEffect::SpawnEffect(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	// Only spawn if we've got valid params
	if (ValidateParameters(MeshComp))
	{
		UFXSystemComponent* NewComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(Template, MeshComp, SocketName, LocationOffset, RotationOffset, EAttachLocation::KeepRelativeOffset, !bDestroyAtEnd);
		return NewComponent;
	}
	return nullptr;
}

void UAnimNotifyState_TimedNiagaraEffect::NotifyBegin(USkeletalMeshComponent * MeshComp, class UAnimSequenceBase * Animation, float TotalDuration)
{
	SpawnEffect(MeshComp, Animation);
	Received_NotifyBegin(MeshComp, Animation, TotalDuration);
}

void UAnimNotifyState_TimedNiagaraEffect::NotifyTick(USkeletalMeshComponent * MeshComp, class UAnimSequenceBase * Animation, float FrameDeltaTime)
{
	Received_NotifyTick(MeshComp, Animation, FrameDeltaTime);
}

void UAnimNotifyState_TimedNiagaraEffect::NotifyEnd(USkeletalMeshComponent * MeshComp, class UAnimSequenceBase * Animation)
{
	TArray<USceneComponent*> Children;
	MeshComp->GetChildrenComponents(false, Children);

	for (USceneComponent* Component : Children)
	{
		if (UFXSystemComponent* FXSystemComponent = Cast<UFXSystemComponent>(Component))
		{
			bool bSocketMatch = FXSystemComponent->GetAttachSocketName() == SocketName;
			bool bTemplateMatch = FXSystemComponent->GetFXSystemAsset() == Template;

#if WITH_EDITORONLY_DATA
			// In editor someone might have changed our parameters while we're ticking; so check 
			// previous known parameters too.
			bSocketMatch |= PreviousSocketNames.Contains(FXSystemComponent->GetAttachSocketName());
			bTemplateMatch |= PreviousTemplates.Contains(FXSystemComponent->GetFXSystemAsset());
#endif

			if (bSocketMatch && bTemplateMatch && FXSystemComponent->IsActive())
			{
				// Either destroy the component or deactivate it to have it's active FXSystems finish.
				// The component will auto destroy once all FXSystem are gone.
				if (bDestroyAtEnd)
				{
					FXSystemComponent->DestroyComponent();
				}
				else
				{
					FXSystemComponent->Deactivate();
				}

#if WITH_EDITORONLY_DATA
				// No longer need to track previous values as we've found our component
				// and removed it.
				PreviousTemplates.Empty();
				PreviousSocketNames.Empty();
#endif
				// Removed a component, no need to continue
				break;
			}
		}
	}

	Received_NotifyEnd(MeshComp, Animation);
}

bool UAnimNotifyState_TimedNiagaraEffect::ValidateParameters(USkeletalMeshComponent* MeshComp)
{
	bool bValid = true;

	if (!Template)
	{
		bValid = false;
	}
	else if (!MeshComp->DoesSocketExist(SocketName) && MeshComp->GetBoneIndex(SocketName) == INDEX_NONE)
	{
		bValid = false;
	}

	return bValid;
}

FString UAnimNotifyState_TimedNiagaraEffect::GetNotifyName_Implementation() const
{
	if (Template)
	{
		return Template->GetName();
	}

	return UAnimNotifyState::GetNotifyName_Implementation();
}

#if WITH_EDITOR
void UAnimNotifyState_TimedNiagaraEffect::PreEditChange(UProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange)
	{
		if (PropertyAboutToChange->GetName() == GET_MEMBER_NAME_STRING_CHECKED(UAnimNotifyState_TimedNiagaraEffect, Template) && Template != NULL)
		{
			PreviousTemplates.Add(Template);
		}

		if (PropertyAboutToChange->GetName() == GET_MEMBER_NAME_STRING_CHECKED(UAnimNotifyState_TimedNiagaraEffect, SocketName) && SocketName != FName(TEXT("None")))
		{
			PreviousSocketNames.Add(SocketName);
		}
	}
}
#endif
