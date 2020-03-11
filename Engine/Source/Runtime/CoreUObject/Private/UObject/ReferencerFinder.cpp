// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/ReferencerFinder.h"

#include "UObject/UObjectIterator.h"
#include "UObject/FastReferenceCollector.h"
#include "UObject/UObjectArray.h"
#include "Async/ParallelFor.h"

class FAllReferencesProcessor : public FSimpleReferenceProcessorBase
{
	const TSet<UObject*>& PotentiallyReferencedObjects;
	TSet<UObject*>& ReferencingObjects;
	UObject* CurrentObject;

public:
	FAllReferencesProcessor(const TSet<UObject*>& InPotentiallyReferencedObjects, TSet<UObject*>& OutReferencingObjects)
		: PotentiallyReferencedObjects(InPotentiallyReferencedObjects)
		, ReferencingObjects(OutReferencingObjects)
		, CurrentObject(nullptr)
	{
	}
	FORCEINLINE_DEBUGGABLE void HandleTokenStreamObjectReference(TArray<UObject*>& ObjectsToSerialize, UObject* ReferencingObject, UObject*& Object, const int32 TokenIndex, bool bAllowReferenceElimination)
	{
		if (!ReferencingObject)
		{
			ReferencingObject = CurrentObject;
		}
		if (Object && ReferencingObject && Object != ReferencingObject)
		{
			if (PotentiallyReferencedObjects.Contains(Object))
			{
				ReferencingObjects.Add(ReferencingObject);
			}
		}
	}
	void SetCurrentObject(UObject* Obj)
	{
		CurrentObject = Obj;
	}
};
typedef TDefaultReferenceCollector<FAllReferencesProcessor> FAllReferencesCollector;

// Until all native UObject classes have been registered it's unsafe to run FReferencerFinder on multiple threads
static bool GUObjectRegistrationComplete = false;

void FReferencerFinder::NotifyRegistrationComplete()
{
	GUObjectRegistrationComplete = true;
}

TArray<UObject*> FReferencerFinder::GetAllReferencers(const TArray<UObject*>& Referencees, const TSet<UObject*>* ObjectsToIgnore )
{
	return GetAllReferencers(TSet<UObject*>(Referencees), ObjectsToIgnore);
}

void LockUObjectHashTables();
void UnlockUObjectHashTables();

TArray<UObject*> FReferencerFinder::GetAllReferencers(const TSet<UObject*>& Referencees, const TSet<UObject*>* ObjectsToIgnore )
{
	TArray<UObject*> Ret;
	if(Referencees.Num() > 0)
	{
		FCriticalSection ResultCritical;

		// Lock hashtables so that nothing can add UObjects while we're iterating over the GUObjectArray
		LockUObjectHashTables();

		const int32 MaxNumberOfObjects = GUObjectArray.GetObjectArrayNum();
		const int32 NumThreads = FMath::Max(1, FTaskGraphInterface::Get().GetNumWorkerThreads());
		const int32 NumberOfObjectsPerThread = (MaxNumberOfObjects / NumThreads) + 1;

		ParallelFor(NumThreads, [&Referencees, ObjectsToIgnore, &ResultCritical, &Ret, NumberOfObjectsPerThread, NumThreads, MaxNumberOfObjects](int32 ThreadIndex)
		{
			TSet<UObject*> ThreadResult;
			FAllReferencesProcessor Processor(Referencees, ThreadResult);
			TFastReferenceCollector<false, FAllReferencesProcessor, FAllReferencesCollector, FGCArrayPool, true> ReferenceCollector(Processor, FGCArrayPool::Get());
			FGCArrayStruct ArrayStruct;

			ArrayStruct.ObjectsToSerialize.Reserve(NumberOfObjectsPerThread);

			const int32 FirstObjectIndex = ThreadIndex * NumberOfObjectsPerThread;
			const int32 NumObjects = (ThreadIndex < (NumThreads - 1)) ? NumberOfObjectsPerThread : (MaxNumberOfObjects - (NumThreads - 1)*NumberOfObjectsPerThread);

			// First cache all potential referencers
			for (int32 ObjectIndex = 0; ObjectIndex < NumObjects && (FirstObjectIndex + ObjectIndex) < MaxNumberOfObjects; ++ObjectIndex)
			{
				FUObjectItem& ObjectItem = GUObjectArray.GetObjectItemArrayUnsafe()[FirstObjectIndex + ObjectIndex];
				if (ObjectItem.Object)
				{
					UObject* PotentialReferencer = static_cast<UObject*>(ObjectItem.Object);

					if (ObjectsToIgnore && ObjectsToIgnore->Contains(PotentialReferencer))
					{
						continue;
					}

					if (!Referencees.Contains(PotentialReferencer))
					{
						ArrayStruct.ObjectsToSerialize.Add(PotentialReferencer);
					}
				}
			}

			// Now check if any of the potential referencers is referencing any of the referencees
			ReferenceCollector.CollectReferences(ArrayStruct);

			if (ThreadResult.Num())
			{
				// We found objects referencing some of the referencess so add them to the final results array
				FScopeLock ResultLock(&ResultCritical);
				Ret.Append(ThreadResult.Array());
			}
		}, GUObjectRegistrationComplete ? EParallelForFlags::None :  EParallelForFlags::ForceSingleThread );

		UnlockUObjectHashTables();
	}
	return Ret;
}
