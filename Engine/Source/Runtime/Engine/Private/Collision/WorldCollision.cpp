// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	WorldCollision.cpp: UWorld collision implementation
=============================================================================*/

#include "WorldCollision.h"
#include "Misc/CoreMisc.h"
#include "EngineDefines.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Framework/Docking/TabManager.h"
#include "Collision.h"
#include "Collision/PhysXCollision.h"

DEFINE_LOG_CATEGORY(LogCollision);

/** Collision stats */


DEFINE_STAT(STAT_Collision_SceneQueryTotal);
DEFINE_STAT(STAT_Collision_RaycastAny);
DEFINE_STAT(STAT_Collision_RaycastSingle);
DEFINE_STAT(STAT_Collision_RaycastMultiple);
DEFINE_STAT(STAT_Collision_GeomSweepAny);
DEFINE_STAT(STAT_Collision_GeomSweepSingle);
DEFINE_STAT(STAT_Collision_GeomSweepMultiple);
DEFINE_STAT(STAT_Collision_GeomOverlapMultiple);
DEFINE_STAT(STAT_Collision_FBodyInstance_OverlapMulti);
DEFINE_STAT(STAT_Collision_FBodyInstance_OverlapTest);
DEFINE_STAT(STAT_Collision_FBodyInstance_LineTrace);
DEFINE_STAT(STAT_Collision_PreFilter);
DEFINE_STAT(STAT_Collision_PostFilter);

/** default collision response container - to be used without reconstructing every time**/
FCollisionResponseContainer FCollisionResponseContainer::DefaultResponseContainer(ECR_Block);

/* This is default response param that's used by trace query **/
FCollisionResponseParams		FCollisionResponseParams::DefaultResponseParam;
FCollisionObjectQueryParams		FCollisionObjectQueryParams::DefaultObjectQueryParam;
FCollisionQueryParams			FCollisionQueryParams::DefaultQueryParam(SCENE_QUERY_STAT(DefaultQueryParam), true);
FComponentQueryParams			FComponentQueryParams::DefaultComponentQueryParams(SCENE_QUERY_STAT(DefaultComponentQueryParams));
FCollisionShape					FCollisionShape::LineShape;

// default being the 0. That isn't invalid, but ObjectQuery param overrides this 
ECollisionChannel DefaultCollisionChannel = (ECollisionChannel) 0;


/* Set functions for each Shape type */
void FBaseTraceDatum::Set(UWorld * World, const FCollisionShape& InCollisionShape, const FCollisionQueryParams& Param, const struct FCollisionResponseParams &InResponseParam, const struct FCollisionObjectQueryParams& InObjectQueryParam,
	ECollisionChannel Channel, uint32 InUserData, int32 FrameCounter)
{
	ensure(World);
	CollisionParams.CollisionShape = InCollisionShape;
	CollisionParams.CollisionQueryParam = Param;
	CollisionParams.ResponseParam = InResponseParam;
	CollisionParams.ObjectQueryParam = InObjectQueryParam;
	TraceChannel = Channel;
	UserData = InUserData;
	FrameNumber = FrameCounter;
	PhysWorld = World;
}


//////////////////////////////////////////////////////////////////////////

bool UWorld::LineTraceTestByChannel(const FVector& Start,const FVector& End,ECollisionChannel TraceChannel, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */, const FCollisionResponseParams& ResponseParam /* = FCollisionResponseParams::DefaultResponseParam */) const
{
	return RaycastTest(this, Start, End, TraceChannel, Params, ResponseParam, FCollisionObjectQueryParams::DefaultObjectQueryParam);
}

bool UWorld::LineTraceSingleByChannel(struct FHitResult& OutHit,const FVector& Start,const FVector& End,ECollisionChannel TraceChannel,const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */, const FCollisionResponseParams& ResponseParam /* = FCollisionResponseParams::DefaultResponseParam */) const
{
	return RaycastSingle(this, OutHit, Start, End, TraceChannel, Params, ResponseParam, FCollisionObjectQueryParams::DefaultObjectQueryParam);
}

