// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Core.h"
#include "Chaos/ChaosArchive.h"
#include "Chaos/Box.h"
#include "Chaos/Particles.h"
#include "Chaos/PhysicalMaterials.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/KinematicTargets.h"
#include "UObject/ExternalPhysicsCustomObjectVersion.h"
#include "UObject/ExternalPhysicsMaterialCustomObjectVersion.h"
#include "UObject/PhysicsObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"

class FName;

namespace Chaos
{

using FKinematicTarget = TKinematicTarget<FReal, 3>;

enum class EResimType: uint8;
enum class ESleepType: uint8;

class FParticlePositionRotation
{
public:
	void Serialize(FChaosArchive& Ar)
	{
		Ar << MX << MR;
	}

	template <typename TOther>
	void CopyFrom(const TOther& Other)
	{
		MX = Other.X();
		MR = Other.R();
	}

	template <typename TOther>
	bool IsEqual(const TOther& Other) const
	{
		return MX == Other.X() && MR == Other.R();
	}

	bool operator==(const FParticlePositionRotation& Other) const
	{
		return IsEqual(Other);
	}

	const FVec3& X() const { return MX; }
	void SetX(const FVec3& InX){ MX = InX; }

	const FRotation3& R() const { return MR; }
	void SetR(const FRotation3& InR) { MR = InR; }
	
private:
	FVec3 MX;
	FRotation3 MR;

};

inline FChaosArchive& operator<<(FChaosArchive& Ar,FParticlePositionRotation& Data)
{
	Data.Serialize(Ar);
	return Ar;
}

class FParticleVelocities
{
public:
	void Serialize(FChaosArchive& Ar)
	{
		Ar << MV << MW;
	}

	template <typename TOther>
	void CopyFrom(const TOther& Other)
	{
		MV = Other.V();
		MW = Other.W();
	}

	template <typename TOther>
	bool IsEqual(const TOther& Other) const
	{
		return MV == Other.V() && MW == Other.W();
	}

	bool operator==(const FParticleVelocities& Other) const
	{
		return IsEqual(Other);
	}

	const FVec3& V() const { return MV; }
	void SetV(const FVec3& V) { MV = V; }

	const FVec3& W() const { return MW; }
	void SetW(const FVec3& W){ MW = W; }

private:
	FVec3 MV;
	FVec3 MW;
};

inline FChaosArchive& operator<<(FChaosArchive& Ar,FParticleVelocities& Data)
{
	Data.Serialize(Ar);
	return Ar;
}

class FParticleDynamics
{
public:
	void Serialize(FChaosArchive& Ar)
	{
		Ar << MF;
		Ar << MTorque;
		Ar << MLinearImpulse;
		Ar << MAngularImpulse;	
	}

	template <typename TOther>
	void CopyFrom(const TOther& Other)
	{
		MF = Other.F();
		MTorque = Other.Torque();
		MLinearImpulse = Other.LinearImpulse();
		MAngularImpulse = Other.AngularImpulse();
	}

	template <typename TOther>
	bool IsEqual(const TOther& Other) const
	{
		return F() == Other.F()
			&& Torque() == Other.Torque()
			&& LinearImpulse() == Other.LinearImpulse()
			&& AngularImpulse() == Other.AngularImpulse();
	}

	bool operator==(const FParticleDynamics& Other) const
	{
		return IsEqual(Other);
	}

	const FVec3& F() const { return MF; }
	void SetF(const FVec3& F){ MF = F; }

	const FVec3& Torque() const { return MTorque; }
	void SetTorque(const FVec3& Torque){ MTorque = Torque; }

	const FVec3& LinearImpulse() const { return MLinearImpulse; }
	void SetLinearImpulse(const FVec3& LinearImpulse){ MLinearImpulse = LinearImpulse; }

	const FVec3& AngularImpulse() const { return MAngularImpulse; }
	void SetAngularImpulse(const FVec3& AngularImpulse){ MAngularImpulse = AngularImpulse; }

private:
	FVec3 MF;
	FVec3 MTorque;
	FVec3 MLinearImpulse;
	FVec3 MAngularImpulse;

};

inline FChaosArchive& operator<<(FChaosArchive& Ar, FParticleDynamics& Data)
{
	Data.Serialize(Ar);
	return Ar;
}

class FParticleMassProps
{
public:
	void Serialize(FChaosArchive& Ar)
	{
		Ar << MCenterOfMass;
		Ar << MRotationOfMass;
		Ar << MI;
		Ar << MInvI;
		Ar << MM;
		Ar << MInvM;
	}

	template <typename TOther>
	void CopyFrom(const TOther& Other)
	{
		MCenterOfMass = Other.CenterOfMass();
		MRotationOfMass = Other.RotationOfMass();
		MI = Other.I();
		MInvI = Other.InvI();
		MM = Other.M();
		MInvM = Other.InvM();
	}

	template <typename TOther>
	bool IsEqual(const TOther& Other) const
	{
		return CenterOfMass() == Other.CenterOfMass()
			&& RotationOfMass() == Other.RotationOfMass()
			&& I() == Other.I()
			&& InvI() == Other.InvI()
			&& M() == Other.M()
			&& InvM() == Other.InvM();
	}

	bool operator==(const FParticleMassProps& Other) const
	{
		return IsEqual(Other);
	}

	const FVec3& CenterOfMass() const { return MCenterOfMass; }
	void SetCenterOfMass(const FVec3& InCenterOfMass){ MCenterOfMass = InCenterOfMass; }

