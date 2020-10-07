// Copyright Epic Games, Inc. All Rights Reserved.

#include "BuoyancyComponent.h"
#include "WaterBodyActor.h"
#include "DrawDebugHelpers.h"
#include "WaterSplineComponent.h"
#include "Physics/SimpleSuspension.h"

TAutoConsoleVariable<int32> CVarWaterDebugBuoyancy(
	TEXT("r.Water.DebugBuoyancy"),
	0,
	TEXT("Enable debug drawing for water interactions."),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarWaterUseSplineKeyOptimization(
	TEXT("r.Water.UseSplineKeyOptimization"),
	1,
	TEXT("Whether to cache spline input key for water bodies."),
	ECVF_Default);

UBuoyancyComponent::UBuoyancyComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, BuoyancyCoefficient(0.1f)
	, BuoyancyDamp(1000.f)
	, BuoyancyDamp2(1.f)
	, BuoyancyRampMinVelocity(20.f)
	, BuoyancyRampMaxVelocity(50.f)
	, BuoyancyRampMax(1.f)
	, MaxBuoyantForce(5000000.f)
	, WaterShorePushFactor(0.3f)
	, WaterVelocityStrength(0.01f)
	, MaxWaterForce(10000.f)
	, PontoonConfiguration(0)
	, VelocityPontoonIndex(0)
	, bIsOverlappingWaterBody(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UBuoyancyComponent::BeginPlay()
{
	Super::BeginPlay();
	for (FSphericalPontoon& Pontoon : Pontoons)
	{
		if (Pontoon.CenterSocket != NAME_None)
		{
			Pontoon.bUseCenterSocket = true;
		}
	}
	SetupWaterBodyOverlaps();
}

void UBuoyancyComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Owner = GetOwner();
	check(Owner);
	UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
	check(PrimComp);
	const FVector PhysicsVelocity = PrimComp->GetComponentVelocity();
	const FVector ForwardDir = PrimComp->GetForwardVector();
	const float ForwardSpeed = FVector::DotProduct(ForwardDir, PhysicsVelocity);
	const float ForwardSpeedKmh = ForwardSpeed * 0.036f; //cm/s to km/h
	UpdatePontoonCoefficients();
	UpdatePontoons(DeltaTime, ForwardSpeed, ForwardSpeedKmh, PrimComp);
	ApplyBuoyancy(PrimComp);
	const FVector& WaterForce = ComputeWaterForce(DeltaTime, PhysicsVelocity);
	PrimComp->AddForce(WaterForce, NAME_None, /*bAccelChange=*/true);
}

void UBuoyancyComponent::SetupWaterBodyOverlaps()
{
	AActor* Owner = GetOwner();
	check(Owner);
	UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
	check(PrimComp);
	PrimComp->SetCollisionObjectType(ECollisionChannel::ECC_PhysicsBody);
	if (PrimComp->GetCollisionResponseToChannel(ECollisionChannel::ECC_WorldStatic) == ECollisionResponse::ECR_Ignore)
	{
		PrimComp->SetCollisionResponseToChannel(ECollisionChannel::ECC_WorldStatic, ECollisionResponse::ECR_Overlap);
	}
	PrimComp->SetGenerateOverlapEvents(true);
}

void UBuoyancyComponent::AddCustomPontoon(float Radius, FName CenterSocketName)
{
	FSphericalPontoon Pontoon;
	Pontoon.Radius = Radius;
	Pontoon.CenterSocket = CenterSocketName;
	Pontoons.Add(Pontoon);
}

void UBuoyancyComponent::AddCustomPontoon(float Radius, const FVector& RelativeLocation)
{
	FSphericalPontoon Pontoon;
	Pontoon.Radius = Radius;
	Pontoon.RelativeLocation = RelativeLocation;
	Pontoons.Add(Pontoon);
}

void UBuoyancyComponent::EnteredWaterBody(AWaterBody* WaterBody)
{
	bool bIsFirstBody = !CurrentWaterBodies.Num() && WaterBody;
	CurrentWaterBodies.AddUnique(WaterBody);
	for (FSphericalPontoon& Pontoon : Pontoons)
	{
		Pontoon.SplineSegments.FindOrAdd(WaterBody, -1);
	}
	if (bIsFirstBody)
	{
		bIsOverlappingWaterBody = true;
	}
}

void UBuoyancyComponent::ExitedWaterBody(AWaterBody* WaterBody)
{
	CurrentWaterBodies.Remove(WaterBody);
	for (FSphericalPontoon& Pontoon : Pontoons)
	{
		Pontoon.SplineSegments.Remove(WaterBody);
	}
	if (!CurrentWaterBodies.Num())
	{
		bIsOverlappingWaterBody = false;
	}
}

void UBuoyancyComponent::ApplyBuoyancy(UPrimitiveComponent* PrimitiveComponent)
{
	check(GetOwner());

	if (PrimitiveComponent && bIsOverlappingWaterBody)
	{
		int PontoonIndex = 0;
		for (const FSphericalPontoon& Pontoon : Pontoons)
		{
			if (PontoonConfiguration & (1 << PontoonIndex))
			{
				PrimitiveComponent->AddForceAtLocation(Pontoon.LocalForce, Pontoon.CenterLocation);
			}
			PontoonIndex++;
		}
	}
}

void UBuoyancyComponent::ComputeBuoyancy(FSphericalPontoon& Pontoon, float ForwardSpeedKmh)
{
	AActor* Owner = GetOwner();
	check(Owner);

	auto ComputeBuoyantForce = [&](FVector CenterLocation, float Radius, float InBuoyancyCoefficient, float CurrentWaterLevel) -> float
	{
		const float Bottom = CenterLocation.Z - Radius;
		const float SubDiff = FMath::Clamp(CurrentWaterLevel - Bottom, 0.f, 2.f * Radius);

		// The following was obtained by integrating the volume of a sphere
		// over a linear section of SubmersionDiff length.
		static const float Pi = (float)PI;
		const float SubDiffSq = SubDiff * SubDiff;
		const float SubVolume = (Pi / 3.f) * SubDiffSq * ((3.f * Radius) - SubDiff);

#if ENABLE_DRAW_DEBUG
		if (CVarWaterDebugBuoyancy.GetValueOnAnyThread())
		{
			const FVector WaterPoint = FVector(CenterLocation.X, CenterLocation.Y, CurrentWaterLevel);
			DrawDebugLine(GetWorld(), WaterPoint - 50.f * FVector::ForwardVector, WaterPoint + 50.f * FVector::ForwardVector, FColor::Blue, false, -1.f, 0, 3.f);
			DrawDebugLine(GetWorld(), WaterPoint - 50.f * FVector::RightVector, WaterPoint + 50.f * FVector::RightVector, FColor::Blue, false, -1.f, 0, 3.f);
		}
#endif

		const UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
		check(PrimComp && PrimComp->GetBodyInstance());
		const float VelocityZ = PrimComp->GetBodyInstance()->GetUnrealWorldVelocity().Z;
		const float FirstOrderDrag = BuoyancyDamp * VelocityZ;
		const float SecondOrderDrag = FMath::Sign(VelocityZ) * BuoyancyDamp2 * VelocityZ * VelocityZ;
		const float DampingFactor = -FMath::Max(FirstOrderDrag + SecondOrderDrag, 0.f);
		// The buoyant force scales with submersed volume
		return SubVolume * (InBuoyancyCoefficient) + DampingFactor;
	};

	const float MinVelocity = BuoyancyRampMinVelocity;
	const float MaxVelocity = BuoyancyRampMaxVelocity;
	const float RampFactor = FMath::Clamp((ForwardSpeedKmh - MinVelocity) / (MaxVelocity - MinVelocity), 0.f, 1.f);
	const float BuoyancyRamp = RampFactor * (BuoyancyRampMax - 1);
	float BuoyancyCoefficientWithRamp = BuoyancyCoefficient * (1 + BuoyancyRamp);

	const float BuoyantForce = FMath::Clamp(ComputeBuoyantForce(Pontoon.CenterLocation, Pontoon.Radius, BuoyancyCoefficientWithRamp, Pontoon.WaterHeight), 0.f, MaxBuoyantForce);
	Pontoon.LocalForce = FVector::UpVector * BuoyantForce * Pontoon.PontoonCoefficient;
}

void UBuoyancyComponent::ComputePontoonCoefficients()
{
	TArray<float>& PontoonCoefficients = ConfiguredPontoonCoefficients.FindOrAdd(PontoonConfiguration);
	if (PontoonCoefficients.Num() == 0)
	{
		TArray<FVector> LocalPontoonLocations;
		UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
		if (!PrimComp)
		{
			return;
		}
		for (int32 PontoonIndex = 0; PontoonIndex < Pontoons.Num(); ++PontoonIndex)
		{
			if (PontoonConfiguration & (1 << PontoonIndex))
			{
				const FVector LocalPosition = PrimComp->GetSocketTransform(Pontoons[PontoonIndex].CenterSocket, ERelativeTransformSpace::RTS_ParentBoneSpace).GetLocation();
				LocalPontoonLocations.Add(LocalPosition);
			}
		}
		PontoonCoefficients.AddZeroed(LocalPontoonLocations.Num());
		if (FBodyInstance* BodyInstance = PrimComp->GetBodyInstance())
		{
			const FVector& LocalCOM = BodyInstance->GetMassSpaceLocal().GetLocation();
			//Distribute a mass of 1 to each pontoon so that we get a scaling factor based on position relative to CoM
			FSimpleSuspensionHelpers::ComputeSprungMasses(LocalPontoonLocations, LocalCOM, 1.f, PontoonCoefficients);
		}
	}

	// Apply the coefficients
	for (int32 PontoonIndex = 0, CoefficientIdx = 0; PontoonIndex < Pontoons.Num(); ++PontoonIndex)
	{
		if (PontoonConfiguration & (1 << PontoonIndex))
		{
			Pontoons[PontoonIndex].PontoonCoefficient = PontoonCoefficients[CoefficientIdx++];
		}
	}
}

int32 UBuoyancyComponent::UpdatePontoons(float DeltaTime, float ForwardSpeed, float ForwardSpeedKmh, UPrimitiveComponent* PrimitiveComponent)
{
	AActor* Owner = GetOwner();
	check(Owner);

	int32 NumPontoonsInWater = 0;
	if (bIsOverlappingWaterBody)
	{
		int PontoonIndex = 0;
		for (FSphericalPontoon& Pontoon : Pontoons)
		{
			if (PontoonConfiguration & (1 << PontoonIndex))
			{
				if (Pontoon.bUseCenterSocket)
				{
					const FTransform& PrimCompTransform = PrimitiveComponent->GetSocketTransform(Pontoon.CenterSocket);
					Pontoon.CenterLocation = PrimCompTransform.GetLocation() + Pontoon.Offset;
					Pontoon.SocketRotation = PrimCompTransform.GetRotation();
				}
				else
				{
					Pontoon.CenterLocation = PrimitiveComponent->GetComponentLocation() + Pontoon.RelativeLocation;
				}
				GetWaterSplineKey(Pontoon.CenterLocation, Pontoon.SplineInputKeys, Pontoon.SplineSegments);
				const FVector PontoonBottom = Pontoon.CenterLocation - FVector(0, 0, Pontoon.Radius);
				/*Pass in large negative default value so we don't accidentally assume we're in water when we're not.*/
				Pontoon.WaterHeight = GetWaterHeight(PontoonBottom - FVector::UpVector * 100.f, Pontoon.SplineInputKeys, -100000.f, Pontoon.CurrentWaterBody, Pontoon.WaterDepth, Pontoon.WaterPlaneLocation, Pontoon.WaterPlaneNormal);

				const bool bPrevIsInWater = Pontoon.bIsInWater;
				const float ImmersionDepth = Pontoon.WaterHeight - PontoonBottom.Z;
				/*check if the pontoon is currently in water*/
				if (ImmersionDepth >= 0.f)
				{
					Pontoon.bIsInWater = true;
					Pontoon.ImmersionDepth = ImmersionDepth;
					NumPontoonsInWater++;
				}
				else
				{
					Pontoon.bIsInWater = false;
					Pontoon.ImmersionDepth = 0.f;
				}

#if ENABLE_DRAW_DEBUG
				if (CVarWaterDebugBuoyancy.GetValueOnAnyThread())
				{
					DrawDebugSphere(GetWorld(), Pontoon.CenterLocation, Pontoon.Radius, 16, FColor::Red, false, -1.f, 0, 1.f);
				}
#endif
				ComputeBuoyancy(Pontoon, ForwardSpeedKmh);

				if (Pontoon.bIsInWater && !bPrevIsInWater)
				{
					Pontoon.SplineSegments.Reset();
					// BlueprintImplementables don't really work on the actor component level unfortunately, so call back in to the function defined on the actor itself.
					OnPontoonEnteredWater(Pontoon);
				}
				if (!Pontoon.bIsInWater && bPrevIsInWater)
				{
					Pontoon.SplineSegments.Reset();
					OnPontoonExitedWater(Pontoon);
				}
			}
			PontoonIndex++;
		}

#if ENABLE_DRAW_DEBUG
		if (CVarWaterDebugBuoyancy.GetValueOnAnyThread())
		{
			TMap<const AWaterBody*, float> DebugSplineKeyMap;
			TMap<const AWaterBody*, float> DebugSplineSegmentsMap;
			for (int i = 0; i < 30; ++i)
			{
				for (int j = 0; j < 30; ++j)
				{
					FVector Location = PrimitiveComponent->GetComponentLocation() + (FVector::RightVector * (i - 15) * 30) + (FVector::ForwardVector * (j - 15) * 30);
					GetWaterSplineKey(Location, DebugSplineKeyMap, DebugSplineSegmentsMap);
					FVector Point(Location.X, Location.Y, GetWaterHeight(Location - FVector::UpVector * 200.f, DebugSplineKeyMap, GetOwner()->GetActorLocation().Z));
					DrawDebugPoint(GetWorld(), Point, 5.f, IsOverlappingWaterBody() ? FColor::Green : FColor::Red, false, -1.f, 0);
				}
			}
		}
#endif
	}
	return NumPontoonsInWater;
}

float GetWaterSplineKeyFast(FVector Location, const AWaterBody* WaterBody, TMap<const AWaterBody*, float>& OutSegmentMap)/*const*/
{
	if (!OutSegmentMap.Contains(WaterBody))
	{
		OutSegmentMap.Add(WaterBody, -1);
	}

	const UWaterSplineComponent* WaterSpline = WaterBody->GetWaterSpline();
	const FVector LocalLocation = WaterSpline->GetComponentTransform().InverseTransformPosition(Location);
	const FInterpCurveVector& InterpCurve = WaterSpline->GetSplinePointsPosition();
	float& Segment = OutSegmentMap[WaterBody];

	if (Segment == -1)
	{
		float DummyDistance;
		return InterpCurve.InaccurateFindNearest(LocalLocation, DummyDistance, Segment);
	}

	//We have the cached segment, so search for the best point as in FInterpCurve<T>::InaccurateFindNearest
	//but only in the current segment and the two immediate neighbors

	//River splines aren't looped, so we don't have to handle that case
	const int32 NumPoints = InterpCurve.Points.Num();
	const int32 LastSegmentIdx = FMath::Max(0, NumPoints - 2);
	if (NumPoints > 1)
	{
		float BestDistanceSq = BIG_NUMBER;
		float BestResult = BIG_NUMBER;
		float BestSegment = Segment;
		for (int32 i = Segment - 1; i <= Segment + 1; ++i)
		{
			const int32 SegmentIdx = FMath::Clamp(i, 0, LastSegmentIdx);
			float LocalDistanceSq;
			float LocalResult = InterpCurve.InaccurateFindNearestOnSegment(LocalLocation, SegmentIdx, LocalDistanceSq);
			if (LocalDistanceSq < BestDistanceSq)
			{
				BestDistanceSq = LocalDistanceSq;
				BestResult = LocalResult;
				BestSegment = SegmentIdx;
			}
		}

		if (FMath::IsNearlyEqual(BestResult, Segment - 1) || FMath::IsNearlyEqual(BestResult, Segment + 1))
		{
			//We're at either end of the search - it's possible we skipped a segment so just do a full lookup in this case
			float DummyDistance;
			return InterpCurve.InaccurateFindNearest(LocalLocation, DummyDistance, Segment);
		}

		Segment = BestSegment;
		return BestResult;
	}

	if (NumPoints == 1)
	{
		Segment = 0;
		return InterpCurve.Points[0].InVal;
	}

	return 0.0f;
}

void UBuoyancyComponent::GetWaterSplineKey(FVector Location, TMap<const AWaterBody*, float>& OutMap, TMap<const AWaterBody*, float>& OutSegmentMap) const
{
	OutMap.Reset();
	for (const AWaterBody* WaterBody : CurrentWaterBodies)
	{
		if (WaterBody && WaterBody->GetWaterBodyType() == EWaterBodyType::River)
		{
			float SplineInputKey;
			if (CVarWaterUseSplineKeyOptimization.GetValueOnAnyThread())
			{
				SplineInputKey = GetWaterSplineKeyFast(Location, WaterBody, OutSegmentMap);
			}
			else
			{
				SplineInputKey = WaterBody->FindInputKeyClosestToWorldLocation(Location);
			}
			OutMap.Add(WaterBody, SplineInputKey);
		}
	}
}

float UBuoyancyComponent::GetWaterHeight(FVector Position, const TMap<const AWaterBody*, float>& SplineKeyMap, float DefaultHeight, AWaterBody*& OutWaterBody, float& OutWaterDepth, FVector& OutWaterPlaneLocation, FVector& OutWaterPlaneNormal, bool bShouldIncludeWaves)
{
	float WaterHeight = DefaultHeight;
	OutWaterBody = nullptr;
	OutWaterDepth = 0.f;
	OutWaterPlaneLocation = FVector::ZeroVector;
	OutWaterPlaneNormal = FVector::UpVector;

	float MaxImmersionDepth = -1.f;
	for (AWaterBody* CurrentWaterBody : CurrentWaterBodies)
	{
		if (CurrentWaterBody)
		{
			const float SplineInputKey = SplineKeyMap.FindRef(CurrentWaterBody);

			EWaterBodyQueryFlags QueryFlags =
				EWaterBodyQueryFlags::ComputeLocation
				| EWaterBodyQueryFlags::ComputeNormal
				| EWaterBodyQueryFlags::ComputeImmersionDepth;

			if (bShouldIncludeWaves)
			{
				QueryFlags |= EWaterBodyQueryFlags::IncludeWaves;
			}

			FWaterBodyQueryResult QueryResult = CurrentWaterBody->QueryWaterInfoClosestToWorldLocation(Position, QueryFlags, SplineInputKey);
			if (QueryResult.IsInWater() && QueryResult.GetImmersionDepth() > MaxImmersionDepth)
			{
				check(!QueryResult.IsInExclusionVolume());
				WaterHeight = Position.Z + QueryResult.GetImmersionDepth();
				OutWaterBody = CurrentWaterBody;
				if (EnumHasAnyFlags(QueryResult.GetQueryFlags(), EWaterBodyQueryFlags::ComputeDepth))
				{
					OutWaterDepth = QueryResult.GetWaterSurfaceDepth();
				}
				OutWaterPlaneLocation = QueryResult.GetWaterPlaneLocation();
				OutWaterPlaneNormal = QueryResult.GetWaterPlaneNormal();
				MaxImmersionDepth = QueryResult.GetImmersionDepth();
			}
		}
	}
	return WaterHeight;
}

float UBuoyancyComponent::GetWaterHeight(FVector Position, const TMap<const AWaterBody*, float>& SplineKeyMap, float DefaultHeight, bool bShouldIncludeWaves /*= true*/)
{
	AWaterBody* DummyActor;
	float DummyDepth;
	FVector DummyWaterPlaneLocation;
	FVector DummyWaterPlaneNormal;
	return GetWaterHeight(Position, SplineKeyMap, DefaultHeight, DummyActor, DummyDepth, DummyWaterPlaneLocation, DummyWaterPlaneNormal, bShouldIncludeWaves);
}

void UBuoyancyComponent::OnPontoonEnteredWater(const FSphericalPontoon& Pontoon)
{
	OnEnteredWaterDelegate.Broadcast(Pontoon);
}

void UBuoyancyComponent::OnPontoonExitedWater(const FSphericalPontoon& Pontoon)
{
	OnExitedWaterDelegate.Broadcast(Pontoon);
}

void UBuoyancyComponent::UpdatePontoonCoefficients()
{
	// Get current configuration mask
	uint32 NewPontoonConfiguration = 0;
	for (int32 PontoonIndex = 0; PontoonIndex < Pontoons.Num(); ++PontoonIndex)
	{
		if (Pontoons[PontoonIndex].bEnabled)
		{
			NewPontoonConfiguration |= 1 << PontoonIndex;
		}
	}

	// Store the new configuration, and return true if its value has changed.
	const bool bConfigurationChanged = PontoonConfiguration != NewPontoonConfiguration;
	PontoonConfiguration = NewPontoonConfiguration;

	// If the configuration changed, update coefficients
	if (bConfigurationChanged)
	{
		// Apply new configuration, recomputing coefficients if necessary
		ComputePontoonCoefficients();
	}
}

FVector UBuoyancyComponent::ComputeWaterForce(const float DeltaTime, const FVector LinearVelocity) const
{
	AActor* Owner = GetOwner();
	check(Owner);

	if (Pontoons.Num())
	{
		const FSphericalPontoon& Pontoon = Pontoons[VelocityPontoonIndex];
		const AWaterBody* WaterBody = Pontoon.CurrentWaterBody;
		if (WaterBody && WaterBody->GetWaterBodyType() == EWaterBodyType::River)
		{
			float InputKey = Pontoon.SplineInputKeys[WaterBody];
			const float WaterSpeed = WaterBody->GetWaterVelocityAtSplineInputKey(InputKey);

			const FVector SplinePointLocation = WaterBody->GetWaterSpline()->GetLocationAtSplineInputKey(InputKey, ESplineCoordinateSpace::World);
			// Move away from spline
			const FVector ShoreDirection = (Pontoon.CenterLocation - SplinePointLocation).GetSafeNormal2D();

			const float WaterShorePushfactor = WaterShorePushFactor;
			const FVector WaterDirection = WaterBody->GetWaterSpline()->GetDirectionAtSplineInputKey(InputKey, ESplineCoordinateSpace::World) * (1 - WaterShorePushfactor)
				+ ShoreDirection * (WaterShorePushfactor);
			const FVector WaterVelocity = WaterDirection * WaterSpeed;
			const UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
			check(PrimComp && PrimComp->GetBodyInstance());
			const FVector ActorVelocity = PrimComp->GetBodyInstance()->GetUnrealWorldVelocity();
			const float ActorSpeedInWaterDir = FMath::Abs(FVector::DotProduct(ActorVelocity, WaterDirection));
			if (ActorSpeedInWaterDir < WaterSpeed)
			{
				const FVector Acceleration = (WaterVelocity / DeltaTime) * WaterVelocityStrength;
				const float MaxWaterAcceleration = MaxWaterForce;
				return Acceleration.GetClampedToSize(-MaxWaterAcceleration, MaxWaterAcceleration);
			}
		}
	}
	return FVector::ZeroVector;
}