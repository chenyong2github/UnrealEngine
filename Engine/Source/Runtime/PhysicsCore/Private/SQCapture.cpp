// Copyright Epic Games, Inc. All Rights Reserved.

#include "SQCapture.h"

#include "Chaos/ImplicitObject.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "PhysicsInterfaceTypesCore.h"

#include "PhysXSupportCore.h"
#include "PhysicsPublicCore.h"
#include "PhysTestSerializer.h" 

#if PHYSICS_INTERFACE_PHYSX
#include "PhysXToChaosUtil.h"
#endif

class FSQCaptureFilterCallback : public ICollisionQueryFilterCallbackBase
{
public:
	FSQCaptureFilterCallback(const FSQCapture& InCapture) : Capture(InCapture) {}
	virtual ~FSQCaptureFilterCallback() {}
	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const ChaosInterface::FQueryHit& Hit) override { /*check(false);*/  return ECollisionQueryHitType::Touch; }
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const Chaos::FPerShapeData& Shape, const Chaos::FGeometryParticle& Actor) override { return Capture.GetFilterResult(&Shape, &Actor); }

#if PHYSICS_INTERFACE_PHYSX
	virtual ECollisionQueryHitType PostFilter(const FCollisionFilterData& FilterData, const physx::PxQueryHit& Hit) override { return ECollisionQueryHitType::Touch; }
	virtual ECollisionQueryHitType PreFilter(const FCollisionFilterData& FilterData, const physx::PxShape& Shape, physx::PxRigidActor& Actor) override { return Capture.GetFilterResult(&Shape, &Actor); }
	virtual PxQueryHitType::Enum preFilter(const PxFilterData& filterData, const PxShape* shape, const PxRigidActor* actor, PxHitFlags& queryFlags) override { return U2PCollisionQueryHitType(Capture.GetFilterResult(shape, actor)); }
	virtual PxQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxQueryHit& hit) override { return PxQueryHitType::eTOUCH; }
#endif

private:
	const FSQCapture& Capture;
};

FSQCapture::FSQCapture(FPhysTestSerializer& InPhysSerializer)
	: OutputFlags(EHitFlags::None)
	, ChaosGeometry(nullptr)
	, PhysSerializer(InPhysSerializer)
	, bDiskDataIsChaos(false)
	, bChaosDataReady(false)
	, bPhysXDataReady(false)
{
}

FSQCapture::~FSQCapture()
{
}

#if PHYSICS_INTERFACE_PHYSX

PxActor* FSQCapture::GetTransientActor(PxActor* Actor) const
{
	PxActor* TransientActor = NonTransientToTransientActors.FindRef(Actor);
	return TransientActor ? TransientActor : Actor;
}

PxShape* FSQCapture::GetTransientShape(PxShape* Shape) const
{
	PxShape* TransientShape = NonTransientToTransientShapes.FindRef(Shape);
	return TransientShape ? TransientShape : Shape;
}

void FSQCapture::SerializePhysXHitType(FArchive& Ar, PxOverlapHit& Hit)
{
	uint64 Actor = (PxSerialObjectId)GetTransientActor(Hit.actor);
	uint64 Shape = (PxSerialObjectId)GetTransientShape(Hit.shape);

	Ar << Actor << Shape << Hit.faceIndex;
	Hit.actor = (PxRigidActor*)Actor;	//todo: 32bit, should be ok as long as you never load a 64bit capture on a 32bit machine. If compiler error just add some checks and do conversion manually
	Hit.shape = (PxShape*)Shape;
}

template <typename T>
void FSQCapture::SerializePhysXHitType(FArchive& Ar, T& Hit)
{
	uint64 Actor = (PxSerialObjectId)GetTransientActor(Hit.actor);
	uint64 Shape = (PxSerialObjectId)GetTransientShape(Hit.shape);
	FVector Position = P2UVector(Hit.position);
	FVector Normal = P2UVector(Hit.normal);
	uint16 HitFlags = Hit.flags;

	Ar << Actor << Shape << Hit.faceIndex << HitFlags << Position << Normal << Hit.distance;
	Hit.actor = (PxRigidActor*)Actor;	//todo: 32bit, should be ok as long as you never load a 64bit capture on a 32bit machine. If compiler error just add some checks and do conversion manually
	Hit.shape = (PxShape*)Shape;
	Hit.position = U2PVector(Position);
	Hit.normal = U2PVector(Normal);
	Hit.flags = (PxHitFlags)HitFlags;
}