	const FRotation3& RotationOfMass() const { return MRotationOfMass; }
	void SetRotationOfMass(const FRotation3& InRotationOfMass){ MRotationOfMass = InRotationOfMass; }

	const FMatrix33& I() const { return MI; }
	void SetI(const FMatrix33& InI){ MI = InI; }

	const FMatrix33& InvI() const { return MInvI; }
	void SetInvI(const FMatrix33& InInvI){ MInvI = InInvI; }

	FReal M() const { return MM; }
	void SetM(FReal InM){ MM = InM; }

	FReal InvM() const { return MInvM; }
	void SetInvM(FReal InInvM){ MInvM = InInvM; }

private:
	FVec3 MCenterOfMass;
	FRotation3 MRotationOfMass;
	FMatrix33 MI;
	FMatrix33 MInvI;
	FReal MM;
	FReal MInvM;


};

inline FChaosArchive& operator<<(FChaosArchive& Ar,FParticleMassProps& Data)
{
	Data.Serialize(Ar);
	return Ar;
}

class FParticleDynamicMisc
{
public:
	void Serialize(FChaosArchive& Ar)
	{
		Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);
		Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);
		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);

		Ar << MLinearEtherDrag;
		Ar << MAngularEtherDrag;
		Ar << MObjectState;
		Ar << MGravityEnabled;
		Ar << MSleepType;
		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddOneWayInteraction)
		{
			Ar << MOneWayInteraction;
		}
		else
		{
			MOneWayInteraction = false;
		}

		if (Ar.CustomVer(FPhysicsObjectVersion::GUID) >= FPhysicsObjectVersion::AddCCDEnableFlag)
		{
			Ar << bCCDEnabled;
		}

		if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::AddCollisionConstraintFlag)
		{
			Ar << MCollisionConstraintFlag;
		}

		if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::AddDisabledFlag)
		{
			Ar << bDisabled;
		}
		
		if(Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::AddChaosMaxLinearAngularSpeed)
		{
			Ar << MMaxLinearSpeedSq;
			Ar << MMaxAngularSpeedSq;
		}
	}

	template <typename TOther>
	void CopyFrom(const TOther& Other)
	{
		SetLinearEtherDrag(Other.LinearEtherDrag());
		SetAngularEtherDrag(Other.AngularEtherDrag());
		SetMaxLinearSpeedSq(Other.MaxLinearSpeedSq());
		SetMaxAngularSpeedSq(Other.MaxAngularSpeedSq());
		SetObjectState(Other.ObjectState());
		SetGravityEnabled(Other.GravityEnabled());
		SetCollisionGroup(Other.CollisionGroup());
		SetResimType(Other.ResimType());
		SetSleepType(Other.SleepType());
		SetOneWayInteraction(Other.OneWayInteraction());
		SetCollisionConstraintFlag(Other.CollisionConstraintFlag());
		SetCCDEnabled(Other.CCDEnabled());
		SetDisabled(Other.Disabled());
	}

	template <typename TOther>
	bool IsEqual(const TOther& Other) const
	{
		return ObjectState() == Other.ObjectState()
			&& LinearEtherDrag() == Other.LinearEtherDrag()
			&& AngularEtherDrag() == Other.AngularEtherDrag()
			&& MaxLinearSpeedSq() == Other.MaxLinearSpeedSq()
			&& MaxAngularSpeedSq() == Other.MaxAngularSpeedSq()
			&& GravityEnabled() == Other.GravityEnabled()
			&& CollisionGroup() == Other.CollisionGroup()
			&& ResimType() == Other.ResimType()
			&& SleepType() == Other.SleepType()
			&& OneWayInteraction() == Other.OneWayInteraction() 
			&& CollisionConstraintFlag() == Other.CollisionConstraintFlag()
			&& CCDEnabled() == Other.CCDEnabled()
			&& Disabled() == Other.Disabled();
	}

	bool operator==(const FParticleDynamicMisc& Other) const
	{
		return IsEqual(Other);
	}

	FReal LinearEtherDrag() const { return MLinearEtherDrag; }
	void SetLinearEtherDrag(FReal InLinearEtherDrag) { MLinearEtherDrag = InLinearEtherDrag; }

	FReal AngularEtherDrag() const { return MAngularEtherDrag; }
	void SetAngularEtherDrag(FReal InAngularEtherDrag) { MAngularEtherDrag = InAngularEtherDrag; }

	FReal MaxLinearSpeedSq() const { return MMaxLinearSpeedSq; }
	void SetMaxLinearSpeedSq(FReal InMaxLinearSpeed) { MMaxLinearSpeedSq = InMaxLinearSpeed; }

	FReal MaxAngularSpeedSq() const { return MMaxAngularSpeedSq; }
	void SetMaxAngularSpeedSq(FReal InMaxAngularSpeed) { MMaxAngularSpeedSq = InMaxAngularSpeed; }

	EObjectStateType ObjectState() const { return MObjectState; }
	void SetObjectState(EObjectStateType InState){ MObjectState = InState; }

	bool GravityEnabled() const { return MGravityEnabled; }
	void SetGravityEnabled(bool InGravity){ MGravityEnabled = InGravity; }

	bool CCDEnabled() const { return bCCDEnabled; }
	void SetCCDEnabled(bool bInCCDEnabled) { bCCDEnabled = bInCCDEnabled; }

	bool Disabled() const { return bDisabled; }
	void SetDisabled(bool bInDisabled) { bDisabled = bInDisabled; }

	int32 CollisionGroup() const { return MCollisionGroup; }
	void SetCollisionGroup(int32 InGroup){ MCollisionGroup = InGroup; }

	EResimType ResimType() const { return MResimType; }
	void SetResimType(EResimType Type) { MResimType = Type; }

	ESleepType SleepType() const { return MSleepType; }
	void SetSleepType(ESleepType Type) { MSleepType = Type; }

	uint32 CollisionConstraintFlag() const { return MCollisionConstraintFlag; }
	void SetCollisionConstraintFlag(uint32 InCollisionConstraintFlag) { MCollisionConstraintFlag = InCollisionConstraintFlag; }
	bool OneWayInteraction() const { return MOneWayInteraction; }
	void SetOneWayInteraction(bool InOneWayInteraction) { MOneWayInteraction = InOneWayInteraction; }

