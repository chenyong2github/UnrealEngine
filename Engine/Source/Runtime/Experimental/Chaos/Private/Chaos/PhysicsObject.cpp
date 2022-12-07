// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PhysicsObject.h"
#include "Chaos/PhysicsObjectInternal.h"
#include "Chaos/Framework/PhysicsProxyBase.h"

namespace Chaos
{
	void FPhysicsObjectDeleter::operator()(FPhysicsObjectHandle p)
	{
		delete p;
	}

	FPhysicsObjectUniquePtr FPhysicsObjectFactory::CreatePhysicsObject(IPhysicsProxyBase* InProxy, int32 InBodyIndex, const FName& InBodyName)
	{
		return FPhysicsObjectUniquePtr{new FPhysicsObject(InProxy, InBodyIndex, InBodyName)};
	}

	bool FPhysicsObject::IsValid() const
	{
		return Proxy != nullptr && !Proxy->GetMarkedDeleted();
	}

	bool FPhysicsObject::HasChildren() const
	{
		EPhysicsProxyType ProxyType = Proxy->GetType();
		switch (ProxyType)
		{
		case EPhysicsProxyType::GeometryCollectionType:
		{
			FGeometryDynamicCollection& Collection = static_cast<FGeometryCollectionPhysicsProxy*>(Proxy)->GetExternalCollection();
			return !Collection.Children[BodyIndex].IsEmpty();
		}
		default:
			break;
		}
		return false;
	}

	FPhysicsObjectHandle FPhysicsObject::GetParentObject() const
	{
		EPhysicsProxyType ProxyType = Proxy->GetType();
		switch (ProxyType)
		{
		case EPhysicsProxyType::GeometryCollectionType:
		{
			FGeometryCollectionPhysicsProxy* GeometryCollectionProxy = static_cast<FGeometryCollectionPhysicsProxy*>(Proxy);
			FGeometryDynamicCollection& Collection = GeometryCollectionProxy->GetExternalCollection();
			if (int32 Index = Collection.Parent[BodyIndex]; Index != INDEX_NONE)
			{
				return GeometryCollectionProxy->GetPhysicsObjectByIndex(Index);
			}
		}
		default:
			break;
		}
		return nullptr;
	}
}