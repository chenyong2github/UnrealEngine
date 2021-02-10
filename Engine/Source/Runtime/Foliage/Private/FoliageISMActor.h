// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "InstancedFoliage.h"
#include "ISMPartition/ISMPartitionActor.h"
#include "ISMPartition/ISMComponentDescriptor.h"
#include "Misc/Guid.h"
#include "Containers/SortedMap.h"

struct FFoliageInfo;
class UInstancedStaticMeshComponent;
class UPrimitiveComponent;
class UBlueprint;
class UFoliageType_Actor;

struct FFoliageISMActor : public FFoliageImpl
{
	FFoliageISMActor(FFoliageInfo* Info)
		: FFoliageImpl(Info)
#if WITH_EDITORONLY_DATA
		, Guid(FGuid::NewGuid())
		, ActorClass(nullptr)
		, FoliageTypeActor(nullptr)
#endif
	{
	}

#if WITH_EDITORONLY_DATA
	FGuid Guid;
	FISMClientHandle ClientHandle;
	TSortedMap<int32, TArray<FTransform>> ISMDefinition;
	UClass* ActorClass;
	const UFoliageType_Actor* FoliageTypeActor;
#endif
	virtual void Serialize(FArchive& Ar) override;
		
#if WITH_EDITOR
	virtual void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector) override;
	virtual bool IsInitialized() const override;
	virtual void Initialize(const UFoliageType* FoliageType) override;
	virtual void Uninitialize() override;
	virtual void Reapply(const UFoliageType* FoliageType) override;
	virtual int32 GetInstanceCount() const override;
	virtual void PreAddInstances(const UFoliageType* FoliageType, int32 AddedInstanceCount) override;
	virtual void AddInstance(const FFoliageInstance& NewInstance) override;
	virtual void RemoveInstance(int32 InstanceIndex) override;
	virtual void SetInstanceWorldTransform(int32 InstanceIndex, const FTransform& Transform, bool bTeleport) override;
	virtual FTransform GetInstanceWorldTransform(int32 InstanceIndex) const override;
	virtual bool IsOwnedComponent(const UPrimitiveComponent* Component) const override;
	
	virtual void SelectAllInstances(bool bSelect) override;
	virtual void SelectInstance(bool bSelect, int32 Index) override;
	virtual void SelectInstances(bool bSelect, const TSet<int32>& SelectedIndices) override;
	virtual int32 GetInstanceIndexFrom(const UPrimitiveComponent* Component, int32 ComponentIndex) const;
	virtual FBox GetSelectionBoundingBox(const TSet<int32>& SelectedIndices) const override;
	virtual void ApplySelection(bool bApply, const TSet<int32>& SelectedIndices) override;
	virtual void ClearSelection(const TSet<int32>& SelectedIndices) override;

	virtual void BeginUpdate() override;
	virtual void EndUpdate() override;
	virtual void Refresh(bool bAsync, bool bForce) override;
	virtual void OnHiddenEditorViewMaskChanged(uint64 InHiddenEditorViews) override;
	virtual void PostEditUndo(FFoliageInfo* InInfo, UFoliageType* FoliageType) override;
	virtual void NotifyFoliageTypeWillChange(UFoliageType* FoliageType) override;
	virtual void NotifyFoliageTypeChanged(UFoliageType* FoliageType, bool bSourceChanged) override;

private:
	void RegisterDelegates();
	void UnregisterDelegates();
	void OnBlueprintChanged(UBlueprint* InBlueprint);
#endif
};