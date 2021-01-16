// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/GCObject.h"


/**
 * FModelingModeObjectsKeepaliveHelper is a small utility class that can be created to allow UObjects to
 * be explicitly held out from Garbage Collection. This is necessary to Hotfix various Tools in 4.26.1
 */
class FModelingModeObjectsKeepaliveHelper
{
public:

	void Enable()
	{
		ActiveKeepalive = MakeUnique<FGCKeepaliveObjectSet>();
	}

	void AddKeepaliveObject(UObject* Object)
	{
		ActiveKeepalive->KeepaliveObjects.Add(Object);
	}

	void Disable()
	{
		ActiveKeepalive->KeepaliveObjects.Reset();
		ActiveKeepalive = nullptr;
	}

private:

	class FGCKeepaliveObjectSet : public FGCObject
	{
	public:
		TSet<UObject*> KeepaliveObjects;

		virtual void AddReferencedObjects(FReferenceCollector& Collector)
		{
			for (UObject* Obj : KeepaliveObjects)
			{
				Collector.AddReferencedObject(Obj);
			}
		}
	};

	TUniquePtr<FGCKeepaliveObjectSet> ActiveKeepalive;
};
