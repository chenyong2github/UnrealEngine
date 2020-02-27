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
		uint32 Entry;

		bool operator==(const FDirtyIdx& Rhs) const
		{
			return Entry == Rhs.Entry;
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

		T Pop(int32 Idx)
		{
			FreeIndices.Add(Idx);
			T Result;
			Swap(Result,Elements[Idx]);
			Elements[Idx].~T();
			return Result;
		}

		int32 Write(const T& Element)
		{
			const int32 Idx = GetFree();
			Elements[Idx] = Element;
			return Idx;
		}

		void Update(int32 Entry, const T& Element)
		{
			Elements[Entry] = Element;
		}

	private:

		int32 GetFree()
		{
			//todo: can we avoid default constructors? maybe if std::is_trivially_copyable
			if(FreeIndices.Num())
			{
				int32 NewIdx = FreeIndices.Pop();
				Elements[NewIdx] = T();
				return NewIdx;
			}
			else
			{
				return Elements.AddDefaulted(1);
			}
		}

		TArray<T> Elements;
		TArray<int32> FreeIndices;
	};

	class FDirtyPropertiesManager
	{
	public:
#define PARTICLE_PROPERTY(PropName, Type) \
const TDirtyElementPool<Type>& Get##PropName##Pool() const { return Dirty##PropName##Pool; }\
TDirtyElementPool<Type>& Get##PropName##Pool() { return Dirty##PropName##Pool; }

#include "ParticleProperties.h"
#undef PARTICLE_PROPERTY
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

		~FDirtyProperties()
		{
			//Make sure to call Clean before killing this guy
			ensure(Flags.IsClean());
		}

		void Clean(FDirtyPropertiesManager& DirtyPropertyManager)
		{
			if(!Flags.IsClean())
			{
#define PARTICLE_PROPERTY(PropName, Type) if(Flags.IsDirty(EParticleFlags::PropName)){ Pop##PropName(DirtyPropertyManager); }
#include "ParticleProperties.h"
#undef PARTICLE_PROPERTY
			}
		}

#define PARTICLE_PROPERTY(PropName, Type)\
Type const& Read##PropName(const FDirtyPropertiesManager& DirtyPropertiesManager) const { return ReadImp(PropName, DirtyPropertiesManager.Get##PropName##Pool()); }\
Type Pop##PropName(FDirtyPropertiesManager& DirtyPropertiesManager) { return PopImp(PropName, EParticleFlags::PropName, DirtyPropertiesManager.Get##PropName##Pool()); }\
void Write##PropName(FDirtyPropertiesManager& DirtyPropertiesManager, Type const& Val) { WriteImp(PropName, EParticleFlags::PropName, Val, DirtyPropertiesManager.Get##PropName##Pool()); }

#include "ParticleProperties.h"
#undef PARTICLE_PROPERTY

	private:
		FParticleDirtyFlags Flags;
#define PARTICLE_PROPERTY(PropName, Type) FDirtyIdx PropName;
#include "ParticleProperties.h"
#undef PARTICLE_PROPERTY

		template <typename T>
		void WriteImp(FDirtyIdx& Prop, EParticleFlags Flag, const T& Val, TDirtyElementPool<T>& Pool)
		{
			if(Flags.IsDirty(Flag))
			{
				Pool.Update(Prop.Entry,Val);
			}
			else
			{
				Prop.Entry = Pool.Write(Val);
				Flags.MarkDirty(Flag);
			}
		}

		template <typename T>
		const T& ReadImp(const FDirtyIdx Prop, const TDirtyElementPool<T>& Pool) const
		{
			return Pool.Read(Prop.Entry);
		}

		template <typename T>
		T PopImp(const FDirtyIdx Prop, EParticleFlags Flag, TDirtyElementPool<T>& Pool)
		{
			ensure(Flags.IsDirty(Flag));
			Flags.MarkClean(Flag);
			return Pool.Pop(Prop.Entry);
		}
	};
}