template <typename THit>
void FSQCapture::SerializePhysXBuffers(FArchive& Ar, int32 Version, PhysXInterface::FDynamicHitBuffer<THit>& PhysXBuffer)
{
	Ar << PhysXBuffer.hasBlock;
	if (PhysXBuffer.hasBlock)
	{
		SerializePhysXHitType(Ar, PhysXBuffer.block);
	}

	TArray<THit> TmpHits;
	THit* Hits;
	int32 NumHits;
	if (Version < 1)
	{
		Ar << PhysXBuffer.maxNbTouches << PhysXBuffer.nbTouches;
		TmpHits.AddDefaulted(PhysXBuffer.nbTouches);
		Hits = TmpHits.GetData();
		NumHits = TmpHits.Num();
	}
	else
	{
		NumHits = PhysXBuffer.GetNumHits();
		Ar << NumHits;
		if (Ar.IsLoading())
		{
			TmpHits.AddDefaulted(NumHits);
			PhysXBuffer.processTouches(TmpHits.GetData(), NumHits);
		}

		Hits = PhysXBuffer.GetHits();
	}

	for (int32 Idx = 0; Idx < NumHits; ++Idx)
	{
		SerializePhysXHitType(Ar, Hits[Idx]);
	}
}

void FSQCapture::SerializeActorToShapeHitsArray(FArchive& Ar)
{
	int32 NumActors = PxActorToShapeHitsArray.Num();
	Ar << NumActors;
	if (Ar.IsLoading())
	{
		for (int32 ActorIdx = 0; ActorIdx < NumActors; ++ActorIdx)
		{
			uint64 Actor;
			Ar << Actor;
			int32 NumShapes;
			Ar << NumShapes;

			TArray<TPair<PxShape*, ECollisionQueryHitType>> Pairs;
			for (int32 ShapeIdx = 0; ShapeIdx < NumShapes; ++ShapeIdx)
			{
				uint64 Shape;
				Ar << Shape;
				ECollisionQueryHitType HitType;
				Ar << HitType;
				PxShape* ShapePtr = static_cast<PxShape*>(PhysSerializer.FindObject(Shape));
				check(ShapePtr);
				Pairs.Emplace(ShapePtr, HitType);
			}
			PxActor* ActorPtr = static_cast<PxActor*>(PhysSerializer.FindObject(Actor));
			check(ActorPtr);
			PxActorToShapeHitsArray.Add(ActorPtr, Pairs);
		}
	}
	else if (Ar.IsSaving())
	{
		for (auto& Itr : PxActorToShapeHitsArray)
		{
			uint64 Actor = (PxSerialObjectId)NonTransientToTransientActors.FindChecked(Itr.Key);
			Ar << Actor;
			int32 NumShapes = Itr.Value.Num();
			Ar << NumShapes;

			for (auto& Pair : Itr.Value)
			{
				uint64 Shape = (PxSerialObjectId)NonTransientToTransientShapes.FindChecked(Pair.Key);
				Ar << Shape;
				Ar << Pair.Value;

			}
		}
	}
}

#endif

void SerializeQueryFilterData(FArchive& Ar, FQueryFilterData& QueryFilterData)
{
	Ar << QueryFilterData.data.word0;
	Ar << QueryFilterData.data.word1;
	Ar << QueryFilterData.data.word2;
	Ar << QueryFilterData.data.word3;
	uint16 Flags = QueryFilterData.flags;
	Ar << Flags;
#if PHYSICS_INTERFACE_PHYSX
	QueryFilterData.flags = (PxQueryFlags)Flags;
#else
	QueryFilterData.flags = (FChaosQueryFlags)Flags;
#endif
	Ar << QueryFilterData.clientId;
}

