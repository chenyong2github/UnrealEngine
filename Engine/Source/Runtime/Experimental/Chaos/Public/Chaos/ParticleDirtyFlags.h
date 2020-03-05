// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Core.h"
#include "Framework/PhysicsProxyBase.h"
#include "Chaos/ChaosArchive.h"

class FName;

namespace Chaos
{

using ChaosGeometrySp = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>;

#define PARTICLE_PROPERTY(PropName, Type) PropName,
	enum EParticleProperty : uint32
	{
#include "ParticleProperties.inl"
		NumProperties
	};

#undef PARTICLE_PROPERTY

#define PARTICLE_PROPERTY_TYPE(TypeName, Type) TypeName,
	enum EParticlePropertyType: uint32
	{
#include "ParticlePropertiesTypes.inl"
		NumTypes
	};

#undef PARTICLE_PROPERTY_TYPE

template <typename T>
struct TParticlePropertyTrait
{
};

#define PARTICLE_PROPERTY_TYPE(TypeName, Type) \
template <>\
struct TParticlePropertyTrait<Type>\
{\
	static constexpr uint32 PoolIdx = (uint32)EParticlePropertyType::TypeName;\
};

#include "ParticlePropertiesTypes.inl"
#undef PARTICLE_PROPERTY_TYPE
	
	// There is a dirty flag for every user-settable particle property.
	// Dirty property values will get copied from game to physics thread buffers,
	// but clean property values will get overridden with physics thread results.
#define PARTICLE_PROPERTY(PropName, Type) PropName = (uint32)1 << (uint32)EParticleProperty::PropName,

	enum class EParticleFlags : uint32
	{
		#include "ParticleProperties.inl"
		DummyFlag
	};
#undef PARTICLE_PROPERTY

	constexpr EParticleFlags ParticlePropToFlag(EParticleProperty Prop)
	{
		switch(Prop)
		{
			#define PARTICLE_PROPERTY(PropName, Type) case PropName: return EParticleFlags::PropName;
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY
		default: return (EParticleFlags)0;
		}
	}

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
		uint32 bHasEntry : 1;
		uint32 Entry : 31;
	};

	template <typename T>
	class TDirtyElementPool
	{
		static_assert(sizeof(TParticlePropertyTrait<T>::PoolIdx),"Property type must be registed. Is it in ParticlePropertiesTypes?");
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
		template <typename T>
		TDirtyElementPool<T>& GetPool()
		{
			switch(TParticlePropertyTrait<T>::PoolIdx)
			{
#define PARTICLE_PROPERTY_TYPE(TypeName, Type) case EParticlePropertyType::TypeName: return (TDirtyElementPool<T>&)TypeName##Pool;
#include "ParticlePropertiesTypes.inl"
#undef PARTICLE_PROPERTY_TYPE
			default: check(false);
			}
			
			static TDirtyElementPool<T> ErrorPool;
			return ErrorPool;
		}

		template <typename T>
		const TDirtyElementPool<T>& GetPool() const
		{
			switch(TParticlePropertyTrait<T>::PoolIdx)
			{
#define PARTICLE_PROPERTY_TYPE(TypeName, Type) case EParticlePropertyType::TypeName: return (const TDirtyElementPool<T>&)TypeName##Pool;
#include "ParticlePropertiesTypes.inl"
#undef PARTICLE_PROPERTY_TYPE
			default: check(false);
			}

			static TDirtyElementPool<T> ErrorPool;
			return ErrorPool;
		}

	private:

#define PARTICLE_PROPERTY_TYPE(TypeName, Type) TDirtyElementPool<Type> TypeName##Pool;
#include "ParticlePropertiesTypes.inl"
#undef PARTICLE_PROPERTY_TYPE
	};


template <typename T>
class TRemoteParticleProperty
{
public:
	TRemoteParticleProperty()
	{
		Idx.bHasEntry = false;
	}

	TRemoteParticleProperty(const TRemoteParticleProperty<T>& Rhs) = delete;
	TRemoteParticleProperty(TRemoteParticleProperty<T>&& Rhs)
	: Idx(Rhs.Idx)
	{
		Rhs.bHasEntry = false;
	}

	~TRemoteParticleProperty()
	{
		ensure(!Idx.bHasEntry);	//leaking, make sure to call Pop
	}

	const T& Read(const FDirtyPropertiesManager& Manager) const
	{
		ensure(Idx.bHasEntry);
		return Manager.GetPool<T>().Read(Idx.Entry);
	}

	T Pop(FDirtyPropertiesManager& Manager)
	{
		ensure(Idx.bHasEntry);
		Idx.bHasEntry = false;
		return Manager.GetPool<T>().Pop(Idx.Entry);
	}

	void Clear(FDirtyPropertiesManager& Manager)
	{
		if(Idx.bHasEntry)
		{
			Idx.bHasEntry = false;
			Manager.GetPool<T>().Pop(Idx.Entry);
		}
	}

