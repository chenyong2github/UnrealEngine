// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExternalActorsUtils.h"
#include "Serialization/ArchiveUObject.h"
#include "GameFramework/Actor.h"

class FArchiveGatherExternalActorRefs : public FArchiveUObject
{
public:
	FArchiveGatherExternalActorRefs(UObject* InRoot, TArray<AActor*>& InActorReferences)
		: Root(InRoot)
		, ActorReferences(InActorReferences)
	{
		SetIsSaving(true);
		SetIsPersistent(true);
		ArIgnoreOuterRef = true;
		ArIsObjectReferenceCollector = true;
		ArShouldSkipBulkData = true;

		Root->Serialize(*this);
	}

	virtual FArchive& operator<<(UObject*& Obj) override
	{
		if (Obj && (Obj != Root) && !Obj->IsTemplate() && !Obj->HasAnyFlags(RF_Transient))
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
		if(Obj->IsA<AActor>())
		{
			AActor* Actor = (AActor*)Obj;
			AActor* TopParentActor = Actor;
			while(TopParentActor->GetParentActor())
			{
				TopParentActor = TopParentActor->GetParentActor();
			}

			check(TopParentActor);

			if (TopParentActor->IsPackageExternal())
			{
				ActorReferences.Add(TopParentActor);
			}
		}
	}

	UObject* Root;
	TArray<AActor*>& ActorReferences;
	TSet<UObject*> SubObjects;
};

TArray<AActor*> ExternalActorsUtils::GetExternalActorReferences(UObject* Root)
{
	TArray<AActor*> Result;
	FArchiveGatherExternalActorRefs Ar(Root, Result);
	return MoveTemp(Result);
}