void FSQCapture::SerializeChaosActorToShapeHitsArray(Chaos::FChaosArchive& Ar)
{
	int32 NumActors = ChaosActorToShapeHitsArray.Num();
	Ar << NumActors;
	if (Ar.IsLoading())
	{
		for (int32 ActorIdx = 0; ActorIdx < NumActors; ++ActorIdx)
		{
			Chaos::TSerializablePtr<Chaos::FGeometryParticle>Actor;
			Ar << Actor;
			int32 NumShapes;
			Ar << NumShapes;

			TArray<TPair<Chaos::FPerShapeData*, ECollisionQueryHitType>> Pairs;
			for (int32 ShapeIdx = 0; ShapeIdx < NumShapes; ++ShapeIdx)
			{
				Chaos::TSerializablePtr<Chaos::FPerShapeData> Shape;
				Ar << Shape;
				ECollisionQueryHitType HitType;
				Ar << HitType;
				Pairs.Emplace(const_cast<Chaos::FPerShapeData*>(Shape.Get()), HitType);
			}
			ChaosActorToShapeHitsArray.Add(const_cast<Chaos::FGeometryParticle*>(Actor.Get()), Pairs);
		}
	}
	else if (Ar.IsSaving())
	{
		for (auto& Itr : ChaosActorToShapeHitsArray)
		{
			Ar << AsAlwaysSerializable(Itr.Key);
			int32 NumShapes = Itr.Value.Num();
			Ar << NumShapes;

			for (auto& Pair : Itr.Value)
			{
				Ar << AsAlwaysSerializable(Pair.Key);
				Ar << Pair.Value;

			}
		}
	}
}

template <typename THit>
void FSQCapture::SerializeChaosBuffers(Chaos::FChaosArchive& Ar, int32 Version, ChaosInterface::FSQHitBuffer<THit>& ChaosBuffer)
{
	bool bHasBlock = ChaosBuffer.HasBlockingHit();
	Ar << bHasBlock;

	if (bHasBlock)
	{
		if (Ar.IsLoading())
		{
			THit Hit;
			Ar << Hit;
			ChaosBuffer.SetBlockingHit(Hit);
		}
		else
		{
			THit Hit = *ChaosBuffer.GetBlock();
			Ar << Hit;
		}
	}

	int32 NumHits;
	
	NumHits = ChaosBuffer.GetNumHits();
	Ar << NumHits;

	for (int32 Idx = 0; Idx < NumHits; ++Idx)
	{
		if (Ar.IsLoading())
		{
			THit Touch;
			Ar << Touch;
			ChaosBuffer.AddTouchingHit(Touch);
		}
		else
		{
			THit* Hits = ChaosBuffer.GetHits();
			Ar << Hits[Idx];
		}
	}
	
}

void FSQCapture::Serialize(Chaos::FChaosArchive& Ar)
{
	static const FName SQCaptureName = TEXT("SQCapture");
	Chaos::FChaosArchiveScopedMemory ScopedMemory(Ar, SQCaptureName, false);

	int32 Version = 2;
	Ar << Version;
	Ar << SQType;
	Ar << bDiskDataIsChaos;
	Ar << Dir << StartTM << DeltaMag << OutputFlags;
	Ar << GeomData;
	Ar << HitData;

	if (Version >= 1)
	{
		Ar << StartPoint;
	}

#if WITH_PHYSX
	if (bDiskDataIsChaos == false)
	{
#if PHYSICS_INTERFACE_PHYSX
		SerializePhysXBuffers(Ar, Version, PhysXSweepBuffer);

		if (Version >= 1)
		{
			SerializePhysXBuffers(Ar, Version, PhysXRaycastBuffer);
			SerializePhysXBuffers(Ar, Version, PhysXOverlapBuffer);

			SerializeActorToShapeHitsArray(Ar);
		}

		if (Ar.IsLoading())
		{
			CreatePhysXData();
		}
#endif
		if(Version >= 1)
		{
			SerializeQueryFilterData(Ar, QueryFilterData);
		}
	}
#endif

#if WITH_CHAOS
	if (bDiskDataIsChaos)
	{
#if WITH_CHAOS
		SerializeChaosBuffers(Ar, Version, ChaosSweepBuffer);
		SerializeChaosBuffers(Ar, Version, ChaosRaycastBuffer);
		SerializeChaosBuffers(Ar, Version, ChaosOverlapBuffer);
#endif // WITH_CHAOS

		SerializeChaosActorToShapeHitsArray(Ar);
		SerializeQueryFilterData(Ar, QueryFilterData);

		if (Version >= 2)
		{
			Ar << SerializableChaosGeometry;
			ChaosGeometry = SerializableChaosGeometry.Get();
		}
	}
#endif

	if (Ar.IsLoading())
	{
#if 0
		CreateChaosDataFromPhysX();
#endif // WITH_PHYSX

		if (bDiskDataIsChaos)
		{
			FilterCallback = MakeUnique<FSQCaptureFilterCallback>(*this);
		}
	}
}