bool UWorld::LineTraceMultiByChannel(TArray<struct FHitResult>& OutHits,const FVector& Start,const FVector& End,ECollisionChannel TraceChannel,const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */, const FCollisionResponseParams& ResponseParam /* = FCollisionResponseParams::DefaultResponseParam */) const
{
	return RaycastMulti(this, OutHits, Start, End, TraceChannel, Params, ResponseParam, FCollisionObjectQueryParams::DefaultObjectQueryParam);
}

bool UWorld::SweepTestByChannel(const FVector& Start, const FVector& End, const FQuat& Rot, ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */, const FCollisionResponseParams& ResponseParam /* = FCollisionResponseParams::DefaultResponseParam */) const
{
	if (CollisionShape.IsNearlyZero())
	{
		// if extent is 0, we'll just do linetrace instead
		return LineTraceTestByChannel(Start, End, TraceChannel, Params, ResponseParam);
	}
	else
	{
		return GeomSweepTest(this, CollisionShape, Rot, Start, End, TraceChannel, Params, ResponseParam, FCollisionObjectQueryParams::DefaultObjectQueryParam);
	}
}

bool UWorld::SweepSingleByChannel(struct FHitResult& OutHit, const FVector& Start, const FVector& End, const FQuat& Rot, ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */, const FCollisionResponseParams& ResponseParam /* = FCollisionResponseParams::DefaultResponseParam */) const
{
	if (CollisionShape.IsNearlyZero())
	{
		return LineTraceSingleByChannel(OutHit, Start, End, TraceChannel, Params, ResponseParam);
	}
	else
	{
		return GeomSweepSingle(this, CollisionShape, Rot, OutHit, Start, End, TraceChannel, Params, ResponseParam, FCollisionObjectQueryParams::DefaultObjectQueryParam);
	}
}

bool UWorld::SweepMultiByChannel(TArray<struct FHitResult>& OutHits, const FVector& Start, const FVector& End, const FQuat& Rot, ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */, const FCollisionResponseParams& ResponseParam /* = FCollisionResponseParams::DefaultResponseParam */) const
{
	if (CollisionShape.IsNearlyZero())
	{
		return LineTraceMultiByChannel(OutHits, Start, End, TraceChannel, Params, ResponseParam);
	}
	else
	{
		return GeomSweepMulti(this, CollisionShape, Rot, OutHits, Start, End, TraceChannel, Params, ResponseParam, FCollisionObjectQueryParams::DefaultObjectQueryParam);
	}
}

bool UWorld::OverlapBlockingTestByChannel(const FVector& Pos, const FQuat& Rot, ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */, const FCollisionResponseParams& ResponseParam /* = FCollisionResponseParams::DefaultResponseParam */) const
{
	bool bBlocking = false;
	bBlocking = GeomOverlapBlockingTest(this, CollisionShape, Pos, Rot, TraceChannel, Params, ResponseParam, FCollisionObjectQueryParams::DefaultObjectQueryParam);
	return bBlocking;

}

bool UWorld::OverlapAnyTestByChannel(const FVector& Pos, const FQuat& Rot, ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */, const FCollisionResponseParams& ResponseParam /* = FCollisionResponseParams::DefaultResponseParam */) const
{
	bool bBlocking = false;
	bBlocking = GeomOverlapAnyTest(this, CollisionShape, Pos, Rot, TraceChannel, Params, ResponseParam, FCollisionObjectQueryParams::DefaultObjectQueryParam);
	return bBlocking;

}

bool UWorld::OverlapMultiByChannel(TArray<struct FOverlapResult>& OutOverlaps, const FVector& Pos, const FQuat& Rot, ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */, const FCollisionResponseParams& ResponseParam /* = FCollisionResponseParams::DefaultResponseParam */) const
{
	return GeomOverlapMulti(this, CollisionShape, Pos, Rot, OutOverlaps, TraceChannel, Params, ResponseParam, FCollisionObjectQueryParams::DefaultObjectQueryParam);
}

