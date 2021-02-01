// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimComponent.h"
#include "DrawDebugHelpers.h"
#include "MotionWarpingComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/Character.h"

UContextualAnimComponent::UContextualAnimComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SetGenerateOverlapEvents(true);

	bHiddenInGame = false;
}

bool UContextualAnimComponent::QueryData(const FContextualAnimQueryParams& QueryParams, FContextualAnimQueryResult& Result) const
{
	return ContextualAnimAsset ? ContextualAnimAsset->QueryData(Result, QueryParams, GetComponentTransform()) : false;
}

UAnimInstance* UContextualAnimComponent::GetAnimInstanceForActor(AActor* Actor) const
{
	//@TODO: Replace with an Interface in the future
	ACharacter* Character = Actor ? Cast<ACharacter>(Actor) : nullptr;
	USkeletalMeshComponent* SkelMeshComp = Character ? Character->GetMesh() : nullptr;
	return SkelMeshComp ? SkelMeshComp->GetAnimInstance() : nullptr;
}

bool UContextualAnimComponent::IsActorPlayingContextualAnimation(AActor* Actor) const
{
	UAnimInstance* AnimInstance = GetAnimInstanceForActor(Actor);
	UAnimMontage* CurrentMontage = AnimInstance ? AnimInstance->GetCurrentActiveMontage() : nullptr;
	AActor*const* ActorPtr = CurrentMontage ? MontageToActorMap.Find(CurrentMontage) : nullptr;
	return (ActorPtr && *ActorPtr == Actor);
}

bool UContextualAnimComponent::TryStartContextualAnimation(AActor* Actor, const FContextualAnimQueryResult& Data)
{
	if(!Data.IsValid())
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("TryStartContextualAnimation. QueryResult is not valid. Owner: %s Actor: %s"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Actor));

		return false;
	}

	// Early out if the actor doesn't have a valid anim instance
	UAnimInstance* AnimInstance = GetAnimInstanceForActor(Actor);
	if (AnimInstance == nullptr)
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("TryStartContextualAnimation. Can't find AnimInstance for the supplied actor. Owner: %s Actor: %s"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Actor));

		return false;
	}

	// Early out if the actor doesn't have a motion warping component
	UMotionWarpingComponent* MotionWarpingComp = Actor->FindComponentByClass<UMotionWarpingComponent>();
	if (MotionWarpingComp == nullptr)
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("TryStartContextualAnimation. Can't find MotionWarpingComp for the supplied actor. Owner: %s Actor: %s"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Actor));

		return false;
	}

	// Early out if the actor is playing the animation already
	UAnimMontage* CurrentMontage = AnimInstance->GetCurrentActiveMontage();
	AActor** ActorPtr = CurrentMontage ? MontageToActorMap.Find(CurrentMontage) : nullptr;
	if (ActorPtr && *ActorPtr == Actor)
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("TryStartContextualAnimation. The supplied actor is playing the animation already. Owner: %s Actor: %s Anim: %s"), 
		*GetNameSafe(GetOwner()), *GetNameSafe(Actor), *GetNameSafe(CurrentMontage));

		return false;
	}

	// Early out if the animation is not valid
	UAnimMontage* Montage = Data.Animation.Get();
	if (Montage == nullptr)
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("TryStartContextualAnimation. The animation has not been loaded yet. Owner: %s Actor: %s Anim: %s"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Actor), *Data.Animation.GetAssetName());

		return false;
	}

	// Set sync point for motion warping
	const FName SyncPointName = ContextualAnimAsset->MotionWarpSyncPointName;
	MotionWarpingComp->AddOrUpdateSyncPoint(SyncPointName, Data.SyncTransform);

	// Play animation
	AnimInstance->Montage_Play(Montage, 1.f, EMontagePlayReturnType::MontageLength, Data.AnimStartTime);

	// Listen to when the montage ends for clean up purposes
	AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &UContextualAnimComponent::OnMontageBlendingOut);

	// Ignore collision between actors
	SetIgnoreOwnerComponentsWhenMovingForActor(Actor, true);

	// Keep track of the actor
	MontageToActorMap.Add(Montage, Actor);

	UE_LOG(LogContextualAnim, Log, TEXT("TryStartContextualAnimation. Starting contextual anim. Owner: %s Actor: %s Anim: %s StartTime: %f SyncPointName: %s"),
		*GetNameSafe(GetOwner()), *GetNameSafe(Actor), *GetNameSafe(Montage), Data.AnimStartTime, *SyncPointName.ToString());

	return true;
}

