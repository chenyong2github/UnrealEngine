// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayCollection.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Vector.h"
#include "ChaosArchive.h"
#include "HAL/LowLevelMemTracker.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#define PARTICLE_ITERATOR_RANGED_FOR_CHECK 1
#else
#define PARTICLE_ITERATOR_RANGED_FOR_CHECK 0
#endif

namespace Chaos
{
	template<class T, int d>
	class TPBDRigidsEvolution;

	enum class ERemoveParticleBehavior : uint8
	{
		RemoveAtSwap,	//O(1) but reorders particles relative to one another
		Remove			//Keeps particles relative to one another, but O(n)
	};

	template<class T, int d>
	class TParticles : public TArrayCollection
	{
	public:
		TParticles()
			: MRemoveParticleBehavior(ERemoveParticleBehavior::RemoveAtSwap)
		{
			AddArray(&MX);
#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
			MDirtyValidationCount = 0;
#endif
		}
		TParticles(const TParticles<T, d>& Other) = delete;
		TParticles(TParticles<T, d>&& Other)
		    : TArrayCollection(), MX(MoveTemp(Other.MX))
			, MRemoveParticleBehavior(Other.MRemoveParticleBehavior)
		{
			AddParticles(Other.Size());
			AddArray(&MX);
			Other.MSize = 0;
#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
			MDirtyValidationCount = 0;
#endif
		}

		TParticles(TArray<TVector<T, d>>&& Positions)
			: TArrayCollection(), MX(MoveTemp(Positions))
			, MRemoveParticleBehavior(ERemoveParticleBehavior::RemoveAtSwap)
		{
			AddParticles(MX.Num());
			AddArray(&MX);

#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
			MDirtyValidationCount = 0;
#endif
		}

		virtual ~TParticles()
		{}

		void AddParticles(const int32 Num)
		{
			LLM_SCOPE(ELLMTag::ChaosParticles);
			AddElementsHelper(Num);
			IncrementDirtyValidation();
		}

		void DestroyParticle(const int32 Idx)
		{
			LLM_SCOPE(ELLMTag::ChaosParticles);
			if (MRemoveParticleBehavior == ERemoveParticleBehavior::RemoveAtSwap)
			{
				RemoveAtSwapHelper(Idx);
			}
			else
			{
				RemoveAtHelper(Idx, 1);
			}
			IncrementDirtyValidation();
		}

		ERemoveParticleBehavior RemoveParticleBehavior() const { return MRemoveParticleBehavior; }
		ERemoveParticleBehavior& RemoveParticleBehavior() { return MRemoveParticleBehavior; }

		void MoveToOtherParticles(const int32 Idx, TParticles<T,d>& Other)
		{
			MoveToOtherArrayCollection(Idx, Other);
			IncrementDirtyValidation();
		}

		void Resize(const int32 Num)
		{
			AddParticles(Num - Size());
			IncrementDirtyValidation();
		}

		TParticles& operator=(TParticles<T, d>&& Other)
		{
			MX = MoveTemp(Other.MX);
			ResizeHelper(Other.Size());
			Other.MSize = 0;

#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
			MDirtyValidationCount = 0;
			++Other.MDirtyValidationCount;
#endif
			return *this;
		}

		inline const TArrayCollectionArray<TVector<T, d>>& X() const
		{
			return MX;
		}
		
		void Serialize(FArchive& Ar)
		{
			LLM_SCOPE(ELLMTag::ChaosParticles);
			bool bSerialize = true;	//leftover from when we had view support
			Ar << bSerialize;
			if (ensureMsgf(bSerialize, TEXT("Cannot serialize shared views. Refactor needed to reduce memory")))
			{
				Ar << MX;
				ResizeHelper(MX.Num());
			}
			IncrementDirtyValidation();
		}

		const TArrayCollectionArray<TVector<T, d>>& XArray() const
		{
			return MX;
		}
		
		const TVector<T, d>& X(const int32 Index) const
		{
			return MX[Index];
		}

		TVector<T, d>& X(const int32 Index)
		{
			return MX[Index];
		}

		FString ToString(int32 index) const
		{
			return FString::Printf(TEXT("MX:%s"), *X(index).ToString());
		}

		uint32 GetTypeHash() const
		{
			uint32 OutHash = 0;
			const int32 NumXEntries = MX.Num();

			if(NumXEntries > 0)
			{
				OutHash = ::GetTypeHash(MX[0]);

				for(int32 XIndex = 1; XIndex < NumXEntries; ++XIndex)
				{
					OutHash = HashCombine(OutHash, ::GetTypeHash(MX[XIndex]));
				}
			}

			return OutHash;
		}

#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
		int32 DirtyValidationCount() const { return MDirtyValidationCount; }
#endif

	private:
		TArrayCollectionArray<TVector<T, d>> MX;

		ERemoveParticleBehavior MRemoveParticleBehavior;

#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
		int32 MDirtyValidationCount;
#endif

		void IncrementDirtyValidation()
		{
#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
			++MDirtyValidationCount;
#endif
		}

		template<typename operator_T, int operator_d>
		friend FArchive& operator<<(FArchive& Ar, TParticles<operator_T, operator_d>& InParticles)
		{
			InParticles.Serialize(Ar);
			return Ar;
		}
	};

	template<typename T, int d>
	static uint32 GetTypeHash(const TParticles<T, d>& InParticles)
	{
		return InParticles.GetTypeHash();
	}
	
} // namespace Chaos
