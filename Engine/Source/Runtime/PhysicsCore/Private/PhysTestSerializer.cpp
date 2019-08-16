// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Physics engine integration utilities

#include "PhysTestSerializer.h"

#if WITH_PHYSX
#include "PhysXIncludes.h"
#include "PhysXSupportCore.h"
#endif

#if INCLUDE_CHAOS
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"

using namespace Chaos;
#endif

#if WITH_PHYSX && INCLUDE_CHAOS
#include "PhysXToChaosUtil.h"
#endif

#include "PhysicsPublicCore.h"
#include "HAL/FileManager.h"

FPhysTestSerializer::FPhysTestSerializer()
	: bDiskDataIsChaos(false)
	, bChaosDataReady(false)
{
}

void FPhysTestSerializer::Serialize(const TCHAR* FilePrefix)
{
	check(IsInGameThread());
	int32 Tries = 0;
	FString UseFileName;
	do
	{
		UseFileName = FString::Printf(TEXT("%s_%d.bin"), FilePrefix, Tries++);
	} while (IFileManager::Get().FileExists(*UseFileName));

	//this is not actually file safe but oh well, very unlikely someone else is trying to create this file at the same time
	TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*UseFileName));
	if (File)
	{
		UE_LOG(LogPhysicsCore, Log, TEXT("PhysTestSerialize File: %s"), *UseFileName);
		Serialize(*File);
	}
	else
	{
		UE_LOG(LogPhysicsCore, Warning, TEXT("Could not create PhysTestSerialize file(%s)"), *UseFileName);
	}
}

void FPhysTestSerializer::Serialize(FArchive& Ar)
{
	int Version = 0;
	Ar << Version;
	Ar << bDiskDataIsChaos;
	Ar << Data;

	if (Ar.IsLoading())
	{
		CreatePhysXData();
		CreateChaosData();
	}

	bool bHasSQCapture = !!SQCapture;
	Ar << bHasSQCapture;
	if(bHasSQCapture)
	{
		if (Ar.IsLoading())
		{
			SQCapture = TUniquePtr<FSQCapture>(new FSQCapture(*this));
		}
		SQCapture->Serialize(Ar);
	}
}

void FPhysTestSerializer::SetPhysicsData(physx::PxScene& Scene)
{
#if WITH_PHYSX
	check(AlignedDataHelper == nullptr || &Scene != AlignedDataHelper->PhysXScene);

	PxSerializationRegistry* Registry = PxSerialization::createSerializationRegistry(*GPhysXSDK);
	PxCollection* Collection = PxCollectionExt::createCollection(Scene);

	PxSerialization::complete(*Collection, *Registry);

	//give an ID for every object so we can find it later. This only holds for direct objects like actors and shapes
	const uint32 NumObjects = Collection->getNbObjects();
	TArray<PxBase*> Objects;
	Objects.AddUninitialized(NumObjects);
	Collection->getObjects(Objects.GetData(), NumObjects);
	for (PxBase* Obj : Objects)
	{
		Collection->add(*Obj, (PxSerialObjectId)Obj);
	}

	Data.Empty();
	FPhysXOutputStream Stream(&Data);
	PxSerialization::serializeCollectionToBinary(Stream, *Collection, *Registry);
	Collection->release();
	Registry->release();

	bDiskDataIsChaos = false;
#endif
}

void FPhysTestSerializer::SetPhysicsData(Chaos::TPBDRigidsEvolutionGBF<float,3>& Evolution)
{
#if INCLUDE_CHAOS
	bDiskDataIsChaos = true;
#endif
}

#if WITH_PHYSX
FPhysTestSerializer::FPhysXSerializerData::~FPhysXSerializerData()
{
	if (PhysXScene)
	{
		//release all resources the collection created (calling release on the collection is not enough)
		const uint32 NumObjects = Collection->getNbObjects();
		TArray<PxBase*> Objects;
		Objects.AddUninitialized(NumObjects);
		Collection->getObjects(Objects.GetData(), NumObjects);
		for (PxBase* Obj : Objects)
		{
			if (Obj->isReleasable())
			{
				Obj->release();
			}
		}

		Collection->release();
		Registry->release();
		PhysXScene->release();
	}
	FMemory::Free(Data);
}
#endif

void FPhysTestSerializer::CreatePhysXData()
{
#if WITH_PHYSX
	check(bDiskDataIsChaos == false);	//For the moment we don't support chaos to physx direction

	{
		check(Data.Num());	//no data, was the physx scene set?
		AlignedDataHelper = MakeUnique<FPhysXSerializerData>(Data.Num());
		FMemory::Memcpy(AlignedDataHelper->Data, Data.GetData(), Data.Num());
	}
	
	PxSceneDesc Desc = CreateDummyPhysXSceneDescriptor();	//question: does it matter that this is default and not the one set by user settings?
	AlignedDataHelper->PhysXScene = GPhysXSDK->createScene(Desc);

	AlignedDataHelper->Registry = PxSerialization::createSerializationRegistry(*GPhysXSDK);
	AlignedDataHelper->Collection = PxSerialization::createCollectionFromBinary(AlignedDataHelper->Data, *AlignedDataHelper->Registry);
	AlignedDataHelper->PhysXScene->addCollection(*AlignedDataHelper->Collection);
#endif
}