private:
	//NOTE: MObjectState is the only sim-writable data in this struct
	//If you add any more, make sure to update SyncSimWritablePropsFromSim
	//Or consider breaking it (and object state) out of this struct entirely
	FReal MLinearEtherDrag;
	FReal MAngularEtherDrag;
	FReal MMaxLinearSpeedSq;
	FReal MMaxAngularSpeedSq;
	int32 MCollisionGroup;

	EObjectStateType MObjectState;
	EResimType MResimType;
	ESleepType MSleepType;

	bool MGravityEnabled;
	bool MOneWayInteraction = false;
	uint32 MCollisionConstraintFlag = 0;

	bool bCCDEnabled;
	bool bDisabled;
};

inline FChaosArchive& operator<<(FChaosArchive& Ar,FParticleDynamicMisc& Data)
{
	Data.Serialize(Ar);
	return Ar;
}

class FParticleNonFrequentData
{
public:
	FParticleNonFrequentData()
	{

	}

	void Serialize(FChaosArchive& Ar)
	{
		Ar.SerializeConstPtr(MGeometry);
	}

	template <typename TOther>
	void CopyFrom(const TOther& Other)
	{
		SetGeometry(Other.SharedGeometryLowLevel());
		SetUniqueIdx(Other.UniqueIdx());
		SetSpatialIdx(Other.SpatialIdx());
#if CHAOS_CHECKED
		SetDebugName(Other.DebugName());
#endif
	}

	template <typename TOther>
	bool IsEqual(const TOther& Other) const
	{
		return Geometry() == Other.Geometry()
			&& UniqueIdx() == Other.UniqueIdx()
			&& SpatialIdx() == Other.SpatialIdx()
#if CHAOS_CHECKED
			&& DebugName() == Other.DebugName()
#endif
			;
	}

	bool operator==(const FParticleNonFrequentData& Other) const
	{
		return IsEqual(Other);
	}

	//This function is not thread safe, don't use it (geometry is shared across threads). Leaving here for existing code, but needs to be removed
	FImplicitObject* AccessGeometryDangerous() const { return const_cast<FImplicitObject*>(MGeometry.Get()); }

	TSerializablePtr<FImplicitObject> Geometry() const { return TSerializablePtr<const FImplicitObject>(MGeometry);}
	const TSharedPtr<const FImplicitObject,ESPMode::ThreadSafe>& SharedGeometryLowLevel() const { return MGeometry;}
	void SetGeometry(const TSharedPtr<const FImplicitObject,ESPMode::ThreadSafe>& InGeometry) { MGeometry = InGeometry;}

	const FUniqueIdx& UniqueIdx() const { return MUniqueIdx; }
	void SetUniqueIdx(FUniqueIdx InIdx){ MUniqueIdx = InIdx; }

	FSpatialAccelerationIdx SpatialIdx() const { return MSpatialIdx; }
	void SetSpatialIdx(FSpatialAccelerationIdx InIdx){ MSpatialIdx = InIdx; }

#if CHAOS_CHECKED
	FName DebugName() const { return MDebugName; }
	void SetDebugName(FName InName) { MDebugName = InName; }
#endif
private:
	TSharedPtr<const FImplicitObject,ESPMode::ThreadSafe> MGeometry;
	FUniqueIdx MUniqueIdx;
	FSpatialAccelerationIdx MSpatialIdx;

#if CHAOS_CHECKED
	FName MDebugName;
#endif
};

inline FChaosArchive& operator<<(FChaosArchive& Ar,FParticleNonFrequentData& Data)
{
	Data.Serialize(Ar);
	return Ar;
}

struct FCollisionData
{
	FCollisionFilterData QueryData;
	FCollisionFilterData SimData;
	void* UserData;
	EChaosCollisionTraceFlag CollisionTraceType;
	uint8 bSimCollision : 1;
	uint8 bQueryCollision : 1;

	FCollisionData()
	: UserData(nullptr)
	, CollisionTraceType(EChaosCollisionTraceFlag::Chaos_CTF_UseDefault)
	, bSimCollision(true)
	, bQueryCollision(true)
	{
	}

	bool HasCollisionData() const { return bSimCollision || bQueryCollision; }