bool UContextualAnimComponent::TryEndContextualAnimation(AActor* Actor)
{
	// Early out if the actor doesn't have a valid anim instance
	UAnimInstance* AnimInstance = GetAnimInstanceForActor(Actor);
	if(AnimInstance == nullptr)
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("TryEndContextualAnimation. Can't find AnimInstance for the supplied actor. Owner: %s Actor: %s"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Actor));

		return false;
	}

	// Early out if the actor is not playing a contextual animation
	UAnimMontage* CurrentMontage = AnimInstance->GetCurrentActiveMontage();
	AActor** ActorPtr = CurrentMontage ? MontageToActorMap.Find(CurrentMontage) : nullptr;
	if(ActorPtr == nullptr || *ActorPtr != Actor)
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("TryEndContextualAnimation. The supplied actor is not playing a contextual anim. Owner: %s Actor: %s CurrentMontage: %s"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Actor), *GetNameSafe(CurrentMontage));

		return false;
	}

	// Check if we have an exit section and transition to it, otherwise just stop the montage
	static const FName ExitSectionName = FName(TEXT("Exit"));
	const int32 SectionIdx = CurrentMontage->GetSectionIndex(ExitSectionName);
	if(SectionIdx != INDEX_NONE)
	{
		// Unbind blend out delegate for a moment so we don't get it during the transition
		AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &UContextualAnimComponent::OnMontageBlendingOut);

		AnimInstance->Montage_Play(CurrentMontage, 1.f);
		AnimInstance->Montage_JumpToSection(ExitSectionName, CurrentMontage);

		AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &UContextualAnimComponent::OnMontageBlendingOut);

		UE_LOG(LogContextualAnim, Log, TEXT("TryEndContextualAnimation. Playing 'Exit' transition. Owner: %s Actor: %s Anim: %s"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Actor), *GetNameSafe(CurrentMontage));
	}
	else
	{
		UE_LOG(LogContextualAnim, Log, TEXT("TryEndContextualAnimation. Forcing montage to stop. Owner: %s Actor: %s Anim: %s"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Actor), *GetNameSafe(CurrentMontage));

		AnimInstance->Montage_Stop(CurrentMontage->BlendOut.GetBlendTime(), CurrentMontage);
	}
	
	return true;
}

void UContextualAnimComponent::OnMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	// Remove actor from the map
	AActor* Actor = nullptr;
	if(MontageToActorMap.RemoveAndCopyValue(Montage, Actor))
	{
		// Unbind events
		if (UAnimInstance* AnimInstance = GetAnimInstanceForActor(Actor))
		{
			AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &UContextualAnimComponent::OnMontageBlendingOut);
		}

		// Restore collision between actors
		SetIgnoreOwnerComponentsWhenMovingForActor(Actor, false);

		UE_LOG(LogContextualAnim, Log, TEXT("OnMontageBlendingOut. Clean up completed. Actor: %s Montage: %s bInterrupted: %d"), *GetNameSafe(Actor), *GetNameSafe(Montage), bInterrupted);
	}
	else
	{
		UE_LOG(LogContextualAnim, Log, TEXT("OnMontageBlendingOut. Can't find actor playing this montage. Montage: %s bInterrupted: %d"), *GetNameSafe(Montage), bInterrupted);
	}
}

void UContextualAnimComponent::SetIgnoreOwnerComponentsWhenMovingForActor(AActor* Actor, bool bShouldIgnore)
{
	UPrimitiveComponent* ActorRootPrimitiveComp = Actor ? Cast<UPrimitiveComponent>(Actor->GetRootComponent()) : nullptr;
	if (ActorRootPrimitiveComp)
	{
		const TSet<UActorComponent*> Comps = GetOwner()->GetComponents();
		for (UActorComponent* Comp : Comps)
		{
			UPrimitiveComponent* OwnerPrimitiveComp = (Comp != this) ? Cast<UPrimitiveComponent>(Comp) : nullptr;
			if (OwnerPrimitiveComp)
			{
				ActorRootPrimitiveComp->IgnoreComponentWhenMoving(OwnerPrimitiveComp, bShouldIgnore);
			}
		}
	}
}

FBoxSphereBounds UContextualAnimComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox BB(FVector(0.f, 0.f, -10.f), FVector(0.f, 0.f, 10.f));
	if (ContextualAnimAsset)
	{
		for (const FContextualAnimData& Data : ContextualAnimAsset->DataContainer)
		{
			BB += Data.GetAlignmentTransformAtEntryTime().GetLocation();
		}
	}
	return FBoxSphereBounds(BB.TransformBy(GetComponentTransform()));
}

