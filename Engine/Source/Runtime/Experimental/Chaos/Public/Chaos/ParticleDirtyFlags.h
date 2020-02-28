// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Chaos
{

using ChaosGeometrySp = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>;

#define PARTICLE_PROPERTY(PropName, Type) PropName,
	enum EParticleProperty : uint32
	{
#include "ParticleProperties.h"
		NumProperties
	};

#undef PARTICLE_PROPERTY
	
	// There is a dirty flag for every user-settable particle property.
	// Dirty property values will get copied from game to physics thread buffers,
	// but clean property values will get overridden with physics thread results.
#define PARTICLE_PROPERTY(PropName, Type) PropName = 1 << (uint32)EParticleProperty::PropName,

	enum class EParticleFlags : uint32
	{
		#include "ParticleProperties.h"
		DummyFlag
	};
#undef PARTICLE_PROPERTY

	class FParticleDirtyFlags
	{
	public:
		FParticleDirtyFlags() : Bits(0) { }
		~FParticleDirtyFlags() { }

		bool IsDirty() const
		{
			return Bits != 0;
		}

		bool IsDirty(const EParticleFlags CheckBits) const
		{
			return (Bits & (int32)CheckBits) != 0;
		}

		bool IsDirty(const int32 CheckBits) const
		{
			return (Bits & CheckBits) != 0;
		}

		void MarkDirty(const EParticleFlags DirtyBits)
		{
			Bits |= (int32)DirtyBits;
		}

		void MarkClean(const EParticleFlags CleanBits)
		{
			Bits &= ~(int32)CleanBits;
		}

		void Clear()
		{
			Bits = 0;
		}

		bool IsClean() const
		{
			return Bits == 0;
		}

	private:
		int32 Bits;
	};

	struct FDirtyIdx
	{
		EParticleProperty Property : 5;
		uint32 Entry : 27;

		static_assert((uint32)EParticleProperty::NumProperties <= 31,"Cannot have more than 31 properties");

		EParticleFlags GetPropertyFlag() const
		{
			return (EParticleFlags)(1 << (uint32)Property);
		}

		bool operator==(const FDirtyIdx& Rhs) const
		{
			return ((const uint32&)*this) == ((const uint32&)Rhs);
		}
	};

	template <typename T>
	class TDirtyElementPool
	{
	public:
		const T& Read(int32 Idx) const
		{
			return Elements[Idx];
		}

		void Free(int32 Idx)
		{
			Elements[Idx].~T();
			FreeIndices.Add(Idx);
		}

		int32 Write(const T& Element)
		{
			const int32 Idx = GetFree();
			Elements[Idx] = Element;
			return Idx;
		}

		int32 Write(T&& Element)
		{
			const int32 Idx = GetFree();
			Elements[Idx] = MoveTemp(Element);
			return Idx;
		}

	private:

		int32 GetFree()
		{
			return FreeIndices.Num() ? FreeIndices.Pop(/*bAllowShrinking=*/false) : Elements.AddUninitialized(1);
		}

		TArray<T> Elements;
		TArray<int32> FreeIndices;
	};

	class FDirtyPropertiesManager
	{
	public:
#define PARTICLE_PROPERTY(PropName, Type) FDirtyIdx Write##PropName(Type const & Val){ FDirtyIdx Result; Result.Property = EParticleProperty::PropName; Result.Entry = Dirty##PropName##Pool.Write(Val); return Result; };
#include "ParticleProperties.h"
#undef PARTICLE_PROPERTY

#define PARTICLE_PROPERTY(PropName, Type) Type const & Read##PropName(const FDirtyIdx Idx) const { ensure(Idx.Property == EParticleProperty::PropName); return Dirty##PropName##Pool.Read(Idx.Entry);};
#include "ParticleProperties.h"
#undef PARTICLE_PROPERTY

	void FreeProperty(FDirtyIdx Idx)
	{
		switch((uint32)Idx.Property)
		{
#define PARTICLE_PROPERTY(PropName, Type) case EParticleProperty::PropName: { Dirty##PropName##Pool.Free(Idx.Entry); break; }
#include "ParticleProperties.h"
#undef PARTICLE_PROPERTY
		default: check(false);

		}
	}
	private:

		//creates a TDirtyElementPool for each property
#define PARTICLE_PROPERTY(Property, Type) TDirtyElementPool<Type> Dirty##Property##Pool;
#include "ParticleProperties.h"
#undef PARTICLE_PROPERTY
	};

	class FDirtyProperties
	{
	public:
		bool IsDirty(const EParticleFlags CheckBits) const
		{
			return Flags.IsDirty(CheckBits);
		}

		bool IsDirty(const int32 CheckBits) const
		{
			return Flags.IsDirty(CheckBits);
		}

		void DirtyProperty(FDirtyIdx DirtyIdx)
		{
			Flags.MarkDirty(DirtyIdx.GetPropertyFlag());
			DirtyProperties.Add(DirtyIdx);
		}

		~FDirtyProperties()
		{
			//Make sure to call Clean before killing this guy
			ensure(Flags.IsClean());
		}

		void Clean(FDirtyPropertiesManager& DirtyPropertyManager)
		{
			for(const FDirtyIdx Idx : DirtyProperties)
			{
				DirtyPropertyManager.FreeProperty(Idx);
			}

			Flags.Clear();
		}

#define PARTICLE_PROPERTY(PropName, Type) Type const& Read##PropName(const FDirtyPropertiesManager& DirtyPropertyManager) const\
{\
	for(const FDirtyIdx Idx : DirtyProperties)\
	{\
		if(Idx.Property == EParticleProperty::PropName)\
		{\
			return DirtyPropertyManager.Read##PropName(Idx);\
		}\
	}\
	check(false);\
	static Type Error;\
	return Error;\
}
#include "ParticleProperties.h"
#undef PARTICLE_PROPERTY

	private:
		TArray<FDirtyIdx,TInlineAllocator<4>> DirtyProperties;
		FParticleDirtyFlags Flags;
	};
}