	void Serialize(FChaosArchive& Ar)
	{
		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		Ar.UsingCustomVersion(FExternalPhysicsMaterialCustomObjectVersion::GUID);

		Ar << QueryData;
		Ar << SimData;

		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddShapeSimAndQueryCollisionEnabled)
		{
			int8 EnableSim = bSimCollision;
			int8 EnableQuery = bQueryCollision;
			Ar << EnableSim;
			Ar << EnableQuery;
			bSimCollision = EnableSim;
			bQueryCollision = EnableQuery;
		}
		else if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddShapeCollisionDisable)
		{
			bool Disable = !bSimCollision;
			Ar << Disable;
			bSimCollision = !Disable;
		}

		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::SerializePerShapeDataSimulateFlag &&
			Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::RemoveShapeSimAndQueryDuplicateRepresentations)
		{
			bool Simulate = bSimCollision;
			Ar << Simulate;
			bSimCollision = Simulate;
		}

		if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::SerializeCollisionTraceType)
		{
			int32 Data = (int32)CollisionTraceType;
			Ar << Data;
			CollisionTraceType = (EChaosCollisionTraceFlag)Data;
		}
	}
};

inline FChaosArchive& operator<<(FChaosArchive& Ar,FCollisionData& Data)
{
	//TODO: should this only work with dirty flag? Not sure if this path really matters at this point
	Data.Serialize(Ar);
	return Ar;
}

struct FMaterialData
{
	TArray<FMaterialHandle> Materials;
	TArray<FMaterialMaskHandle> MaterialMasks;
	TArray<uint32> MaterialMaskMaps;
	TArray<FMaterialHandle> MaterialMaskMapMaterials;

	void Serialize(FChaosArchive& Ar)
	{
		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		Ar.UsingCustomVersion(FExternalPhysicsMaterialCustomObjectVersion::GUID);

		if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddedMaterialManager)
		{
			Ar << Materials;
		}

		if(Ar.CustomVer(FExternalPhysicsMaterialCustomObjectVersion::GUID) >= FExternalPhysicsMaterialCustomObjectVersion::AddedMaterialMasks)
		{
			Ar << MaterialMasks << MaterialMaskMaps << MaterialMaskMapMaterials;
		}
	}
};

inline FChaosArchive& operator<<(FChaosArchive& Ar,FMaterialData& Data)
{
	//TODO: should this only work with dirty flag? Not sure if this path really matters at this point
	Data.Serialize(Ar);
	return Ar;
}

#define PARTICLE_PROPERTY(PropName, Type) PropName,
	enum class EParticleProperty : uint32
	{
#include "ParticleProperties.inl"
		NumProperties
	};

#undef PARTICLE_PROPERTY

#define PROPERTY_TYPE(TypeName, Type) TypeName,
	enum class EPropertyType: uint32
	{
#include "PropertiesTypes.inl"
		NumTypes
	};

#undef PROPERTY_TYPE

template <typename T>
struct TPropertyTypeTrait
{
};

#define PROPERTY_TYPE(TypeName, Type) \
template <>\
struct TPropertyTypeTrait<Type>\
{\
	static constexpr EPropertyType PoolIdx = EPropertyType::TypeName;\
};

#include "PropertiesTypes.inl"
#undef PROPERTY_TYPE

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
			#define PARTICLE_PROPERTY(PropName, Type) case EParticleProperty::PropName: return EParticleFlags::PropName;
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY
		default: return (EParticleFlags)0;
		}
	}


#define SHAPE_PROPERTY(PropName, Type) PropName,
	enum class EShapeProperty: uint32
	{
#include "ShapeProperties.inl"
		NumShapeProperties
	};

#undef SHAPE_PROPERTY

#define SHAPE_PROPERTY(PropName, Type) PropName = (uint32)1 << (uint32)EShapeProperty::PropName,

	enum class EShapeFlags: uint32
	{
#include "ShapeProperties.inl"
		DummyFlag
	};