void FSQCapture::StartCaptureChaosSweep(const Chaos::FPBDRigidsEvolution& Evolution, const Chaos::FImplicitObject& InQueryGeom, const FTransform& InStartTM, const FVector& InDir, float InDeltaMag, FHitFlags InOutputFlags, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback)
{
	if (IsInGameThread())
	{
		bDiskDataIsChaos = true;
		CaptureChaosFilterResults(Evolution, FilterData, Callback);
		//copy data
		SerializableChaosGeometry = InQueryGeom.Copy();
		ChaosGeometry = SerializableChaosGeometry.Get();
		StartTM = InStartTM;
		Dir = InDir;
		DeltaMag = InDeltaMag;
		OutputFlags = InOutputFlags;
		QueryFilterData = QueryFilter;

		SQType = ESQType::Sweep;
	}
}

void FSQCapture::EndCaptureChaosSweep(const ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>& Results)
{
#if WITH_CHAOS
	if (IsInGameThread())
	{
		check(SQType == ESQType::Sweep);
#if WITH_CHAOS
		ChaosSweepBuffer = Results;
#endif // WITH_CHAOS
	}
#endif
}

void FSQCapture::StartCaptureChaosRaycast(const Chaos::FPBDRigidsEvolution& Evolution, const FVector& InStartPoint, const FVector& InDir, float InDeltaMag, FHitFlags InOutputFlags, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback)
{
	if (IsInGameThread())
	{
		bDiskDataIsChaos = true;
		CaptureChaosFilterResults(Evolution, FilterData, Callback);
		//copy data
		StartPoint = InStartPoint;
		Dir = InDir;
		DeltaMag = InDeltaMag;
		OutputFlags = InOutputFlags;
		QueryFilterData = QueryFilter;

		SQType = ESQType::Raycast;
	}
}

void FSQCapture::EndCaptureChaosRaycast(const ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>& Results)
{
#if WITH_CHAOS
	if (IsInGameThread())
	{
		check(SQType == ESQType::Raycast);
#if WITH_CHAOS
		ChaosRaycastBuffer = Results;
#endif // WITH_CHAOS
	}
#endif
}

void FSQCapture::StartCaptureChaosOverlap(const Chaos::FPBDRigidsEvolution& Evolution, const Chaos::FImplicitObject& InQueryGeom, const FTransform& InStartTM, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback)
{
	if (IsInGameThread())
	{
		bDiskDataIsChaos = true;
		CaptureChaosFilterResults(Evolution, FilterData, Callback);
		//copy data
		SerializableChaosGeometry = InQueryGeom.Copy();
		ChaosGeometry = SerializableChaosGeometry.Get();
		StartTM = InStartTM;
		QueryFilterData = QueryFilter;

		SQType = ESQType::Overlap;
	}
}

void FSQCapture::EndCaptureChaosOverlap(const ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& Results)
{
#if WITH_CHAOS
	if (IsInGameThread())
	{
		check(SQType == ESQType::Overlap);
#if WITH_CHAOS
		ChaosOverlapBuffer = Results;
#endif // WITH_CHAOS
	}
#endif
}

void FSQCapture::CaptureChaosFilterResults(const Chaos::FPBDRigidsEvolution& TransientEvolution, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback)
{
	using namespace Chaos;
	const FPBDRigidsSOAs& Particles = TransientEvolution.GetParticles();
	const int32 NumTransientActors = Particles.GetParticleHandles().Size();

	for (int32 Idx = 0; Idx < NumTransientActors; ++Idx)
	{
		FGeometryParticle* TransientActor = Particles.GetParticleHandles().Handle(Idx)->GTGeometryParticle();
		const FShapesArray& TransientShapes = TransientActor->ShapesArray();
		const int32 NumTransientShapes = TransientShapes.Num();
		TArray<TPair<FPerShapeData*, ECollisionQueryHitType>> ShapeHitsArray; ShapeHitsArray.Reserve(NumTransientShapes);
		for (const auto& TransientShape : TransientShapes)
		{
			const ECollisionQueryHitType Result = Callback.PreFilter(FilterData, *TransientShape, *TransientActor);
			ShapeHitsArray.Emplace(TransientShape.Get(), Result);
		}

		ChaosActorToShapeHitsArray.Add(TransientActor, ShapeHitsArray);
	}
}