// object query interfaces

bool UWorld::OverlapMultiByObjectType(TArray<struct FOverlapResult>& OutOverlaps, const FVector& Pos, const FQuat& Rot, const FCollisionObjectQueryParams& ObjectQueryParams, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */) const
{
	GeomOverlapMulti(this, CollisionShape, Pos, Rot, OutOverlaps, DefaultCollisionChannel, Params, FCollisionResponseParams::DefaultResponseParam, ObjectQueryParams);

	// object query returns true if any hit is found, not only blocking hit
	return (OutOverlaps.Num() > 0);
}

bool UWorld::LineTraceTestByObjectType(const FVector& Start,const FVector& End,const FCollisionObjectQueryParams& ObjectQueryParams, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */) const
{
	return RaycastTest(this, Start, End, DefaultCollisionChannel, Params, FCollisionResponseParams::DefaultResponseParam, ObjectQueryParams);
}

bool UWorld::LineTraceSingleByObjectType(struct FHitResult& OutHit,const FVector& Start,const FVector& End,const FCollisionObjectQueryParams& ObjectQueryParams, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */) const
{
	return RaycastSingle(this, OutHit, Start, End, DefaultCollisionChannel, Params, FCollisionResponseParams::DefaultResponseParam, ObjectQueryParams);
}

bool UWorld::LineTraceMultiByObjectType(TArray<struct FHitResult>& OutHits,const FVector& Start,const FVector& End,const FCollisionObjectQueryParams& ObjectQueryParams, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */) const
{
	RaycastMulti(this, OutHits, Start, End, DefaultCollisionChannel, Params, FCollisionResponseParams::DefaultResponseParam, ObjectQueryParams);

	// object query returns true if any hit is found, not only blocking hit
	return (OutHits.Num() > 0);
}

bool UWorld::SweepTestByObjectType(const FVector& Start, const FVector& End, const FQuat& Rot, const FCollisionObjectQueryParams& ObjectQueryParams, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */) const
{
	if (CollisionShape.IsNearlyZero())
	{
		// if extent is 0, we'll just do linetrace instead
		return LineTraceTestByObjectType(Start, End, ObjectQueryParams, Params);
	}
	else
	{
		return GeomSweepTest(this, CollisionShape, Rot, Start, End, DefaultCollisionChannel, Params, FCollisionResponseParams::DefaultResponseParam, ObjectQueryParams);
	}

}

bool UWorld::SweepSingleByObjectType(struct FHitResult& OutHit, const FVector& Start, const FVector& End, const FQuat& Rot, const FCollisionObjectQueryParams& ObjectQueryParams, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */) const
{
	if (CollisionShape.IsNearlyZero())
	{
		return LineTraceSingleByObjectType(OutHit, Start, End, ObjectQueryParams, Params);
	}
	else
	{
		return GeomSweepSingle(this, CollisionShape, Rot, OutHit, Start, End, DefaultCollisionChannel, Params, FCollisionResponseParams::DefaultResponseParam, ObjectQueryParams);
	}

}

bool UWorld::SweepMultiByObjectType(TArray<struct FHitResult>& OutHits, const FVector& Start, const FVector& End, const FQuat& Rot, const FCollisionObjectQueryParams& ObjectQueryParams, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */) const
{
	if (CollisionShape.IsNearlyZero())
	{
		return LineTraceMultiByObjectType(OutHits, Start, End, ObjectQueryParams, Params);
	}
	else
	{
		GeomSweepMulti(this, CollisionShape, Rot, OutHits, Start, End, DefaultCollisionChannel, Params, FCollisionResponseParams::DefaultResponseParam, ObjectQueryParams);

		// object query returns true if any hit is found, not only blocking hit
		return (OutHits.Num() > 0);
	}
}

