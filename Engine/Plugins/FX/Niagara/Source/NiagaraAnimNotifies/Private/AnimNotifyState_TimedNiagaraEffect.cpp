// Copyright Epic Games, Inc. All Rights Reserved.

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

UFXSystemComponent* UAnimNotifyState_TimedNiagaraEffect::SpawnEffect(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) const
{
	// Only spawn if we've got valid params
	if (ValidateParameters(MeshComp))
	{
		return UNiagaraFunctionLibrary::SpawnSystemAttached(Template, MeshComp, SocketName, LocationOffset, RotationOffset, EAttachLocation::KeepRelativeOffset, !bDestroyAtEnd);
	}
	return nullptr;
}

void UAnimNotifyState_TimedNiagaraEffect::NotifyBegin(USkeletalMeshComponent * MeshComp, class UAnimSequenceBase * Animation, float TotalDuration)
{
	if (UFXSystemComponent* Component = SpawnEffect(MeshComp, Animation))
	{
		// tag the component with the AnimNotify that is triggering the animation so that we can properly clean it up
		Component->ComponentTags.AddUnique(GetFName());
	}

	Super::NotifyBegin(MeshComp, Animation, TotalDuration);
}

void UAnimNotifyState_TimedNiagaraEffect::NotifyEnd(USkeletalMeshComponent * MeshComp, class UAnimSequenceBase * Animation)
{
	TArray<USceneComponent*> Children;
	MeshComp->GetChildrenComponents(false, Children);

	if (Children.Num())
	{
		const FName AnimNotifyTag = GetFName();

		for (USceneComponent* Component : Children)
		{
			if (Component && Component->IsActive() && Component->ComponentHasTag(AnimNotifyTag))
			{
				// we want to special case NiagaraComponent behavior based on it's current execution state.  It could be
				// no longer marked as active because we triggered a Deactivate, but it still hasn't deactivated yet
				if (UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(Component))
				{
					if (NiagaraComponent->GetExecutionState() != ENiagaraExecutionState::Active)
					{
						continue;
					}
				}

				if (UFXSystemComponent* FXComponent = CastChecked<UFXSystemComponent>(Component))
				{
					const bool ComponentActive = FXComponent->IsActive();
						
					// untag the component
					FXComponent->ComponentTags.Remove(AnimNotifyTag);

					// Either destroy the component or deactivate it to have it's active FXSystems finish.
					// The component will auto destroy once all FXSystem are gone.
					if (bDestroyAtEnd)
					{
						FXComponent->DestroyComponent();
					}
					else
					{
						FXComponent->Deactivate();
					}

					// Removed a component, no need to continue
					break;
				}
			}
		}
	}

	Super::NotifyEnd(MeshComp, Animation);
}

bool UAnimNotifyState_TimedNiagaraEffect::ValidateParameters(USkeletalMeshComponent* MeshComp) const
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