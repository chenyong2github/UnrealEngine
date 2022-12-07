// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/PhysicsObject.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Containers/Set.h"
#include "Framework/Threading.h"

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"

namespace Chaos
{
	struct FPhysicsObject
	{
	public:
		bool IsValid() const;

		void SetBodyIndex(int32 InBodyIndex) { BodyIndex = InBodyIndex; }
		int32 GetBodyIndex() const { return BodyIndex; }

		void SetName(const FName& InBodyName) { BodyName = InBodyName; }
		const FName& GetBodyName() const { return BodyName; }

		template<EThreadContext Id>
		EObjectStateType ObjectState()
		{
			TThreadParticle<Id>* Particle = GetParticle<Id>();
			if (!Particle)
			{
				return EObjectStateType::Uninitialized;
			}

			if (TThreadRigidParticle<Id>* Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->ObjectState();
			}
			else
			{
				return Particle->ObjectState();
			}
		}

		template<EThreadContext Id>
		FPhysicsObjectHandle GetRootObject()
		{
			EPhysicsProxyType ProxyType = Proxy->GetType();
			if (ProxyType == EPhysicsProxyType::SingleParticleProxy)
			{
				return this;
			}

			TThreadParticle<Id>* Particle = GetParticle<Id>();
			FPhysicsObjectHandle CurrentObject = this;
			FPhysicsObjectHandle CurrentParent = GetParentObject();
			while (CurrentParent && IsParticleDisabled<Id>(Particle))
			{
				Particle = CurrentParent->GetParticle<Id>();
				CurrentObject = CurrentParent;
				CurrentParent = CurrentParent->GetParentObject();
			}
			return CurrentObject;
		}

		FPhysicsObjectHandle GetParentObject() const;

		template <EThreadContext Id>
		TThreadParticle<Id>* GetRootParticle()
		{
			FPhysicsObjectHandle Root = GetRootObject<Id>();
			if (!Root)
			{
				return nullptr;
			}
			return Root->GetParticle<Id>();
		}

		template <EThreadContext Id>
		TThreadParticle<Id>* GetParticle()
		{
			if (Proxy)
			{
				EPhysicsProxyType ProxyType = Proxy->GetType();
				if constexpr (Id == EThreadContext::External)
				{
					switch (ProxyType)
					{
					case EPhysicsProxyType::GeometryCollectionType:
						return static_cast<FGeometryCollectionPhysicsProxy*>(Proxy)->GetParticleByIndex_External(BodyIndex);
					case EPhysicsProxyType::SingleParticleProxy:
						return static_cast<FSingleParticlePhysicsProxy*>(Proxy)->GetParticle_LowLevel();
					default:
						break;
					}
				}
				else
				{
					switch (ProxyType)
					{
					case EPhysicsProxyType::GeometryCollectionType:
						return static_cast<FGeometryCollectionPhysicsProxy*>(Proxy)->GetParticleByIndex_Internal(BodyIndex);
					case EPhysicsProxyType::SingleParticleProxy:
						return static_cast<FSingleParticlePhysicsProxy*>(Proxy)->GetHandle_LowLevel();
					default:
						break;
					}
				}
			}

			return (TThreadParticle<Id>*)nullptr;
		}

		template <EThreadContext Id>
		static bool IsParticleDisabled(TThreadParticle<Id>* Particle)
		{
			if (TThreadRigidParticle<Id>* Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->Disabled();
			}
			return false;
		}

		IPhysicsProxyBase* PhysicsProxy() { return Proxy; }
		const IPhysicsProxyBase* PhysicsProxy() const { return Proxy; }

		bool HasChildren() const;

		friend class FPhysicsObjectFactory;
	protected:
		FPhysicsObject(IPhysicsProxyBase* InProxy, int32 InBodyIndex = INDEX_NONE, const FName& InBodyName = NAME_None)
			: Proxy(InProxy)
			, BodyIndex(InBodyIndex)
			, BodyName(InBodyName)
		{}

	private:
		IPhysicsProxyBase* Proxy = nullptr;
		int32 BodyIndex = INDEX_NONE;
		FName BodyName = NAME_None;
	};

	/**
	 * This class helps us limit the creation of the FPhysicsObject to only internal use cases. Realistically, only the physics proxies
	 * should be creating physics objects.
	 */
	class FPhysicsObjectFactory
	{
	public:
		static FPhysicsObjectUniquePtr CreatePhysicsObject(IPhysicsProxyBase* InProxy, int32 InBodyIndex = INDEX_NONE, const FName& InBodyName = NAME_None);
	};
}