// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDRigidsEvolutionFwd.h"

namespace Chaos
{
	enum class ECollisionConstraintFlags : uint32
	{
		CCF_None                       = 0x0,
		CCF_BroadPhaseIgnoreCollisions = 0x1,
		CCF_DummyFlag
	};

	class CHAOS_API FIgnoreCollisionManager
	{
	public:
		using FGeometryParticle = TGeometryParticle<FReal, 3>;
		using FHandleID = FUniqueIdx;
		using FParticleArray = TArray<FGeometryParticle*>;
		using FActiveMap = TMap<FHandleID, TArray<FHandleID> >;
		using FPendingMap = TMap<FGeometryParticle*, FParticleArray >;
		struct FStorageData
		{
			FPendingMap PendingActivations;
			FParticleArray PendingDeactivations;
		};

		FIgnoreCollisionManager()
		{
			BufferedData = FMultiBufferFactory<FStorageData>::CreateBuffer(EMultiBufferMode::Double);
		}

		bool ContainsHandle(FHandleID Body0);

		bool IgnoresCollision(FHandleID Body0, FHandleID Body1);

		int32 NumIgnoredCollision(FHandleID Body0);

		void AddIgnoreCollisionsFor(FHandleID Body0, FHandleID Body1);

		void RemoveIgnoreCollisionsFor(FHandleID Body0, FHandleID Body1);

		const FPendingMap& GetPendingActivationsForGameThread() const { return BufferedData->AccessProducerBuffer()->PendingActivations; }
		FPendingMap& GetPendingActivationsForGameThread() { return BufferedData->AccessProducerBuffer()->PendingActivations; }

		const FParticleArray& GetPendingDeactivationsForGameThread() const { return BufferedData->AccessProducerBuffer()->PendingDeactivations; }
		FParticleArray& GetPendingDeactivationsForGameThread() { return BufferedData->AccessProducerBuffer()->PendingDeactivations; }

		/*
		*
		*/
		void ProcessPendingQueues();

		/*
		*
		*/
		void FlipBufferPreSolve();

	private:
		FActiveMap IgnoreCollisionsList;

		FPendingMap PendingActivations;
		FParticleArray PendingDeactivations;
		TUniquePtr<Chaos::IBufferResource<FStorageData>> BufferedData;
	};

} // Chaos