template <typename TShape, typename TActor>
ECollisionQueryHitType GetFilterResultHelper(const TShape* Shape, const TActor* Actor, const TMap<TActor*, TArray<TPair<TShape*, ECollisionQueryHitType>>>& ActorToShapeHitsArray)
{
	if (const TArray<TPair<TShape*, ECollisionQueryHitType>>* ActorToPairs = ActorToShapeHitsArray.Find(Actor))
	{
		for (const TPair<TShape*, ECollisionQueryHitType>& Pair : *ActorToPairs)
		{
			if (Pair.Key == Shape)
			{
				return Pair.Value;
			}
		}
	}

	//todo: figure out why this hits - suspect it's related to threading and how we capture an evolution on GT
	ensure(false);	//should not get here, means we didn't properly capture all filter results
	return ECollisionQueryHitType::None;
}

ECollisionQueryHitType FSQCapture::GetFilterResult(const Chaos::FPerShapeData* Shape, const Chaos::FGeometryParticle* Actor) const
{
	return GetFilterResultHelper(Shape, Actor, ChaosActorToShapeHitsArray);
}


#if PHYSICS_INTERFACE_PHYSX
void FSQCapture::StartCapturePhysXSweep(const PxScene& Scene, const PxGeometry& InQueryGeom, const FTransform& InStartTM, const FVector& InDir, float InDeltaMag, FHitFlags InOutputFlags, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback)
{
	if (IsInGameThread())
	{
		bDiskDataIsChaos = false;
		CapturePhysXFilterResults(Scene, FilterData, Callback);
		//copy data
		PhysXGeometry.storeAny(InQueryGeom);
		StartTM = InStartTM;
		Dir = InDir;
		DeltaMag = InDeltaMag;
		OutputFlags = InOutputFlags;
		QueryFilterData = QueryFilter;

		SQType = ESQType::Sweep;
		SetPhysXGeometryData(InQueryGeom);
	}
}

void FSQCapture::StartCapturePhysXRaycast(const PxScene& Scene, const FVector& InStartPoint, const FVector& InDir, float InDeltaMag, FHitFlags InOutputFlags, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback)
{
	if (IsInGameThread())
	{
		bDiskDataIsChaos = false;
		CapturePhysXFilterResults(Scene, FilterData, Callback);
		//copy data
		StartPoint = InStartPoint;
		Dir = InDir;
		DeltaMag = InDeltaMag;
		OutputFlags = InOutputFlags;
		QueryFilterData = QueryFilter;

		SQType = ESQType::Raycast;
	}
}

void FSQCapture::StartCapturePhysXOverlap(const PxScene& Scene, const PxGeometry& InQueryGeom, const FTransform& WorldTM, const FQueryFilterData& QueryFilter, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback)
{
	if (IsInGameThread())
	{
		bDiskDataIsChaos = false;
		CapturePhysXFilterResults(Scene, FilterData, Callback);
		//copy data
		StartTM = WorldTM;
		QueryFilterData = QueryFilter;

		SQType = ESQType::Overlap;
		SetPhysXGeometryData(InQueryGeom);
	}
}