bool UWorld::OverlapAnyTestByObjectType(const FVector& Pos, const FQuat& Rot, const FCollisionObjectQueryParams& ObjectQueryParams, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params /* = FCollisionQueryParams::DefaultQueryParam */) const
{
	bool bBlocking = false;
	bBlocking = GeomOverlapAnyTest(this, CollisionShape, Pos, Rot, DefaultCollisionChannel, Params, FCollisionResponseParams::DefaultResponseParam, ObjectQueryParams);
	return bBlocking;
}

// profile interfaces
static void GetCollisionProfileChannelAndResponseParams(FName ProfileName, ECollisionChannel &CollisionChannel, FCollisionResponseParams &ResponseParams)
{
	if (UCollisionProfile::GetChannelAndResponseParams(ProfileName, CollisionChannel, ResponseParams))
	{
		return;
	}

	// No profile found
	UE_LOG(LogPhysics, Warning, TEXT("COLLISION PROFILE [%s] is not found"), *ProfileName.ToString());

	CollisionChannel = ECC_WorldStatic;
	ResponseParams = FCollisionResponseParams::DefaultResponseParam;
}

bool UWorld::LineTraceTestByProfile(const FVector& Start, const FVector& End, FName ProfileName, const struct FCollisionQueryParams& Params) const
{
	ECollisionChannel TraceChannel;
	FCollisionResponseParams ResponseParam;
	GetCollisionProfileChannelAndResponseParams(ProfileName, TraceChannel, ResponseParam);

	return LineTraceTestByChannel(Start, End, TraceChannel, Params, ResponseParam);
}

bool UWorld::LineTraceSingleByProfile(struct FHitResult& OutHit, const FVector& Start, const FVector& End, FName ProfileName, const struct FCollisionQueryParams& Params) const
{
	ECollisionChannel TraceChannel;
	FCollisionResponseParams ResponseParam;
	GetCollisionProfileChannelAndResponseParams(ProfileName, TraceChannel, ResponseParam);

	return LineTraceSingleByChannel(OutHit, Start, End, TraceChannel, Params, ResponseParam);
}

bool UWorld::LineTraceMultiByProfile(TArray<struct FHitResult>& OutHits, const FVector& Start, const FVector& End, FName ProfileName, const struct FCollisionQueryParams& Params) const
{
	ECollisionChannel TraceChannel;
	FCollisionResponseParams ResponseParam;
	GetCollisionProfileChannelAndResponseParams(ProfileName, TraceChannel, ResponseParam);

	return LineTraceMultiByChannel(OutHits, Start, End, TraceChannel, Params, ResponseParam);
}

bool UWorld::SweepTestByProfile(const FVector& Start, const FVector& End, const FQuat& Rot, FName ProfileName, const struct FCollisionShape& CollisionShape, const struct FCollisionQueryParams& Params) const
{
	ECollisionChannel TraceChannel;
	FCollisionResponseParams ResponseParam;
	GetCollisionProfileChannelAndResponseParams(ProfileName, TraceChannel, ResponseParam);

	return SweepTestByChannel(Start, End, Rot, TraceChannel, CollisionShape, Params, ResponseParam);
}

bool UWorld::SweepSingleByProfile(struct FHitResult& OutHit, const FVector& Start, const FVector& End, const FQuat& Rot, FName ProfileName, const struct FCollisionShape& CollisionShape, const struct FCollisionQueryParams& Params) const
{
	ECollisionChannel TraceChannel;
	FCollisionResponseParams ResponseParam;
	GetCollisionProfileChannelAndResponseParams(ProfileName, TraceChannel, ResponseParam);

	return SweepSingleByChannel(OutHit, Start, End, Rot, TraceChannel, CollisionShape, Params, ResponseParam);
}

bool UWorld::SweepMultiByProfile(TArray<FHitResult>& OutHits, const FVector& Start, const FVector& End, const FQuat& Rot, FName ProfileName, const struct FCollisionShape& CollisionShape, const struct FCollisionQueryParams& Params) const
{
	ECollisionChannel TraceChannel;
	FCollisionResponseParams ResponseParam;
	GetCollisionProfileChannelAndResponseParams(ProfileName, TraceChannel, ResponseParam);

	return SweepMultiByChannel(OutHits, Start, End, Rot, TraceChannel, CollisionShape, Params, ResponseParam);
}

