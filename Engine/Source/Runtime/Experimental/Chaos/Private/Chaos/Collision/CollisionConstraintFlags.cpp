// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Collision/CollisionConstraintFlags.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{

	bool FIgnoreCollisionManager::ContainsHandle(FHandleID Body0)
	{
		return IgnoreCollisionsList.Contains(Body0);
	}

	bool FIgnoreCollisionManager::IgnoresCollision(FHandleID Body0, FHandleID Body1)
	{
		if (IgnoreCollisionsList.Contains(Body0))
		{
			return IgnoreCollisionsList[Body0].Contains(Body1);
		}
		return false;
	}

	int32 FIgnoreCollisionManager::NumIgnoredCollision(FHandleID Body0)
	{
		if (IgnoreCollisionsList.Contains(Body0))
		{
			return IgnoreCollisionsList[Body0].Num();
		}
		return 0;
	}

	void FIgnoreCollisionManager::AddIgnoreCollisionsFor(FHandleID Body0, FHandleID Body1)
	{
		if (!IgnoreCollisionsList.Contains(Body0))
		{
			IgnoreCollisionsList.Add(Body0, TArray<FHandleID>());
		}
		IgnoreCollisionsList[Body0].Add(Body1);

	}
	void FIgnoreCollisionManager::RemoveIgnoreCollisionsFor(FHandleID Body0, FHandleID Body1)
	{
		if (IgnoreCollisionsList.Contains(Body0))
		{
			IgnoreCollisionsList[Body0].Remove(Body1);
		}
	}

	void FIgnoreCollisionManager::PopStorageData_Internal(int32 ExternalTimestamp)
	{
		FStorageData* StorageData;
		while(StorageDataQueue.Peek(StorageData) && StorageData->ExternalTimestamp <= ExternalTimestamp)
		{
			for (auto& Elem : StorageData->PendingActivations)
			{
				if (PendingActivations.Contains(Elem.Key))
				{
					// the case where the key already existed should be avoided
					// but the implementation is here for completeness. 
					for (auto& Val : Elem.Value)
					{
						if (!PendingActivations[Elem.Key].Contains(Val))
						{
							PendingActivations[Elem.Key].Add(Val);
						}
					}
				}
				else
				{
					PendingActivations.Add(Elem.Key, Elem.Value);
				}
			}

			for (auto& Item : StorageData->PendingDeactivations)
			{
				if (!PendingDeactivations.Contains(Item))
				{
					PendingDeactivations.Add(Item);
				}
			}

			StorageDataQueue.Pop();
			ReleaseStorageData(StorageData);
		}
	}

	void FIgnoreCollisionManager::ProcessPendingQueues()
	{

		// remove particles that have been created and destroyed
		// before the queue was ever processed. 
		TArray<FHandleID> PreculledParticles;
		if (PendingActivations.Num() && PendingDeactivations.Num())
		{
			TArray<FHandleID> DeletionList;
			for (auto& Elem : PendingActivations)
			{
				int32 DeactiveIndex = PendingDeactivations.Find(Elem.Key);
				if (DeactiveIndex != INDEX_NONE)
				{
					DeletionList.Add(Elem.Key);
					PreculledParticles.Add(Elem.Key);
					PendingDeactivations.RemoveAtSwap(DeactiveIndex, 1);
				}
			}
			for (FHandleID Del : DeletionList)
				PendingActivations.Remove(Del);
		}

		// add collision relationships for particles that have valid
		// handles, and have not already been removed from the 
		// simulation. 
		if (PendingActivations.Num())
		{
			TArray<FHandleID> DeletionList;
			for (auto& Elem : PendingActivations)
			{
				for (int Index = Elem.Value.Num() - 1; Index >= 0; Index--)
				{
					if (PreculledParticles.Contains(Elem.Value[Index]))
					{
						Elem.Value.RemoveAtSwap(Index, 1);
					}
					else
					{
						FUniqueIdx ID0 = Elem.Key;
						FUniqueIdx ID1 = Elem.Value[Index];
						if (!IgnoresCollision(ID0, ID1))
						{
							AddIgnoreCollisionsFor(ID0, ID1);
							AddIgnoreCollisionsFor(ID1, ID0);
						}

						Elem.Value.RemoveAtSwap(Index, 1);
					}
				}
				if (!Elem.Value.Num())
					DeletionList.Add(Elem.Key);
			}
			for (FHandleID Del : DeletionList)
				PendingActivations.Remove(Del);
		}

		// remove relationships that exist and have been initialized. 
		if (PendingDeactivations.Num())
		{
			for (auto Index = PendingDeactivations.Num() - 1; Index >= 0; Index--)
			{
				IgnoreCollisionsList.Remove(PendingDeactivations[Index]);
			}
			PendingDeactivations.Empty();
		}
	}

} // Chaos