	void Write(FDirtyPropertiesManager& Manager, const T& Val)
	{
		if(Idx.bHasEntry)
		{
			Manager.GetPool<T>().Update(Idx.Entry,Val);
		}
		else
		{
			Idx.Entry = Manager.GetPool<T>().Write(Val);
			Idx.bHasEntry = true;
		}
	}

	bool IsSet() const
	{
		return Idx.bHasEntry;
	}
private:
	FDirtyIdx Idx;

	TRemoteParticleProperty<T>& operator=(const TRemoteParticleProperty<T>& Rhs){}
};

struct FParticlePropertiesData
{
	template <typename T, EParticleProperty PropertyIdx>
	TRemoteParticleProperty<T>& GetProperty()
	{
		switch(PropertyIdx)
		{
#define PARTICLE_PROPERTY(PropName, Type) case EParticleProperty::PropName: return (TRemoteParticleProperty<T>&) PropName;
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY
		default: check(false);
		}

		static TRemoteParticleProperty<T> Error;
		return Error;
	}

	FDirtyPropertiesManager* Manager;

	~FParticlePropertiesData()
	{
		if(Manager)
		{
#define PARTICLE_PROPERTY(PropName, Type) PropName.Clear(*Manager);
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY
		}
	}

#define PARTICLE_PROPERTY(PropName, Type)\
Type const & Get##PropName() { return PropName.Read(*Manager); };\
bool Has##PropName() const { return PropName.IsSet(); };

#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY

private:
#define PARTICLE_PROPERTY(PropName, Type) TRemoteParticleProperty<Type> PropName;
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY

};

template <typename T, EParticleProperty PropName>
class TParticleProperty
{
public:
	TParticleProperty() = default;
	TParticleProperty(const T& Val)
		: Property(Val)
	{
	}

	//we don't support this because it could lead to bugs with values not being properly written to remote
	TParticleProperty(const TParticleProperty<T,PropName>& Rhs) = delete;

	const T& Read() const { return Property; }
	void Write(const T& Val, bool bInvalidate, FParticlePropertiesData* Remote, FParticleDirtyFlags& Dirty, IPhysicsProxyBase* Proxy)
	{
		Property = Val;
		WriteImp(bInvalidate, Remote, Dirty, Proxy);
	}

	template <typename Lambda>
	void Modify(bool bInvalidate,FParticlePropertiesData* Remote,FParticleDirtyFlags& Dirty,IPhysicsProxyBase* Proxy,const Lambda& LambdaFunc)
	{
		LambdaFunc(Property);
		WriteImp(bInvalidate, Remote, Dirty, Proxy);
	}

	void InitialFlush(FParticlePropertiesData& Remote,FParticleDirtyFlags& Dirty, IPhysicsProxyBase* Proxy)
	{
		if(Dirty.IsDirty(PropertyFlag))
		{
			WriteToRemote(Remote,Proxy);
		}
	}

	void Serialize(FChaosArchive& Ar)
	{
		Ar << Property;
	}

private:
	T Property;
	static constexpr EParticleFlags PropertyFlag = ParticlePropToFlag(PropName);

	void WriteToRemote(FParticlePropertiesData& Remote, IPhysicsProxyBase* Proxy)
	{
		Remote.GetProperty<T,PropName>().Write(*Remote.Manager, Property);
		if(Proxy)
		{
			if(FPhysicsSolverBase* PhysicsSolverBase = Proxy->GetSolver<FPhysicsSolverBase>())
			{
				PhysicsSolverBase->AddDirtyProxy(Proxy);
			}
		}
	}

	void WriteImp(bool bInvalidate, FParticlePropertiesData* Remote, FParticleDirtyFlags& Dirty, IPhysicsProxyBase* Proxy)
	{
		if(bInvalidate)
		{
			Dirty.MarkDirty(PropertyFlag);
			if(Remote)
			{
				WriteToRemote(*Remote,Proxy);

			}

			//for now we have to dirty proxy in here because rest of pipeline is still old
			//todo: remove this once remote data is set, writing to remote should take care of proxy dirtying
			if(Proxy)
			{
				if(FPhysicsSolverBase* PhysicsSolverBase = Proxy->GetSolver<FPhysicsSolverBase>())
				{
					PhysicsSolverBase->AddDirtyProxy(Proxy);
				}
			}
		}
	}

	//we don't support this because it could lead to bugs with values not being properly written to remote
	TParticleProperty<T,PropName>& operator=(const TParticleProperty<T,PropName>& Rhs) {}
};

template <typename T, EParticleProperty PropName>
FChaosArchive& operator<<(FChaosArchive& Ar, TParticleProperty<T, PropName>& Prop)
{
	Prop.Serialize(Ar);
	return Ar;
}
}