#undef SHAPE_PROPERTY

	constexpr EShapeFlags ShapePropToFlag(EShapeProperty Prop)
	{
		switch(Prop)
		{
#define SHAPE_PROPERTY(PropName, Type) case EShapeProperty::PropName: return EShapeFlags::PropName;
#include "ShapeProperties.inl"
#undef SHAPE_PROPERTY
		default: return (EShapeFlags)0;
		}
	}

	template <typename FlagsType>
	class TDirtyFlags
	{
	public:
		TDirtyFlags() : Bits(0) { }

		bool IsDirty() const
		{
			return Bits != 0;
		}

		bool IsDirty(const FlagsType CheckBits) const
		{
			return (Bits & (int32)CheckBits) != 0;
		}

		bool IsDirty(const int32 CheckBits) const
		{
			return (Bits & CheckBits) != 0;
		}

		void MarkDirty(const FlagsType DirtyBits)
		{
			Bits |= (int32)DirtyBits;
		}

		void MarkClean(const FlagsType CleanBits)
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

	using FParticleDirtyFlags = TDirtyFlags<EParticleFlags>;
	using FShapeDirtyFlags = TDirtyFlags<EShapeFlags>;

	struct FDirtyIdx
	{
		uint32 bHasEntry : 1;
		uint32 Entry : 31;
	};

	template <typename T>
	class TDirtyElementPool
	{
		static_assert(sizeof(TPropertyTypeTrait<T>::PoolIdx),"Property type must be registered. Is it in PropertiesTypes.inl?");
	public:
		const T& GetElement(int32 Idx) const { return Elements[Idx]; }
		T& GetElement(int32 Idx){ return Elements[Idx]; }
		
		void Reset(int32 Idx)
		{
			Elements[Idx] = T();
		}

		void SetNum(int32 Num)
		{
			Elements.SetNum(Num);
		}

		int32 Num() const
		{
			return Elements.Num();
		}

	private:

		TArray<T> Elements;
	};

	//want this for sparse representation
#if 0
	template <typename T>
	class TDirtyElementPool
	{
		static_assert(sizeof(TPropertyTypeTrait<T>::PoolIdx),"Property type must be registered. Is it in PropertiesTypes.inl?");
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
				int32 NewIdx = FreeIndices.Pop(/*bAllowShrinking=*/false);
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

class FDirtyPropertiesManager;

template <typename T>
class TRemoteProperty
{
public:
	TRemoteProperty()
	{
		Idx.bHasEntry = false;
	}

	TRemoteProperty(const TRemoteProperty<T>& Rhs) = delete;
	TRemoteProperty(TRemoteProperty<T>&& Rhs)
	: Idx(Rhs.Idx)
	{
		Rhs.bHasEntry = false;
	}

	~TRemoteProperty()
	{
		ensure(!Idx.bHasEntry);	//leaking, make sure to call Pop
	}

	const T& Read(const FDirtyPropertiesManager& Manager) const;
	void Clear(FDirtyPropertiesManager& Manager);
	void Write(FDirtyPropertiesManager& Manager,const T& Val);
	
	bool IsSet() const
	{
		return Idx.bHasEntry;
	}
private:
	FDirtyIdx Idx;

	TRemoteProperty<T>& operator=(const TRemoteProperty<T>& Rhs){}
};

struct FParticlePropertiesData
{
	FParticlePropertiesData(FDirtyPropertiesManager* InManager = nullptr)
		: Manager(InManager)
	{
	}

	template <typename T, EParticleProperty PropertyIdx>
	TRemoteProperty<T>& GetProperty()
	{
		switch(PropertyIdx)
		{
#define PARTICLE_PROPERTY(PropName, Type) case EParticleProperty::PropName: return (TRemoteProperty<T>&) PropName;
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY
		default: check(false);
		}

		static TRemoteProperty<T> Error;
		return Error;
	}

	void Clear()
	{
		if(Manager)
		{
#define PARTICLE_PROPERTY(PropName, Type) PropName.Clear(*Manager);
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY
		}
	}

	inline void FreeToManager();
	
	~FParticlePropertiesData()
	{
		Clear();
	}

	FDirtyPropertiesManager* GetManager(){ return Manager; }
	const FDirtyPropertiesManager* GetManager() const { return Manager; }

#define PARTICLE_PROPERTY(PropName, Type)\
Type const & Get##PropName() const { return PropName.Read(*Manager); }\
bool Has##PropName() const { return PropName.IsSet(); }\
Type const * Find##PropName() const { return Has##PropName() ? &Get##PropName() : nullptr; }

#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY

private:
#define PARTICLE_PROPERTY(PropName, Type) TRemoteProperty<Type> PropName;
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY

	FDirtyPropertiesManager* Manager;

};

struct FShapePropertiesData
{
	FShapePropertiesData(FDirtyPropertiesManager* InManager)
		: Manager(InManager)
	{

	}
	template <typename T,EShapeProperty PropertyIdx>
	TRemoteProperty<T>& GetProperty()
	{
		switch(PropertyIdx)
		{
#define SHAPE_PROPERTY(PropName, Type) case EShapeProperty::PropName: return (TRemoteProperty<T>&) PropName;
#include "ShapeProperties.inl"
#undef SHAPE_PROPERTY
		default: check(false);
		}

		static TRemoteProperty<T> Error;
		return Error;
	}

	void Clear()
	{
		if(Manager)
		{
#define SHAPE_PROPERTY(PropName, Type) PropName.Clear(*Manager);
#include "ShapeProperties.inl"
#undef SHAPE_PROPERTY
		}
	}

	~FShapePropertiesData()
	{
		Clear();
	}

	inline void FreeToManager();

	FDirtyPropertiesManager* GetManager(){ return Manager; }
	const FDirtyPropertiesManager* GetManager() const { return Manager; }

#define SHAPE_PROPERTY(PropName, Type)\
Type const & Get##PropName() const { return PropName.Read(*Manager); }\
bool Has##PropName() const { return PropName.IsSet(); }\
Type const * Find##PropName() const { return Has##PropName() ? &Get##PropName() : nullptr; }

#include "ShapeProperties.inl"
#undef SHAPE_PROPERTY

private:
#define SHAPE_PROPERTY(PropName, Type) TRemoteProperty<Type> PropName;
#include "ShapeProperties.inl"
#undef SHAPE_PROPERTY

	FDirtyPropertiesManager* Manager;
};

class FPerShapeData;

class CHAOS_API FShapeRemoteDataContainer
{
public:
	FShapeRemoteDataContainer(FDirtyPropertiesManager* InManager)
	: Manager(InManager)
	{
	}

	~FShapeRemoteDataContainer()
	{
		Clear();
	}

	void SyncShapes(TArray<TUniquePtr<FPerShapeData>, TInlineAllocator<1>>& Shapes);
	
	void DetachRemoteData(TArray<TUniquePtr<FPerShapeData>,TInlineAllocator<1>>& Shapes);

	inline void FreeToManager();

	inline FShapePropertiesData* NewRemoteShapeProperties();
	
	void Clear()
	{
		//todo: avoid iterating all remote data regardless of if dirty or not
		for(FShapePropertiesData* RemoteData : RemoteDatas)
		{
			if(RemoteData)
			{
				RemoteData->FreeToManager();
			}
		}

		RemoteDatas.Reset();
	}

	const auto& GetRemoteDatas() const
	{
		return RemoteDatas;
	}

	auto& GetRemoteDatas()
	{
		return RemoteDatas;
	}
	
private:

	FDirtyPropertiesManager* Manager;
	TArray<FShapePropertiesData*,TInlineAllocator<4>> RemoteDatas;
};
#endif

class FDirtyPropertiesManager
{
public:

#if 0
	FParticlePropertiesData* NewRemoteParticleProperties()
	{
		return RemoteParticlePool.NewEntry(this);
	}

	void FreeRemoteParticleProperties(FParticlePropertiesData* Entry)
	{
		RemoteParticlePool.FreeEntry(Entry);
	}

	FShapePropertiesData* NewRemoteShapeProperties()
	{
		return RemoteShapePool.NewEntry(this);
	}

	void FreeRemoteShapeProperties(FShapePropertiesData* Entry)
	{
		RemoteShapePool.FreeEntry(Entry);
	}

	FShapeRemoteDataContainer* NewRemoteShapeContainer()
	{
		return RemoteShapeContainerPool.NewEntry(this);
	}

	void FreeRemoteShapeContainer(FShapeRemoteDataContainer* Entry)
	{
		RemoteShapeContainerPool.FreeEntry(Entry);
	}
#endif

	void SetNumParticles(int32 NumParticles)
	{
#define PARTICLE_PROPERTY(PropName, Type) PropName##Pool.SetNum(NumParticles);
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY
	}

	int32 GetNumParticles() const
	{
		//assume this property exists, if it gets renamed just pick any property
		return XRPool.Num();
	}

	void SetNumShapes(int32 NumShapes)
	{
#define SHAPE_PROPERTY(PropName, Type) PropName##ShapePool.SetNum(NumShapes);
#include "ShapeProperties.inl"
#undef SHAPE_PROPERTY
	}

	template <typename T, EParticleProperty PropName>
	TDirtyElementPool<T>& GetParticlePool()
	{
		switch(PropName)
		{
#define PARTICLE_PROPERTY(PropName, Type) case EParticleProperty::PropName: return (TDirtyElementPool<T>&)PropName##Pool;
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY
		default: check(false);
		}

		static TDirtyElementPool<T> ErrorPool;
		return ErrorPool;
	}

	template <typename T,EParticleProperty PropName>
	const TDirtyElementPool<T>& GetParticlePool() const
	{
		switch(PropName)
		{
#define PARTICLE_PROPERTY(PropName, Type) case EParticleProperty::PropName: return (TDirtyElementPool<T>&)PropName##Pool;
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY
		default: check(false);
		}

		static TDirtyElementPool<T> ErrorPool;
		return ErrorPool;
	}

	template <typename T,EShapeProperty PropName>
	TDirtyElementPool<T>& GetShapePool()
	{
		switch(PropName)
		{
#define SHAPE_PROPERTY(PropName, Type) case EShapeProperty::PropName: return (TDirtyElementPool<T>&)PropName##ShapePool;
#include "ShapeProperties.inl"
#undef SHAPE_PROPERTY
		default: check(false);
		}

		static TDirtyElementPool<T> ErrorPool;
		return ErrorPool;
	}

	template <typename T,EShapeProperty PropName>
	const TDirtyElementPool<T>& GetShapePool() const
	{
		switch(PropName)
		{
#define SHAPE_PROPERTY(PropName, Type) case EShapeProperty::PropName: return (TDirtyElementPool<T>&)PropName##ShapePool;
#include "ShapeProperties.inl"
#undef SHAPE_PROPERTY
		default: check(false);
		}

		static TDirtyElementPool<T> ErrorPool;
		return ErrorPool;
	}

private:

#define PARTICLE_PROPERTY(PropName, Type) TDirtyElementPool<Type> PropName##Pool;
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY

#define SHAPE_PROPERTY(PropName, Type) TDirtyElementPool<Type> PropName##ShapePool;
#include "ShapeProperties.inl"
#undef SHAPE_PROPERTY

#if 0
	template <typename T>
	class TRemotePropertiesPool
	{
	public:
		T* NewEntry(FDirtyPropertiesManager* Manager)
		{
			if(Pool.Num())
			{
				return Pool.Pop(/*bAllowShrinking=*/false);
			} else
			{
				return new T(Manager);
			}
		}

		void FreeEntry(T* Entry)
		{
			Entry->Clear();
			Pool.Add(Entry);
		}

		~TRemotePropertiesPool()
		{
			for(T* Entry : Pool)
			{
				delete Entry;
			}
		}

		int32 NumInPool() const { return Pool.Num(); }

	private:
		TArray<T*> Pool;
	};

	TRemotePropertiesPool<FParticlePropertiesData> RemoteParticlePool;
	TRemotePropertiesPool<FShapePropertiesData> RemoteShapePool;
	TRemotePropertiesPool<FShapeRemoteDataContainer> RemoteShapeContainerPool;
#endif

};

class FParticleDirtyData
{
public:
	
	void SetParticleBufferType(EParticleType Type)
	{
		ParticleBufferType = Type;
	}

	EParticleType GetParticleBufferType() const
	{
		return ParticleBufferType;
	}

	void SetFlags(FParticleDirtyFlags InFlags)
	{
		Flags = InFlags;
	}

	FParticleDirtyFlags GetFlags() const
	{
		return Flags;
	}

	void DirtyFlag(EParticleFlags Flag)
	{
		Flags.MarkDirty(Flag);
	}

	template <typename T, EParticleProperty PropName>
	void SyncRemote(FDirtyPropertiesManager& Manager, int32 Idx, const T& Val) const
	{
		if(Flags.IsDirty(ParticlePropToFlag(PropName)))
		{
			Manager.GetParticlePool<T,PropName>().GetElement(Idx) = Val;
		}
	}

	void Clear(FDirtyPropertiesManager& Manager, int32 Idx)
	{
#define PARTICLE_PROPERTY(PropName, Type) ClearHelper<Type, EParticleProperty::PropName>(Manager, Idx);
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY
		Flags.Clear();
	}

	bool IsDirty(EParticleFlags InBits) const
	{
		return Flags.IsDirty(InBits);
	}

#define PARTICLE_PROPERTY(PropName, Type)\
Type const & Get##PropName(const FDirtyPropertiesManager& Manager, int32 Idx) const { return ReadImp<Type, EParticleProperty::PropName>(Manager, Idx); }\
bool Has##PropName() const { return Flags.IsDirty(ParticlePropToFlag(EParticleProperty::PropName)); }\
Type const * Find##PropName(const FDirtyPropertiesManager& Manager, int32 Idx) const { return Has##PropName() ? &Get##PropName(Manager, Idx) : nullptr; }

#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY

private:
	FParticleDirtyFlags Flags;
	EParticleType ParticleBufferType;

	template <typename T,EParticleProperty PropName>
	const T& ReadImp(const FDirtyPropertiesManager& Manager, int32 Idx) const
	{
		ensure(Flags.IsDirty(ParticlePropToFlag(PropName)));
		return Manager.GetParticlePool<T,PropName>().GetElement(Idx);
	}

	template <typename T, EParticleProperty PropName>
	void ClearHelper(FDirtyPropertiesManager& Manager, int32 Idx)
	{
		if(Flags.IsDirty(ParticlePropToFlag(PropName)))
		{
			Manager.GetParticlePool<T, PropName>().Reset(Idx);
		}
	}
};

class FShapeDirtyData
{
public:

	FShapeDirtyData(int32 InShapeIdx)
	: ShapeIdx(InShapeIdx)
	{

	}

	int32 GetShapeIdx() const { return ShapeIdx; }

	void SetFlags(FShapeDirtyFlags InFlags)
	{
		Flags = InFlags;
	}

	template <typename T,EShapeProperty PropName>
	void SyncRemote(FDirtyPropertiesManager& Manager,int32 Idx, const T& Val) const
	{
		if(Flags.IsDirty(ShapePropToFlag(PropName)))
		{
			Manager.GetShapePool<T,PropName>().GetElement(Idx) = Val;
		}
	}

	template <EShapeProperty PropName>
	bool IsDirty() const
	{
		return Flags.IsDirty(ShapePropToFlag(PropName));
	}

	void Clear(FDirtyPropertiesManager& Manager, int32 Idx)
	{
#define SHAPE_PROPERTY(PropName, Type) ClearHelper<Type, EShapeProperty::PropName>(Manager, Idx);
#include "ShapeProperties.inl"
#undef SHAPE_PROPERTY
		Flags.Clear();
	}

#define SHAPE_PROPERTY(PropName, Type)\
Type const & Get##PropName(const FDirtyPropertiesManager& Manager, int32 Idx) const { return ReadImp<Type, EShapeProperty::PropName>(Manager, Idx); }\
bool Has##PropName() const { return Flags.IsDirty(ShapePropToFlag(EShapeProperty::PropName)); }\
Type const * Find##PropName(const FDirtyPropertiesManager& Manager, int32 Idx) const { return Has##PropName() ? &Get##PropName(Manager, Idx) : nullptr; }

#include "ShapeProperties.inl"
#undef PARTICLE_PROPERTY

private:
	int32 ShapeIdx;
	FShapeDirtyFlags Flags;

	template <typename T,EShapeProperty PropName>
	const T& ReadImp(const FDirtyPropertiesManager& Manager, int32 Idx) const
	{
		ensure(Flags.IsDirty(ShapePropToFlag(PropName)));
		return Manager.GetShapePool<T,PropName>().GetElement(Idx);
	}

	template <typename T,EShapeProperty PropName>
	void ClearHelper(FDirtyPropertiesManager& Manager, int32 Idx)
	{
		if(Flags.IsDirty(ShapePropToFlag(PropName)))
		{
			Manager.GetShapePool<T,PropName>().Reset(Idx);
		}
	}
};

#if 0
void FParticlePropertiesData::FreeToManager()
{
	if(Manager)
	{
		Manager->FreeRemoteParticleProperties(this);
	}
}

void FShapePropertiesData::FreeToManager()
{
	if(Manager)
	{
		Manager->FreeRemoteShapeProperties(this);
	}
}

void FShapeRemoteDataContainer::FreeToManager()
{
	if(Manager)
	{
		Manager->FreeRemoteShapeContainer(this);
	}
}

FShapePropertiesData* FShapeRemoteDataContainer::NewRemoteShapeProperties()
{
	return Manager ? Manager->NewRemoteShapeProperties() : nullptr;
}

template <typename T>
const T& TRemoteProperty<T>::Read(const FDirtyPropertiesManager& Manager) const
{
	ensure(Idx.bHasEntry);
	return Manager.GetPool<T>().Read(Idx.Entry);
}

template <typename T>
void TRemoteProperty<T>::Clear(FDirtyPropertiesManager& Manager)
{
	if(Idx.bHasEntry)
	{
		Idx.bHasEntry = false;
		Manager.GetPool<T>().Pop(Idx.Entry);
	}
}

template <typename T>
void TRemoteProperty<T>::Write(FDirtyPropertiesManager& Manager,const T& Val)
{
	if(Idx.bHasEntry)
	{
		Manager.GetPool<T>().Update(Idx.Entry,Val);
	} else
	{
		Idx.Entry = Manager.GetPool<T>().Write(Val);
		Idx.bHasEntry = true;
	}
}
#endif

template <typename T>
class TPropertyPool;

template <typename T>
class TPropertyRef
{
public:
	TPropertyRef() = default;
	TPropertyRef(TPropertyRef<T>&& Other)
	: IdxPlusOne(Other.IdxPlusOne)
	{
		Other.IdxPlusOne = 0;
	}
	TPropertyRef(const TPropertyRef<T>& Other) = delete;	//use AddRef on TPropertyPool

	~TPropertyRef() { ensure(!IsSet()); }	//use ReleaseRef on TPropertyPool before destructor is called

	const bool IsSet() const { return IdxPlusOne != 0; }
	const int32 GetIdx() const { return IdxPlusOne - 1; }

	void SetRefFrom(const TPropertyRef<T>& Other, TPropertyPool<T>& Pool);
private:

	void SetIdx(const int32 InIdx)
	{
		IdxPlusOne = InIdx + 1;
	}

	int32 IdxPlusOne = 0;	//use 0 so that we can treat zeroed entries as invalid (Element is in Elements[IdxPlusOne-1])

	template <typename R>
	friend class TPropertyPool;

	TPropertyRef(const int32 InIdx)
	: IdxPlusOne(InIdx+1)
	{}

};


template <typename T>
class TPropertyPool
{
	static_assert(sizeof(TPropertyTypeTrait<T>::PoolIdx), "Property type must be registered. Is it in PropertiesTypes.inl?");
public:

	void AddElement(const T& Val, TPropertyRef<T>& OutRef)
	{
		//About to lose reference so make sure it's released (if set)
		if(OutRef.IsSet())
		{
			DecRef(OutRef);
		}

		if(FreeList.Num())
		{
			const int32 Idx = FreeList.Pop();
			Elements[Idx] = FPropertyAndCount(Val);
			OutRef.SetIdx(Idx);
		}
		else
		{
			OutRef.SetIdx(Elements.Add(Val));
		}
	}

	void IncRef(const TPropertyRef<T>& Ref)
	{
		ensure(Elements[Ref.GetIdx()].Count);	//must be that someone else is still holding a reference
		++Elements[Ref.GetIdx()].Count;
	}

	void DecRef(TPropertyRef<T>& Ref)
	{
		ensure(Ref.IsSet());	//double release?
		ensure(Elements[Ref.GetIdx()].Count > 0);	//double release?
		
		if(--Elements[Ref.GetIdx()].Count == 0)
		{
			//Can't use destructor because using TArray by value - this should be cheap anyway
			Elements[Ref.GetIdx()] = FPropertyAndCount();
			FreeList.Add(Ref.GetIdx());
		}

		Ref.SetIdx(INDEX_NONE);
	}

	const T& GetElement(const TPropertyRef<T>& Ref) const
	{
		ensure(Elements[Ref.GetIdx()].Count > 0);	//deleted ref?
		return Elements[Ref.GetIdx()].Val;
	}

	T& GetElement(const TPropertyRef<T>& Ref)
	{
		ensure(Elements[Ref.GetIdx()].Count > 0);	//deleted ref?
		return Elements[Ref.GetIdx()].Val;
	}

	~TPropertyPool()
	{
		ensure(Elements.Num() == FreeList.Num());	//All elements have been freed
	}

private:

	struct FPropertyAndCount
	{
		FPropertyAndCount() = default;
		FPropertyAndCount(const T& InVal)
		: Val(InVal)
		, Count(1)
		{}

		T Val;
		int32 Count = 0;
	};

	TArray<FPropertyAndCount> Elements;
	TArray<int32> FreeList;
};

//Similar to FDirtyPropertiesManager but is not needed to be used across threads
//This means we just have one big pool per property that you can new/free into
class FDirtyPropertiesPool
{
public:
	template <typename T, EParticleProperty PropName>
	TPropertyPool<T>& GetPool()
	{
		switch (PropName)
		{
#define PARTICLE_PROPERTY(PropName, Type) case EParticleProperty::PropName: return (TPropertyPool<T>&)PropName##Pool;
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY
		default: check(false);
		}

		static TPropertyPool<T> ErrorPool;
		return ErrorPool;
	}

	template <typename T, EParticleProperty PropName>
	const TPropertyPool<T>& GetPool() const
	{
		switch (PropName)
		{
#define PARTICLE_PROPERTY(PropName, Type) case EParticleProperty::PropName: return (TPropertyPool<T>&)PropName##Pool;
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY
		default: check(false);
		}

		static TPropertyPool<T> ErrorPool;
		return ErrorPool;
	}

private:

#define PARTICLE_PROPERTY(PropName, Type) TPropertyPool<Type> PropName##Pool;
#include "ParticleProperties.inl"
#undef PARTICLE_PROPERTY
};

template <typename T>
void TPropertyRef<T>::SetRefFrom(const TPropertyRef<T>& Other, TPropertyPool<T>& Pool)
{
	//don't do anything unless different
	if(IdxPlusOne != Other.IdxPlusOne)
	{
		if(IsSet())
		{
			Pool.DecRef(*this);
		}

		if(Other.IsSet())
		{
			Pool.IncRef(Other);
		}

		IdxPlusOne = Other.IdxPlusOne;
	}
}

}
