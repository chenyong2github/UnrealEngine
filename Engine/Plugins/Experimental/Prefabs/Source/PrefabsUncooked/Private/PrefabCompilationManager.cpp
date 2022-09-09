// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrefabCompilationManager.h"
#include "Containers/Set.h"
#include "UObject/GCObject.h"
#include "PrefabUncooked.h"
#include "Tickable.h"

struct FPrefabCompilationManagerImpl : FGCObject, FTickableGameObject
{
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual TStatId GetStatId() const override;
	void CompilePrefab(const UPrefabUncooked* EditorPrefab);

	TSet<const UPrefabUncooked*> PrefabsPendingCompilation;
};

void FPrefabCompilationManagerImpl::AddReferencedObjects(FReferenceCollector& Collector)
{	
	Collector.AddReferencedObjects(PrefabsPendingCompilation);
}

void FPrefabCompilationManagerImpl::Tick(float DeltaTime)
{
	// spread this out? Run off main thread? The latter is desirable
	for (const UPrefabUncooked* ped : PrefabsPendingCompilation)
	{
		CompilePrefab(ped);
	}
	PrefabsPendingCompilation.Reset();
}

bool FPrefabCompilationManagerImpl::IsTickable() const
{
	return PrefabsPendingCompilation.Num() > 0;
}

TStatId FPrefabCompilationManagerImpl::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FPrefabCompilationManagerImpl, STATGROUP_Tickables);
}

void FPrefabCompilationManagerImpl::CompilePrefab(const UPrefabUncooked* EditorPrefab)
{
	// add a ufunction? make a lil UPrefab class to hide our factory function?
}

FPrefabCompilationManagerImpl* PFCMImpl = nullptr;

void FPrefabCompilationManager::Initialize()
{
	if (PFCMImpl)
	{
		delete PFCMImpl;
	}
	PFCMImpl = new FPrefabCompilationManagerImpl();
}

void FPrefabCompilationManager::NotifyPrefabEdited(const UPrefabUncooked* EditorPrefab)
{
	PFCMImpl->PrefabsPendingCompilation.Add(EditorPrefab);
}
