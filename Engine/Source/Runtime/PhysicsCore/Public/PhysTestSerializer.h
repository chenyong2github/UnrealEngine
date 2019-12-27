// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PhysicsInterfaceDeclaresCore.h"

#include "Chaos/ChaosArchive.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/ParticleHandle.h"
#include "SQCapture.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/CustomVersion.h"

#ifndef PHYS_TEST_SERIALIZER
#define PHYS_TEST_SERIALIZER 1
#endif

// Utility used for serializing just physics data. This is meant for only the physics engine data (physx or chaos, not any unreal side). It is not meant to be used for actual serialization
// This utility allows for moving back and forth between physx and chaos data. This is not a proper upgrade path

#if PHYS_TEST_SERIALIZER

namespace physx
{
	class PxScene;
	class PxSerializationRegistry;
	class PxCollection;
	class PxBase;
	class PxActor;
	class PxShape;
}

namespace Chaos
{
	template <typename, int>
	class TPBDRigidsEvolutionGBF;

	class FChaosArchive;
}

class PHYSICSCORE_API FPhysTestSerializer
{
public:
	FPhysTestSerializer();
	FPhysTestSerializer(const FPhysTestSerializer& Other) = delete;
	FPhysTestSerializer(FPhysTestSerializer&& Other) = delete;
	FPhysTestSerializer& operator=(const FPhysTestSerializer&) = delete;
	FPhysTestSerializer& operator=(FPhysTestSerializer&&) = delete;

	void Serialize(Chaos::FChaosArchive& Ar);
	void Serialize(const TCHAR* FilePrefix);

	//Set the data from an external source. This will obliterate any existing data. Make sure you are not holding on to old internal data as it will go away
	void SetPhysicsData(Chaos::TPBDRigidsEvolutionGBF<float, 3>& ChaosEvolution);
	void SetPhysicsData(physx::PxScene& Scene);

	const Chaos::FChaosArchiveContext* GetChaosContext() const
	{
		return ChaosContext.Get();
	}

	FSQCapture& CaptureSQ()
	{
		ensure(!SQCapture);	//we don't have support for multi sweeps yet since the scene could change
		SQCapture = TUniquePtr<FSQCapture>(new FSQCapture(*this));
		return *SQCapture;
	}

	const FSQCapture* GetSQCapture()
	{
		if (SQCapture)
		{
			//todo: this sucks, find a better way to create data instead of doing it lazily
#if WITH_PHYSX
			GetPhysXData();
			SQCapture->CreatePhysXData();
#endif

			GetChaosData();
#if WITH_PHYSX
			SQCapture->CreateChaosDataFromPhysX();
#endif
		}
		return SQCapture.Get();
	}

	Chaos::TPBDRigidsEvolutionGBF<float, 3>* GetChaosData()
	{
		if (!bChaosDataReady)
		{
			ensure(!bDiskDataIsChaos);
			//only supported for physx to chaos - don't have serialization context
			CreateChaosData();
		}
		return ChaosEvolution.Get();
	}

#if WITH_PHYSX
	physx::PxScene* GetPhysXData()
	{
		if (!bDiskDataIsChaos)	//don't support chaos to physx
		{
			if (!AlignedDataHelper)
			{
				CreatePhysXData();
			}

			return AlignedDataHelper->PhysXScene;
		}
		return nullptr;
	}

	physx::PxBase* FindObject(uint64 Id);

	Chaos::TGeometryParticle<float,3>* PhysXActorToChaosHandle(physx::PxActor* Actor) const { return PxActorToChaosHandle.FindChecked(Actor)->GTGeometryParticle(); }
	Chaos::TPerShapeData<float,3>* PhysXShapeToChaosImplicit(physx::PxShape* Shape) const { return PxShapeToChaosShapes.FindRef(Shape); }
#endif

private:

	void CreateChaosData();
	void CreatePhysXData();

private:

	TArray<uint8> Data;	//how the data is stored before going to disk
	bool bDiskDataIsChaos;
	bool bChaosDataReady;

	TUniquePtr<FSQCapture> SQCapture;

	TUniquePtr<Chaos::TPBDRigidsEvolutionGBF<float, 3>> ChaosEvolution;
	Chaos::TPBDRigidsSOAs<float, 3> Particles;
	TArray <TUniquePtr<Chaos::TGeometryParticle<float, 3>>> GTParticles;

	TUniquePtr<Chaos::FChaosArchiveContext> ChaosContext;

	FCustomVersionContainer ArchiveVersion;

#if WITH_PHYSX
	struct FPhysXSerializerData
	{
		FPhysXSerializerData(int32 NumBytes)
			: Data(FMemory::Malloc(NumBytes, 128))
			, PhysXScene(nullptr)
			, Collection(nullptr)
			, Registry(nullptr)
		{}
		~FPhysXSerializerData();
		void* Data;
		physx::PxScene* PhysXScene;
		physx::PxCollection* Collection;
		physx::PxSerializationRegistry* Registry;
	};
	TUniquePtr<FPhysXSerializerData> AlignedDataHelper;
#endif

#if WITH_PHYSX
	TMap<physx::PxActor*, Chaos::TGeometryParticleHandle<float, 3>*> PxActorToChaosHandle;
	TMap<physx::PxShape*, Chaos::TPerShapeData<float, 3>*> PxShapeToChaosShapes;
#endif
};

#endif