void FSQCapture::CapturePhysXFilterResults(const PxScene& TransientScene, const FCollisionFilterData& FilterData, ICollisionQueryFilterCallbackBase& Callback)
{
	const uint32 NumTransientActors = TransientScene.getNbActors(PxActorTypeFlag::eRIGID_STATIC | PxActorTypeFlag::eRIGID_DYNAMIC);
	TArray<PxActor*> TransientActors; TransientActors.AddUninitialized(NumTransientActors);
	if (NumTransientActors)
	{
		TransientScene.getActors(PxActorTypeFlag::eRIGID_STATIC | PxActorTypeFlag::eRIGID_DYNAMIC, TransientActors.GetData(), NumTransientActors);
	}
	
	PxHitFlags QueryFlags;	//we know our callback throws this away so no need to store it or use real data
	for (PxActor* TransientAct : TransientActors)
	{
		PxRigidActor* TransientActor = static_cast<PxRigidActor*>(TransientAct);

		const uint32 NumTransientShapes = TransientActor->getNbShapes();
		TArray<PxShape*> TransientShapes; TransientShapes.AddUninitialized(NumTransientShapes);
		TransientActor->getShapes(TransientShapes.GetData(), NumTransientShapes);
		TArray<TPair<PxShape*, ECollisionQueryHitType>> ShapeHitsArray; ShapeHitsArray.Reserve(NumTransientShapes);
		for (PxShape* TransientShape : TransientShapes)
		{
			const PxQueryHitType::Enum Result = Callback.preFilter(U2PFilterData(FilterData), TransientShape, TransientActor, QueryFlags);
			
			//We must use the non transient shape/actor so that we can replay scene queries at runtime without serializing.
			PxShape* NonTransientShape = static_cast<PxShape*>(PhysSerializer.FindObject((PxSerialObjectId)TransientShape));
			ShapeHitsArray.Emplace(NonTransientShape, P2UCollisionQueryHitType(Result));
			NonTransientToTransientShapes.Add(NonTransientShape, TransientShape);	//however, for serialization we must use the original shape/actor because conversion is done during load already
		}

		PxActor* NonTransientActor = static_cast<PxActor*>(PhysSerializer.FindObject((PxSerialObjectId)TransientActor));
		PxActorToShapeHitsArray.Add(NonTransientActor, ShapeHitsArray);
		NonTransientToTransientActors.Add(NonTransientActor, TransientActor);
	}
}


template <typename THit>
void EndCaptureHelper(PhysXInterface::FDynamicHitBuffer<THit>& Dest, const PxHitCallback<THit>& Results)
{
	Dest.block = Results.block;
	Dest.hasBlock = Results.hasBlock;

	if (Results.maxNbTouches == 0)
	{
		//we know this came from a single hit buffer because that's how UE uses the physx api.
		//since we're putting it into a dynamic hit buffer we need to push the block into the hits array
		if (Dest.hasBlock)
		{
			Dest.processTouches(&Dest.block, 1);
		}
	}
	else
	{
		//we know this came from a dynamic hit buffer because that's how UE uses the physx api. Since it's dynamic block is already in the hits buffer
		const PhysXInterface::FDynamicHitBuffer<THit>& DynamicResults = static_cast<const PhysXInterface::FDynamicHitBuffer<THit>&>(Results);
		Dest.processTouches(DynamicResults.GetHits(), DynamicResults.GetNumHits());
	}
}

void FSQCapture::EndCapturePhysXSweep(const PxHitCallback<PxSweepHit>& Results)
{
	if (IsInGameThread())
	{
		check(SQType == ESQType::Sweep);
		EndCaptureHelper(PhysXSweepBuffer, Results);
	}
}

void FSQCapture::EndCapturePhysXRaycast(const PxHitCallback<PxRaycastHit>& Results)
{
	if (IsInGameThread())
	{
		check(SQType == ESQType::Raycast);
		EndCaptureHelper(PhysXRaycastBuffer, Results);
	}
}

void FSQCapture::EndCapturePhysXOverlap(const PxHitCallback<PxOverlapHit>& Results)
{
	if (IsInGameThread())
	{
		check(SQType == ESQType::Overlap);
		EndCaptureHelper(PhysXOverlapBuffer, Results);
	}
}

template <typename THit>
void FixupBufferPointers(FPhysTestSerializer& PhysSerializer, PhysXInterface::FDynamicHitBuffer<THit>& PhysXBuffer)
{
	auto FixupPointersLambda = [&PhysSerializer](PxActorShape& Hit)
	{
		Hit.actor = static_cast<PxRigidActor*>(PhysSerializer.FindObject((PxSerialObjectId)Hit.actor));
		Hit.shape = static_cast<PxShape*>(PhysSerializer.FindObject((PxSerialObjectId)Hit.shape));
	};

	if (PhysXBuffer.hasBlock)
	{
		FixupPointersLambda(PhysXBuffer.block);
	}
	
	const int32 NumHits = PhysXBuffer.GetNumHits();
	for (int32 Idx = 0; Idx < NumHits; ++Idx)
	{
		THit& Hit = PhysXBuffer.GetHits()[Idx];
		FixupPointersLambda(Hit);
	}
}

