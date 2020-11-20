// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExternalActorsUtils.h"
#include "Serialization/ArchiveUObject.h"
#include "GameFramework/Actor.h"

class FArchiveGatherExternalActorRefs : public FArchiveUObject
{
public:
	FArchiveGatherExternalActorRefs(UObject* InRoot, TSet<AActor*>& InActorReferences)
		: Root(InRoot)
		, ActorReferences(InActorReferences)
	{
		SetIsSaving(true);
		SetIsPersistent(true);
		ArIgnoreOuterRef = true;
		ArIsObjectReferenceCollector = true;
		ArShouldSkipBulkData = true;

		SubObjects.Add(Root);

		Root->Serialize(*this);
	}

	virtual FArchive& operator<<(UObject*& Obj) override
	{
		if (Obj && !Obj->IsTemplate() && !Obj->HasAnyFlags(RF_Transient))
		{
			bool bWasAlreadyInSet;
			SubObjects.Add(Obj, &bWasAlreadyInSet);

			if (!bWasAlreadyInSet)
			{
				HandleObjectReference(Obj);

				if (Obj->IsInOuter(Root))
				{
					Obj->Serialize(*this);
				}
			}
		}
		return *this;
	}

private:
	void HandleObjectReference(UObject* Obj)
	{
		AActor* Actor = Cast<AActor>(Obj);

		if (!Actor)
		{
			Actor = Obj->GetTypedOuter<AActor>();
		}

		if(Actor)
		{
			AActor* TopParentActor = Actor;
			while(TopParentActor->GetParentActor())
			{
				TopParentActor = TopParentActor->GetParentActor();
			}

			check(TopParentActor);

			if (TopParentActor->IsPackageExternal() && (TopParentActor != Root))
			{
				ActorReferences.Add(TopParentActor);
			}
		}
	}

	UObject* Root;
	TSet<AActor*>& ActorReferences;
	TSet<UObject*> SubObjects;
};

TArray<AActor*> ExternalActorsUtils::GetExternalActorReferences(UObject* Root)
{
	TSet<AActor*> Result;
	FArchiveGatherExternalActorRefs Ar(Root, Result);
	return Result.Array();
}
