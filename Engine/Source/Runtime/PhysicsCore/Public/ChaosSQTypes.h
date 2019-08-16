// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once 

#if INCLUDE_CHAOS

#include "CoreMinimal.h"
#include "PhysicsInterfaceWrapperShared.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/Declares.h"

namespace Chaos
{
	template <typename T, int d>
	class TPerShapeData;
}

namespace ChaosInterface
{

	struct FActorShape
	{
		Chaos::TGeometryParticle<float, 3>* Actor;
		const Chaos::TPerShapeData<float, 3>* Shape;
	};

	struct FQueryHit : public FActorShape
	{
		FQueryHit() : FaceIndex(-1) {}

		/**
		Face index of touched triangle, for triangle meshes, convex meshes and height fields. Defaults to -1 if face index is not available
		*/

		uint32 FaceIndex;
	};

	struct FLocationHit : public FQueryHit
	{
		FHitFlags Flags;
		FVector WorldPosition;
		FVector WorldNormal;
		float Distance;
	};

	struct FRaycastHit : public FLocationHit
	{
		float U;
		float V;
	};

	struct FOverlapHit : public FQueryHit
	{
	};

	struct FSweepHit : public FLocationHit
	{
	};

	/** Stores the results of scene queries. This can be passed around to multiple SQAccelerators and is responsible for sorting the results and pruning based on blocking.
	IncFlushCount / DecFlushCount is used to ensure any final sort / pruning operation is done when all SQAccelerators are finished. If you are passing this into multiple accelerators
	you should call IncFlushCount / DecFlushCount yourself as otherwise each accelerator will trigger its own sort / prune.
	*/
	template<typename HitType>
	class FSQHitBuffer
	{
	public:
		FSQHitBuffer(bool bSingle = false)
			: AcceleratorDepth(0)
			, bHasBlockingHit(false)
			, bSingleResult(bSingle)
		{
			if (!bSingle)
			{
				Hits.Reserve(512);
			}
		}

		virtual ~FSQHitBuffer() {}

		//Called 
		void IncFlushCount()
		{
			++AcceleratorDepth;
		}

		void DecFlushCount()
		{
			--AcceleratorDepth;
			if (AcceleratorDepth == 0)
			{
				FinishQuery();
			}
		}

		bool HasHit() const { return GetNumHits(); }
		int32 GetNumHits() const { return Hits.Num(); }
		HitType* GetHits() { return Hits.GetData(); }
		const HitType* GetHits() const { return Hits.GetData(); }

		HitType* GetBlock() { return HasBlockingHit() ? &Hits.Last() : nullptr; }
		const HitType* GetBlock() const { return HasBlockingHit() ? &Hits.Last() : nullptr; }

		bool HasBlockingHit() const { return bHasBlockingHit; }

		void SetBlockingHit(const HitType& InBlockingHit)
		{
			CurrentBlockingHit = InBlockingHit;
			bHasBlockingHit = true;
		}

		void AddTouchingHit(const HitType& InTouchingHit)
		{
			Hits.Add(InTouchingHit);
		}

		/** Does not do any distance verification. This is up to the SQ code to manage */
		void InsertHit(const HitType& Hit, bool bBlocking)
		{
			if (bBlocking)
			{
				SetBlockingHit(Hit);
			}
			else
			{
				AddTouchingHit(Hit);
			}
		}

		bool WantsSingleResult() const { return bSingleResult; }

	private:

		void FinishQuery()
		{
			//todo: sort
			//Hits.Sort();
			if (bHasBlockingHit)
			{
				//remove any touches that are past the blocking hit
				/*for (int32 HitIdx = Hits.Num() - 1; HitIdx >= 0; --HitIdx)
				{
					if(Hits[HitIdx])
				}*/
				//always put blocking hit in the back
				Hits.Add(CurrentBlockingHit);
			}
		}

		HitType CurrentBlockingHit;
		int32 AcceleratorDepth;
		bool bHasBlockingHit;
		bool bSingleResult;

		TArray<HitType> Hits;
	};

	template<typename HitType>
	class FSQSingleHitBuffer : public FSQHitBuffer<HitType>
	{
	public:
		FSQSingleHitBuffer()
			: FSQHitBuffer<HitType>(/*bSingle=*/ true)
		{}
	};

}

#endif
