// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorReferencesUtils.h"
#include "Serialization/ArchiveUObject.h"
#include "GameFramework/Actor.h"

class FArchiveGatherExternalActorRefs : public FArchiveUObject
{
public:
	FArchiveGatherExternalActorRefs(UObject* InRoot, TSet<AActor*>& InActorReferences, EObjectFlags InRequiredFlags)
		: Root(InRoot)
		, ActorReferences(InActorReferences)
		, RequiredFlags(InRequiredFlags)
	{
		SetIsSaving(true);
		SetIsPersistent(true);
		ArIgnoreOuterRef = true;
		ArIsObjectReferenceCollector = true;
		ArShouldSkipBulkData = true;

		// Avoid having to wait until compilable asset are built just to scan for actors. 
		// Assets like Textures/Shaders/StaticMesh should not refer to actors anyway.
		SetShouldSkipCompilingAssets(true);

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

			if (((RequiredFlags == RF_NoFlags) || TopParentActor->HasAnyFlags(RequiredFlags)) && (TopParentActor != Root))
			{
				ActorReferences.Add(TopParentActor);
			}
		}
	}

	UObject* Root;
	TSet<AActor*>& ActorReferences;
	TSet<UObject*> SubObjects;
	EObjectFlags RequiredFlags;
};

TArray<AActor*> ActorsReferencesUtils::GetExternalActorReferences(UObject* Root)
{
	return GetActorReferences(Root, RF_HasExternalPackage);
}

TArray<AActor*> ActorsReferencesUtils::GetActorReferences(UObject* Root, EObjectFlags RequiredFlags)
{
	TSet<AActor*> Result;
	FArchiveGatherExternalActorRefs Ar(Root, Result, RequiredFlags);
	return Result.Array();
}