FPrimitiveSceneProxy* UContextualAnimComponent::CreateSceneProxy()
{
	class FContextualAnimSceneProxy final : public FPrimitiveSceneProxy
	{
	public:

		SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		FContextualAnimSceneProxy(const UContextualAnimComponent* InComponent)
			: FPrimitiveSceneProxy(InComponent)
			, ContextualAnimComp(InComponent)
		{
		}

		static void DrawSector(FPrimitiveDrawInterface* PDI, const FVector& Origin, const FVector& Direction, float MinDistance, float MaxDistance, float MinAngle, float MaxAngle, const FLinearColor& Color, uint8 DepthPriority, float Thickness)
		{
			// Draw Cone lines
			const FVector LeftDirection = Direction.RotateAngleAxis(MinAngle, FVector::UpVector);
			const FVector RightDirection = Direction.RotateAngleAxis(MaxAngle, FVector::UpVector);
			PDI->DrawLine(Origin + (LeftDirection * MinDistance), Origin + (LeftDirection * MaxDistance), Color, DepthPriority, Thickness);
			PDI->DrawLine(Origin + (RightDirection * MinDistance), Origin + (RightDirection * MaxDistance), Color, DepthPriority, Thickness);

			// Draw Near Arc
			FVector LastDirection = LeftDirection;
			for (float Step = MinAngle; Step <= MaxAngle; Step += 10)
			{
				const float Length = MinDistance;
				const float Angle = FMath::Clamp(Step, MinAngle, MaxAngle);
				const FVector NewDirection = Direction.RotateAngleAxis(Angle, FVector::UpVector);
				const FVector LineStart = Origin + (LastDirection * Length);
				const FVector LineEnd = Origin + (NewDirection * Length);
				PDI->DrawLine(LineStart, LineEnd, Color, DepthPriority, Thickness);
				LastDirection = NewDirection;
			}

			// Draw Far Arc
			LastDirection = LeftDirection;
			for (float Step = MinAngle; Step <= MaxAngle; Step += 10)
			{
				const float Length = MaxDistance;
				const float Angle = FMath::Clamp(Step, MinAngle, MaxAngle);
				const FVector NewDirection = Direction.RotateAngleAxis(Angle, FVector::UpVector);
				const FVector LineStart = Origin + (LastDirection * Length);
				const FVector LineEnd = Origin + (NewDirection * Length);
				PDI->DrawLine(LineStart, LineEnd, Color, DepthPriority, Thickness);
				LastDirection = NewDirection;
			}
		}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			const UContextualAnimAsset* Asset = ContextualAnimComp.IsValid() ? ContextualAnimComp->ContextualAnimAsset : nullptr;
			if (Asset == nullptr)
			{
				return;
			}

			const FContextualAnimDebugParams Params = ContextualAnimComp->DebugParams;

			const FMatrix& LocalToWorld = GetLocalToWorld();
			const FTransform ToWorldTransform = FTransform(LocalToWorld);

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				if (VisibilityMap & (1 << ViewIndex))
				{
					const FSceneView* View = Views[ViewIndex];

					// Taking into account the min and maximum drawing distance
					const float DistanceSqr = (View->ViewMatrices.GetViewOrigin() - LocalToWorld.GetOrigin()).SizeSquared();
					if (DistanceSqr < FMath::Square(GetMinDrawDistance()) || DistanceSqr > FMath::Square(GetMaxDrawDistance()))
					{
						continue;
					}

					FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

					// Draw collision sphere
					const FLinearColor DrawSphereColor = GetViewSelectionColor(FColor::Red, *View, IsSelected(), IsHovered(), false, IsIndividuallySelected());
					const float SphereRadius = ContextualAnimComp->GetScaledSphereRadius();

					float AbsScaleX = LocalToWorld.GetScaledAxis(EAxis::X).Size();
					float AbsScaleY = LocalToWorld.GetScaledAxis(EAxis::Y).Size();
					float AbsScaleZ = LocalToWorld.GetScaledAxis(EAxis::Z).Size();
					float MinAbsScale = FMath::Min3(AbsScaleX, AbsScaleY, AbsScaleZ);

					FVector ScaledX = LocalToWorld.GetUnitAxis(EAxis::X) * MinAbsScale;
					FVector ScaledY = LocalToWorld.GetUnitAxis(EAxis::Y) * MinAbsScale;
					FVector ScaledZ = LocalToWorld.GetUnitAxis(EAxis::Z) * MinAbsScale;

					const int32 SphereSides = FMath::Clamp<int32>(SphereRadius / 4.f, 16, 64);
					DrawCircle(PDI, LocalToWorld.GetOrigin(), ScaledX, ScaledY, DrawSphereColor, SphereRadius, SphereSides, SDPG_World);
					DrawCircle(PDI, LocalToWorld.GetOrigin(), ScaledX, ScaledZ, DrawSphereColor, SphereRadius, SphereSides, SDPG_World);
					DrawCircle(PDI, LocalToWorld.GetOrigin(), ScaledY, ScaledZ, DrawSphereColor, SphereRadius, SphereSides, SDPG_World);

					FContextualAnimQueryResult Result;
					if (Params.TestActor.IsValid())
					{
						if (ContextualAnimComp->QueryData(FContextualAnimQueryParams(Params.TestActor.Get(), true, true), Result))
						{							
							const float Time = Result.AnimStartTime;
							const FTransform TransformAtTime = Asset->DataContainer[Result.DataIndex].GetAlignmentTransformAtTime(Time) * ToWorldTransform;
							DrawCoordinateSystem(PDI, TransformAtTime.GetLocation(), TransformAtTime.Rotator(), 20.f, SDPG_World, 2.f);
						}
					}

					for (int32 Idx = 0; Idx < Asset->DataContainer.Num(); Idx++)
					{
						const FContextualAnimData& Data = Asset->DataContainer[Idx];

						FLinearColor DrawColor = Result.DataIndex == Idx ? FLinearColor::Red : FLinearColor::White;

						// Draw Entry Point
						const FTransform EntryTransform = (Data.GetAlignmentTransformAtEntryTime() * ToWorldTransform);
						DrawCoordinateSystem(PDI, EntryTransform.GetLocation(), EntryTransform.Rotator(), 20.f, SDPG_World, 3.f);

						// Draw Facing Tolerance
						if(Data.Facing.Tolerance > 0.f)
						{
							DrawSector(PDI, EntryTransform.GetLocation(), EntryTransform.GetRotation().GetForwardVector(), 0.f, 30.f, -Data.Facing.Tolerance, Data.Facing.Tolerance, DrawColor, SDPG_World, 1.f);
						}
						else
						{
							DrawCircle(PDI, EntryTransform.GetLocation(), FVector(1, 0, 0), FVector(0, 1, 0), DrawColor, 30.f, 12, SDPG_World, 1.f);
						}

						// Draw Sync Point
						const FTransform SyncPoint = Data.GetAlignmentTransformAtSyncTime() * ToWorldTransform;
						DrawCoordinateSystem(PDI, SyncPoint.GetLocation(), SyncPoint.Rotator(), 20.f, SDPG_World, 3.f);

						if(Params.DrawAlignmentTransformAtTime != Data.EntryTime)
						{
							const FTransform RootAtTime = (Data.GetAlignmentTransformAtTime(Params.DrawAlignmentTransformAtTime) * ToWorldTransform);
							DrawCoordinateSystem(PDI, RootAtTime.GetLocation(), RootAtTime.Rotator(), 10.f, SDPG_World, 2.f);
						}

						// Draw Sector
						FVector Origin = ToWorldTransform.GetLocation();
						FVector Direction = (EntryTransform.GetLocation() - ToWorldTransform.GetLocation()).GetSafeNormal2D();

						if(Data.OffsetFromOrigin != 0.f)
						{
							Origin = Origin + Direction * Data.OffsetFromOrigin;
						}

						if(Data.Angle.Tolerance > 0.f)
						{
							DrawSector(PDI, Origin, Direction, Data.Distance.MinDistance, Data.Distance.MaxDistance, -Data.Angle.Tolerance, Data.Angle.Tolerance, DrawColor, SDPG_World, 3.f);
						}
						else
						{
							if(Data.Distance.MinDistance > 0.f)
							{
								DrawCircle(PDI, Origin, FVector(1, 0, 0), FVector(0, 1, 0), DrawColor, Data.Distance.MinDistance, 12, SDPG_World, 2.f);
							}
							
							if (Data.Distance.MaxDistance > 0.f)
							{
								DrawCircle(PDI, Origin, FVector(1, 0, 0), FVector(0, 1, 0), DrawColor, Data.Distance.MaxDistance, 12, SDPG_World, 2.f);
							}
						}
					}
				}
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			const bool bShowForCollision = View->Family->EngineShowFlags.Collision;
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = IsShown(View);
			Result.bDynamicRelevance = true;
			Result.bNormalTranslucency = Result.bSeparateTranslucency = IsShown(View);
			return Result;
		}

		virtual uint32 GetMemoryFootprint(void) const override
		{
			return(sizeof(*this) + GetAllocatedSize());
		}

		uint32 GetAllocatedSize(void) const
		{
			return(FPrimitiveSceneProxy::GetAllocatedSize());
		}

	private:
		TWeakObjectPtr<const UContextualAnimComponent> ContextualAnimComp;
	};

	if(bEnableDebug)
	{
		return new FContextualAnimSceneProxy(this);
	}

	return nullptr;
}