ECollisionQueryHitType FSQCapture::GetFilterResult(const PxShape* Shape, const PxActor* Actor) const
{
	return GetFilterResultHelper(Shape, Actor, PxActorToShapeHitsArray);
}

void FSQCapture::CreatePhysXData()
{
	if (bDiskDataIsChaos || bPhysXDataReady)
	{
		return;
	}

	if (SQType != ESQType::Raycast)
	{
		AlignedDataHelper = MakeUnique<FPhysXSerializerData>(GeomData.Num());
		FMemory::Memcpy(AlignedDataHelper->Data, GeomData.GetData(), GeomData.Num());

		AlignedDataHelper->Registry = PxSerialization::createSerializationRegistry(*GPhysXSDK);
		AlignedDataHelper->Collection = PxSerialization::createCollectionFromBinary(AlignedDataHelper->Data, *AlignedDataHelper->Registry);
		if (PxBase* ColShape = AlignedDataHelper->Collection->find(ShapeCollectionID))
		{
			AlignedDataHelper->Shape = static_cast<PxShape*>(ColShape);
			PhysXGeometry = AlignedDataHelper->Shape->getGeometry();
		}
		else
		{
			AlignedDataHelper.Reset();
		}
	}

	FixupBufferPointers(PhysSerializer, PhysXRaycastBuffer);
	FixupBufferPointers(PhysSerializer, PhysXSweepBuffer);
	FixupBufferPointers(PhysSerializer, PhysXOverlapBuffer);

	FilterCallback = MakeUnique<FSQCaptureFilterCallback>(*this);

	bPhysXDataReady = true;
}

void FSQCapture::SetPhysXGeometryData(const PxGeometry& Geometry)
{
	check(AlignedDataHelper == nullptr);
	check(SQType != ESQType::Raycast);

	PxSerializationRegistry* Registry = PxSerialization::createSerializationRegistry(*GPhysXSDK);
	PxCollection* Collection = PxCreateCollection();

	//create a shape so we can serialize geometry
	PxMaterial* Material = GPhysXSDK->createMaterial(1, 1, 1);
	PxShape* Shape = GPhysXSDK->createShape(Geometry, *Material);
	Collection->add(*Shape, ShapeCollectionID);

	PxSerialization::complete(*Collection, *Registry);

	GeomData.Empty();
	FPhysXOutputStream Stream(&GeomData);
	PxSerialization::serializeCollectionToBinary(Stream, *Collection, *Registry);
	Material->release();
	Shape->release();
	Collection->release();
	Registry->release();
}

FSQCapture::FPhysXSerializerData::FPhysXSerializerData(int32 NumBytes)
	: Data(FMemory::Malloc(NumBytes, 128))
	, Shape(nullptr)
	, Collection(nullptr)
	, Registry(nullptr)
{
}

FSQCapture::FPhysXSerializerData::~FPhysXSerializerData()
{
	if (Collection)
	{
		//release all resources the collection created (calling release on the collection is not enough)
		const uint32 NumObjects = Collection->getNbObjects();
		TArray<PxBase*> Objects;
		Objects.AddUninitialized(NumObjects);
		Collection->getObjects(Objects.GetData(), NumObjects);
		for (PxBase* Obj : Objects)
		{
			Obj->release();
		}

		Collection->release();
		Registry->release();
	}
	FMemory::Free(Data);
}

#endif

#if 0
void PhysXQueryHitToChaosQueryHit(ChaosInterface::FQueryHit& ChaosHit, const PxQueryHit& PxHit, const FPhysTestSerializer& Serializer)
{
	ChaosHit.Actor = Serializer.PhysXActorToChaosHandle(PxHit.actor);
	ChaosHit.Shape = Serializer.PhysXShapeToChaosImplicit(PxHit.shape);
}

