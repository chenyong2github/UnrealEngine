// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimComponent.h"
#include "ContextualAnimAsset.h"
#include "DrawDebugHelpers.h"

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
	Result.Reset();

	if (!ContextualAnimAsset || !QueryParams.Querier.IsValid())
	{
		return false;
	}

	int32 DataIndex = INDEX_NONE;
	if (QueryParams.bComplexQuery)
	{
		for (int32 Idx = 0; Idx < ContextualAnimAsset->DataContainer.Num(); Idx++)
		{
			const FContextualAnimData& Data = ContextualAnimAsset->DataContainer[Idx];
			const FTransform EntryTransform = Data.GetAlignmentTransformAtEntryTime() * GetComponentTransform();

			FVector Origin = GetComponentTransform().GetLocation();
			FVector Direction = (EntryTransform.GetLocation() - Origin).GetSafeNormal2D();

			if (Data.OffsetFromOrigin != 0.f)
			{
				Origin = Origin + Direction * Data.OffsetFromOrigin;
			}

			// Distance Test
			//--------------------------------------------------
			if (Data.Distance.MaxDistance > 0.f || Data.Distance.MinDistance > 0.f)
			{
				const float DistSq = FVector::DistSquared2D(Origin, QueryParams.Querier->GetActorLocation());

				if (Data.Distance.MaxDistance > 0.f)
				{
					if (DistSq > FMath::Square(Data.Distance.MaxDistance))
					{
						continue;
					}
				}

				if (Data.Distance.MinDistance > 0.f)
				{
					if (DistSq < FMath::Square(Data.Distance.MinDistance))
					{
						continue;
					}
				}
			}

			// Angle Test
			//--------------------------------------------------
			if (Data.Angle.Tolerance > 0.f)
			{
				//@TODO: Cache this
				const float AngleCos = FMath::Cos(FMath::Clamp(FMath::DegreesToRadians(Data.Angle.Tolerance), 0.f, PI));
				const FVector ToLocation = (QueryParams.Querier->GetActorLocation() - Origin).GetSafeNormal2D();
				if (FVector::DotProduct(ToLocation, Direction) < AngleCos)
				{
					continue;
				}
			}

			// Facing Test
			//--------------------------------------------------
			if (Data.Facing.Tolerance > 0.f)
			{
				//@TODO: Cache this
				const float FacingCos = FMath::Cos(FMath::Clamp(FMath::DegreesToRadians(Data.Facing.Tolerance), 0.f, PI));
				if (FVector::DotProduct(QueryParams.Querier->GetActorForwardVector(), EntryTransform.GetRotation().GetForwardVector()) < FacingCos)
				{
					continue;
				}
			}

			// Return the first item that passes all tests
			DataIndex = Idx;
			break;
		}
	}
	else // Simple Query
	{
		float BestDistanceSq = MAX_FLT;
		for (int32 Idx = 0; Idx < ContextualAnimAsset->DataContainer.Num(); Idx++)
		{
			const FContextualAnimData& Data = ContextualAnimAsset->DataContainer[Idx];

			//@TODO: Convert querier location to local space instead
			const FTransform EntryTransform = Data.GetAlignmentTransformAtEntryTime() * GetComponentTransform();
			const float DistSq = FVector::DistSquared2D(EntryTransform.GetLocation(), QueryParams.Querier->GetActorLocation());
			if (DistSq < BestDistanceSq)
			{
				BestDistanceSq = DistSq;
				DataIndex = Idx;
			}
		}
	}

	if(DataIndex != INDEX_NONE)
	{
		const FContextualAnimData& ResultData = ContextualAnimAsset->DataContainer[DataIndex];

		Result.DataIndex = DataIndex;
		Result.Animation = ResultData.Animation;
		Result.EntryTransform = ResultData.GetAlignmentTransformAtEntryTime() * GetComponentTransform();
		Result.SyncTransform = ResultData.GetAlignmentTransformAtSyncTime() * GetComponentTransform();

		if (QueryParams.bFindAnimStartTime)
		{
			const FVector LocalLocation = (QueryParams.Querier->GetActorTransform().GetRelativeTransform(GetComponentTransform())).GetLocation();
			Result.AnimStartTime = ResultData.FindBestAnimStartTime(LocalLocation);
		}

		return true;
	}

	return false;
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