bool UWorld::OverlapBlockingTestByProfile(const FVector& Pos, const FQuat& Rot, FName ProfileName, const struct FCollisionShape& CollisionShape, const struct FCollisionQueryParams& Params) const
{
	ECollisionChannel TraceChannel;
	FCollisionResponseParams ResponseParam;
	GetCollisionProfileChannelAndResponseParams(ProfileName, TraceChannel, ResponseParam);

	return OverlapBlockingTestByChannel(Pos, Rot, TraceChannel, CollisionShape, Params, ResponseParam);
}

bool UWorld::OverlapAnyTestByProfile(const FVector& Pos, const FQuat& Rot, FName ProfileName, const struct FCollisionShape& CollisionShape, const struct FCollisionQueryParams& Params) const
{
	ECollisionChannel TraceChannel;
	FCollisionResponseParams ResponseParam;
	GetCollisionProfileChannelAndResponseParams(ProfileName, TraceChannel, ResponseParam);

	return OverlapAnyTestByChannel(Pos, Rot, TraceChannel, CollisionShape, Params, ResponseParam);
}

bool UWorld::OverlapMultiByProfile(TArray<struct FOverlapResult>& OutOverlaps, const FVector& Pos, const FQuat& Rot, FName ProfileName, const struct FCollisionShape& CollisionShape, const struct FCollisionQueryParams& Params) const
{
	ECollisionChannel TraceChannel;
	FCollisionResponseParams ResponseParam;
	GetCollisionProfileChannelAndResponseParams(ProfileName, TraceChannel, ResponseParam);

	return OverlapMultiByChannel(OutOverlaps, Pos, Rot, TraceChannel, CollisionShape, Params, ResponseParam);
}


bool UWorld::ComponentOverlapMulti(TArray<struct FOverlapResult>& OutOverlaps, const class UPrimitiveComponent* PrimComp, const FVector& Pos, const FQuat& Quat, const FComponentQueryParams& Params, const FCollisionObjectQueryParams& ObjectQueryParams) const
{
	if (PrimComp)
	{
		ComponentOverlapMultiByChannel(OutOverlaps, PrimComp, Pos, Quat, PrimComp->GetCollisionObjectType(), Params, ObjectQueryParams);
		
		// object query returns true if any hit is found, not only blocking hit
		return (OutOverlaps.Num() > 0);
	}
	else
	{
		UE_LOG(LogCollision, Log, TEXT("ComponentOverlapMulti : No PrimComp"));
		return false;
	}
}

bool UWorld::ComponentOverlapMultiByChannel(TArray<struct FOverlapResult>& OutOverlaps, const class UPrimitiveComponent* PrimComp, const FVector& Pos, const FQuat& Quat, ECollisionChannel TraceChannel, const FComponentQueryParams& Params /* = FComponentQueryParams::DefaultComponentQueryParams */, const FCollisionObjectQueryParams& ObjectQueryParams/* =FCollisionObjectQueryParams::DefaultObjectQueryParam */) const
{
	if (PrimComp)
	{
		return PrimComp->ComponentOverlapMulti(OutOverlaps, this, Pos, Quat, TraceChannel, Params, ObjectQueryParams);
	}
	else
	{
		UE_LOG(LogCollision, Log, TEXT("ComponentOverlapMulti : No PrimComp"));
		return false;
	}
}