void PhysXLocationHitToChaosLocationHit(ChaosInterface::FLocationHit& ChaosHit, const PxLocationHit& PxHit, const FPhysTestSerializer& Serializer)
{
	PhysXQueryHitToChaosQueryHit(ChaosHit, PxHit, Serializer);
	ChaosHit.WorldPosition = P2UVector(PxHit.position);
	ChaosHit.WorldNormal = P2UVector(PxHit.normal);
	ChaosHit.Flags = P2UHitFlags(PxHit.flags);
	ChaosHit.Distance = PxHit.distance;
}

void PhysXHitToChaosHit(ChaosInterface::FSweepHit& ChaosHit, const PxSweepHit& PxHit, const FPhysTestSerializer& Serializer)
{
	PhysXLocationHitToChaosLocationHit(ChaosHit, PxHit, Serializer);
}

void PhysXHitToChaosHit(ChaosInterface::FOverlapHit& ChaosHit, const PxOverlapHit& PxHit, const FPhysTestSerializer& Serializer)
{
	PhysXQueryHitToChaosQueryHit(ChaosHit, PxHit, Serializer);
}

void PhysXHitToChaosHit(ChaosInterface::FRaycastHit& ChaosHit, const PxRaycastHit& PxHit, const FPhysTestSerializer& Serializer)
{
	PhysXLocationHitToChaosLocationHit(ChaosHit, PxHit, Serializer);
	ChaosHit.U = PxHit.u;
	ChaosHit.V = PxHit.v;
}

template <typename TChaosHit, typename TPhysXHit>
void PhysXToChaosBufferData(ChaosInterface::FSQHitBuffer<TChaosHit>& ChaosBuffer, const PhysXInterface::FDynamicHitBuffer<TPhysXHit>& PhysXBuffer, const FPhysTestSerializer& PhysSerializer)
{
	if (PhysXBuffer.hasBlock)
	{
		TChaosHit Hit;
		PhysXHitToChaosHit(Hit, PhysXBuffer.block, PhysSerializer);
		ChaosBuffer.SetBlockingHit(Hit);

	}
	const int32 NumHits = PhysXBuffer.GetNumHits();
	for (int32 Idx = 0; Idx < NumHits; ++Idx)
	{
		TChaosHit Hit;
		PhysXHitToChaosHit(Hit, PhysXBuffer.GetHits()[Idx], PhysSerializer);
		ChaosBuffer.AddTouchingHit(Hit);
	}
}

void FSQCapture::CreateChaosFilterResults()
{
#if WITH_PHYSX
	for (auto Itr : PxActorToShapeHitsArray)
	{
		Chaos::FGeometryParticle* Actor = PhysSerializer.PhysXActorToChaosHandle(Itr.Key);
		const auto& ShapesArray = Actor->ShapesArray();
		TArray<TPair<Chaos::FPerShapeData*, ECollisionQueryHitType>> FilterResults;

		const auto& Pairs = Itr.Value;
		int32 Idx = 0;
		for (const auto& Pair : Pairs)
		{
			FilterResults.Add(TPair<Chaos::FPerShapeData*, ECollisionQueryHitType>(ShapesArray[Idx++].Get(), Pair.Value));
		}
		ChaosActorToShapeHitsArray.Add(Actor, FilterResults);
	}
#endif
}

void FSQCapture::CreateChaosDataFromPhysX()
{
	using namespace Chaos;
	
	if (bDiskDataIsChaos || bChaosDataReady)
	{
		return;
	}

	if (AlignedDataHelper && AlignedDataHelper->Shape)
	{
		TUniquePtr<TImplicitObjectTransformed<float, 3>> TransformedObj = PxShapeToChaosGeom(AlignedDataHelper->Shape);
		//we know the dummy px shape has no transform so we want to use the inner object
		ChaosGeometry = TransformedObj->GetTransformedObject();
		ChaosOwnerObject = MoveTemp(TransformedObj);
	}

#if WITH_CHAOS
	PhysXToChaosBufferData(ChaosRaycastBuffer, PhysXRaycastBuffer, PhysSerializer);
	PhysXToChaosBufferData(ChaosSweepBuffer, PhysXSweepBuffer, PhysSerializer);
	PhysXToChaosBufferData(ChaosOverlapBuffer, PhysXOverlapBuffer, PhysSerializer);
#endif // WITH_CHAOS

	CreateChaosFilterResults();
	bChaosDataReady = true;
}
#endif