#if WITH_PHYSX
physx::PxBase* FPhysTestSerializer::FindObject(uint64 Id)
{
	if (!AlignedDataHelper)
	{
		CreatePhysXData();
	}

	physx::PxBase* Ret = AlignedDataHelper->Collection->find(Id);
	ensure(Ret);
	return Ret;
}
#endif

void FPhysTestSerializer::CreateChaosData()
{
	check(bDiskDataIsChaos == false);	//For the moment we assume data is written as physx
#if INCLUDE_CHAOS && WITH_PHYSX
	if (bChaosDataReady)
	{
		return;
	}

	PxScene* Scene = GetPhysXData();
	check(Scene);

	const uint32 NumStatic = Scene->getNbActors(PxActorTypeFlag::eRIGID_STATIC);
	const uint32 NumDynamic = Scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC);
	const uint32 NumActors = NumStatic + NumDynamic;

	TArray<PxActor*> Actors;
	Actors.AddUninitialized(NumActors);
	if (NumStatic)
	{
		Scene->getActors(PxActorTypeFlag::eRIGID_STATIC, Actors.GetData(), NumStatic);
		auto NewParticles = Particles.CreateStaticParticles(NumStatic);	//question: do we want to distinguish query only and sim only actors?
		for (uint32 Idx = 0; Idx < NumStatic; ++Idx)
		{
			GTParticles.Emplace(MakeUnique<TGeometryParticle<float, 3>>());
			NewParticles[Idx]->GTGeometryParticle() = GTParticles.Last().Get();
		}
	}

	if (NumDynamic)
	{
		Scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC, &Actors[NumStatic], NumDynamic);
		auto NewParticles = Particles.CreateDynamicParticles(NumDynamic);	//question: do we want to distinguish query only and sim only actors?

		for (uint32 Idx = 0; Idx < NumDynamic; ++Idx)
		{
			GTParticles.Emplace(MakeUnique<TPBDRigidParticle<float, 3>>());
			NewParticles[Idx]->GTGeometryParticle() = GTParticles.Last().Get();
		}
	}

	auto& Handles = Particles.GetParticleHandles();
	int32 Idx = 0;
	for (PxActor* Act : Actors)
	{
		//transform
		PxRigidActor* Actor = static_cast<PxRigidActor*>(Act);
		auto& Particle = Handles.Handle(Idx);
		auto& GTParticle = Particle->GTGeometryParticle();
		Particle->X() = P2UVector(Actor->getGlobalPose().p);
		Particle->R() = P2UQuat(Actor->getGlobalPose().q);
		Particle->GTGeometryParticle()->SetX(Particle->X());
		Particle->GTGeometryParticle()->SetR(Particle->R());

		if (auto PBDRigid = Particle->AsDynamic())
		{
			PBDRigid->P() = Particle->X();
			PBDRigid->Q() = Particle->R();

			PBDRigid->GTGeometryParticle()->AsDynamic()->SetP(PBDRigid->P());
			PBDRigid->GTGeometryParticle()->AsDynamic()->SetQ(PBDRigid->R());
		}

		PxActorToChaosHandle.Add(Act, Particle.Get());

		//geometry
		TArray<TUniquePtr<TImplicitObject<float, 3>>> Geoms;
		const int32 NumShapes = Actor->getNbShapes();
		TArray<PxShape*> Shapes;
		Shapes.AddUninitialized(NumShapes);
		Actor->getShapes(Shapes.GetData(), NumShapes);
		for (PxShape* Shape : Shapes)
		{
			if (TUniquePtr<TImplicitObjectTransformed<float, 3>> Geom = PxShapeToChaosGeom(Shape))
			{
				Geoms.Add(MoveTemp(Geom));
			}
		}

		if(Geoms.Num())
		{
			if (Geoms.Num() == 1)
			{
				auto SharedGeom = TSharedPtr<TImplicitObject<float, 3>, ESPMode::ThreadSafe>(Geoms[0].Release());
				GTParticle->SetGeometry(SharedGeom);
				Particle->SetSharedGeometry(SharedGeom);
			}
			else
			{
				GTParticle->SetGeometry(MakeUnique<TImplicitObjectUnion<float, 3>>(MoveTemp(Geoms)));
				Particle->SetGeometry(GTParticle->Geometry());
			}
		}

		int32 ShapeIdx = 0;
		for (PxShape* Shape : Shapes)
		{
			PxShapeToChaosShapes.Add(Shape, GTParticle->ShapesArray()[ShapeIdx++].Get());
		}

		++Idx;
	}

	ChaosEvolution = MakeUnique<TPBDRigidsEvolutionGBF<float, 3>>(Particles);
	bChaosDataReady = true;
#endif
}