bool UWorld::ComponentSweepMulti(TArray<struct FHitResult>& OutHits, class UPrimitiveComponent* PrimComp, const FVector& Start, const FVector& End, const FQuat& Quat, const FComponentQueryParams& Params) const
{
	if (GetPhysicsScene() == NULL)
	{
		return false;
	}

	if (PrimComp == NULL)
	{
		UE_LOG(LogCollision, Log, TEXT("ComponentSweepMulti : No PrimComp"));
		return false;
	}

	ECollisionChannel TraceChannel = PrimComp->GetCollisionObjectType();

	// if extent is 0, do line trace
	if (PrimComp->IsZeroExtent())
	{
		return RaycastMulti(this, OutHits, Start, End, TraceChannel, Params, FCollisionResponseParams(PrimComp->GetCollisionResponseToChannels()));
	}

	OutHits.Reset();

	const FBodyInstance* BodyInstance = PrimComp->GetBodyInstance();

	if (!BodyInstance || !BodyInstance->IsValidBodyInstance())
	{
		UE_LOG(LogCollision, Log, TEXT("ComponentSweepMulti : (%s) No physics data"), *PrimComp->GetReadableName());
		return false;
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if(PrimComp->IsA(USkeletalMeshComponent::StaticClass()))
	{
		UE_LOG(LogCollision, Log, TEXT("ComponentSweepMulti : SkeletalMeshComponent support only root body (%s) "), *PrimComp->GetReadableName());
	}
#endif

	SCOPE_CYCLE_COUNTER(STAT_Collision_GeomSweepMultiple);
	bool bHaveBlockingHit = false;

#if WITH_PHYSX
	ExecuteOnPxRigidActorReadOnly(BodyInstance, [&] (const PxRigidActor* PRigidActor)
	{
		// Get all the shapes from the actor
		FInlinePxShapeArray PShapes;
		const int32 NumShapes = FillInlinePxShapeArray_AssumesLocked(PShapes, *PRigidActor);

		// calculate the test global pose of the actor
		const PxQuat PGeomRot = U2PQuat(Quat);
		const PxTransform PGlobalStartPose = PxTransform(U2PVector(Start), PGeomRot);
		const PxTransform PGlobalEndPose = PxTransform(U2PVector(End), PGeomRot);

		// Iterate over each shape
		for(int32 ShapeIdx=0; ShapeIdx<NumShapes; ShapeIdx++)
		{
			PxShape* PShape = PShapes[ShapeIdx];
			check(PShape);

			PxGeometryType::Enum ShapeType = PShape->getGeometryType();
			if(ShapeType == PxGeometryType::eHEIGHTFIELD || ShapeType == PxGeometryType::eTRIANGLEMESH)
			{
				//We skip complex shapes. Should this respect complex as simple?
				continue;
			}

			// Calc shape global pose
			const PxTransform PLocalShape = PShape->getLocalPose();
			const PxTransform PShapeGlobalStartPose = PGlobalStartPose.transform(PLocalShape);
			const PxTransform PShapeGlobalEndPose = PGlobalEndPose.transform(PLocalShape);
			// consider localshape rotation for shape rotation
			const PxQuat PShapeRot = PGeomRot * PLocalShape.q;

			if (GeomSweepMulti_PhysX(this, PShape->getGeometry().any(), PShapeRot, OutHits, P2UVector(PShapeGlobalStartPose.p), P2UVector(PShapeGlobalEndPose.p), TraceChannel, Params, FCollisionResponseParams(PrimComp->GetCollisionResponseToChannels())))
			{
				bHaveBlockingHit = true;
			}
		}
	});
#endif //WITH_PHYSX

	return bHaveBlockingHit;
}

#if ENABLE_COLLISION_ANALYZER


static class FCollisionExec : private FSelfRegisteringExec
{
public:
	/** Console commands, see embeded usage statement **/
	virtual bool Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar ) override
	{
#if ENABLE_COLLISION_ANALYZER
		if (FParse::Command(&Cmd, TEXT("CANALYZER")))
		{
			FGlobalTabmanager::Get()->InvokeTab(FName(TEXT("CollisionAnalyzerApp")));
			return true;
		}
#endif // ENABLE_COLLISION_ANALYZER
		return false;
	}
} CollisionExec;

#endif // ENABLE_COLLISION_ANALYZER

