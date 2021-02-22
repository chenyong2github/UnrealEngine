// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimSceneActorComponent.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/Character.h"
#include "ContextualAnimMetadata.h"

UContextualAnimSceneActorComponent::UContextualAnimSceneActorComponent(const FObjectInitializer& ObjectInitializer)
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

bool UContextualAnimSceneActorComponent::QueryData(const FContextualAnimQueryParams& QueryParams, FContextualAnimQueryResult& Result) const
{
	return SceneAsset ? SceneAsset->QueryData(Result, QueryParams, GetComponentTransform()) : false;
}

FBoxSphereBounds UContextualAnimSceneActorComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox BB(FVector(0.f, 0.f, -10.f), FVector(0.f, 0.f, 10.f));
	if (SceneAsset)
	{
		for (const FContextualAnimData& Data : SceneAsset->InteractorTrack.AnimDataContainer)
		{
			BB += Data.GetAlignmentTransformAtEntryTime().GetLocation();
		}
	}
	return FBoxSphereBounds(BB.TransformBy(GetComponentTransform()));
}

FPrimitiveSceneProxy* UContextualAnimSceneActorComponent::CreateSceneProxy()
{
	class FSceneActorCompProxy final : public FPrimitiveSceneProxy
	{
	public:

		SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		FSceneActorCompProxy(const UContextualAnimSceneActorComponent* InComponent)
			: FPrimitiveSceneProxy(InComponent)
			, SceneAssetPtr(InComponent->SceneAsset)
			, Params(InComponent->DebugParams)
			, Radius(InComponent->GetScaledSphereRadius())
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
			float Angle = MinAngle;
			while (Angle < MaxAngle)
			{
				Angle = FMath::Clamp<float>(Angle + 10, MinAngle, MaxAngle);

				const float Length = MinDistance;
				const FVector NewDirection = Direction.RotateAngleAxis(Angle, FVector::UpVector);
				const FVector LineStart = Origin + (LastDirection * Length);
				const FVector LineEnd = Origin + (NewDirection * Length);
				PDI->DrawLine(LineStart, LineEnd, Color, DepthPriority, Thickness);
				LastDirection = NewDirection;
			}

			// Draw Far Arc
			LastDirection = LeftDirection;
			Angle = MinAngle;
			while (Angle < MaxAngle)
			{
				Angle = FMath::Clamp<float>(Angle + 10, MinAngle, MaxAngle);

				const float Length = MaxDistance;
				const FVector NewDirection = Direction.RotateAngleAxis(Angle, FVector::UpVector);
				const FVector LineStart = Origin + (LastDirection * Length);
				const FVector LineEnd = Origin + (NewDirection * Length);
				PDI->DrawLine(LineStart, LineEnd, Color, DepthPriority, Thickness);
				LastDirection = NewDirection;
			}
		}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			const UContextualAnimCompositeSceneAsset* Asset = SceneAssetPtr.Get();
			if (Asset == nullptr)
			{
				return;
			}

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

					float AbsScaleX = LocalToWorld.GetScaledAxis(EAxis::X).Size();
					float AbsScaleY = LocalToWorld.GetScaledAxis(EAxis::Y).Size();
					float AbsScaleZ = LocalToWorld.GetScaledAxis(EAxis::Z).Size();
					float MinAbsScale = FMath::Min3(AbsScaleX, AbsScaleY, AbsScaleZ);

					FVector ScaledX = LocalToWorld.GetUnitAxis(EAxis::X) * MinAbsScale;
					FVector ScaledY = LocalToWorld.GetUnitAxis(EAxis::Y) * MinAbsScale;
					FVector ScaledZ = LocalToWorld.GetUnitAxis(EAxis::Z) * MinAbsScale;

					const int32 SphereSides = FMath::Clamp<int32>(Radius / 4.f, 16, 64);
					DrawCircle(PDI, LocalToWorld.GetOrigin(), ScaledX, ScaledY, DrawSphereColor, Radius, SphereSides, SDPG_World);
					DrawCircle(PDI, LocalToWorld.GetOrigin(), ScaledX, ScaledZ, DrawSphereColor, Radius, SphereSides, SDPG_World);
					DrawCircle(PDI, LocalToWorld.GetOrigin(), ScaledY, ScaledZ, DrawSphereColor, Radius, SphereSides, SDPG_World);

					FContextualAnimQueryResult Result;
					if (Params.TestActor.IsValid())
					{
						if (Asset->QueryData(Result, FContextualAnimQueryParams(Params.TestActor.Get(), true, true), ToWorldTransform))
						{							
							const float Time = Result.AnimStartTime;
							const FTransform TransformAtTime = Asset->InteractorTrack.AnimDataContainer[Result.DataIndex].GetAlignmentTransformAtTime(Time) * ToWorldTransform;
							DrawCoordinateSystem(PDI, TransformAtTime.GetLocation(), TransformAtTime.Rotator(), 20.f, SDPG_World, 2.f);
						}
					}

					for (int32 Idx = 0; Idx < Asset->InteractorTrack.AnimDataContainer.Num(); Idx++)
					{
						const FContextualAnimData& Data = Asset->InteractorTrack.AnimDataContainer[Idx];

						FLinearColor DrawColor = Result.DataIndex == Idx ? FLinearColor::Red : FLinearColor::White;

						// Draw Entry Point
						const FTransform EntryTransform = (Data.GetAlignmentTransformAtEntryTime() * ToWorldTransform);
						DrawCoordinateSystem(PDI, EntryTransform.GetLocation(), EntryTransform.Rotator(), 20.f, SDPG_World, 3.f);

						// Draw Facing Tolerance
						if(Data.Metadata && Data.Metadata->Facing.Tolerance > 0.f)
						{
							DrawSector(PDI, EntryTransform.GetLocation(), EntryTransform.GetRotation().GetForwardVector(), 0.f, 30.f, -Data.Metadata->Facing.Tolerance, Data.Metadata->Facing.Tolerance, DrawColor, SDPG_World, 1.f);
						}
						else
						{
							DrawCircle(PDI, EntryTransform.GetLocation(), FVector(1, 0, 0), FVector(0, 1, 0), DrawColor, 30.f, 12, SDPG_World, 1.f);
						}

						// Draw Sync Point
						const FTransform SyncPoint = Data.GetAlignmentTransformAtSyncTime() * ToWorldTransform;
						DrawCoordinateSystem(PDI, SyncPoint.GetLocation(), SyncPoint.Rotator(), 20.f, SDPG_World, 3.f);

						if(Params.DrawAlignmentTransformAtTime != 0.f)
						{
							const FTransform RootAtTime = (Data.GetAlignmentTransformAtTime(Params.DrawAlignmentTransformAtTime) * ToWorldTransform);
							DrawCoordinateSystem(PDI, RootAtTime.GetLocation(), RootAtTime.Rotator(), 10.f, SDPG_World, 2.f);
						}

						// Draw Sector
						if(Data.Metadata)
						{
							FVector Origin = ToWorldTransform.GetLocation();
							FVector Direction = (EntryTransform.GetLocation() - ToWorldTransform.GetLocation()).GetSafeNormal2D();

							if (Data.Metadata->OffsetFromOrigin != 0.f)
							{
								Origin = Origin + Direction * Data.Metadata->OffsetFromOrigin;
							}

							if (Data.Metadata->Angle.Tolerance > 0.f)
							{
								DrawSector(PDI, Origin, Direction, Data.Metadata->Distance.MinDistance, Data.Metadata->Distance.MaxDistance, -Data.Metadata->Angle.Tolerance, Data.Metadata->Angle.Tolerance, DrawColor, SDPG_World, 3.f);
							}
							else
							{
								if (Data.Metadata->Distance.MinDistance > 0.f)
								{
									DrawCircle(PDI, Origin, FVector(1, 0, 0), FVector(0, 1, 0), DrawColor, Data.Metadata->Distance.MinDistance, 12, SDPG_World, 2.f);
								}

								if (Data.Metadata->Distance.MaxDistance > 0.f)
								{
									DrawCircle(PDI, Origin, FVector(1, 0, 0), FVector(0, 1, 0), DrawColor, Data.Metadata->Distance.MaxDistance, 12, SDPG_World, 2.f);
								}
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
		TWeakObjectPtr<const UContextualAnimCompositeSceneAsset> SceneAssetPtr;
		FContextualAnimDebugParams Params;
		float Radius;
	};

	if(bEnableDebug)
	{
		return new FSceneActorCompProxy(this);
	}

	return nullptr;
}