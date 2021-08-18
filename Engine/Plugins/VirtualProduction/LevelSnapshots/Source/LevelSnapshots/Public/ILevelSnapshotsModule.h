// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Restorability/IPropertyComparer.h"
#include "Restorability/ISnapshotRestorabilityOverrider.h"
#include "Serialization/ICustomObjectSnapshotSerializer.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class ULevelSnapshot;

struct LEVELSNAPSHOTS_API FPreTakeSnapshotEventData
{
	ULevelSnapshot* Snapshot;
};

struct LEVELSNAPSHOTS_API FPostTakeSnapshotEventData
{
	ULevelSnapshot* Snapshot;
};

class LEVELSNAPSHOTS_API ILevelSnapshotsModule : public IModuleInterface
{
public:

	static ILevelSnapshotsModule& Get()
	{
		return FModuleManager::Get().GetModuleChecked<ILevelSnapshotsModule>("LevelSnapshots");
	}
	

	DECLARE_EVENT_OneParam(ILevelSnapshotsModule, FPreTakeSnapshotEvent, const FPreTakeSnapshotEventData&);
	/** Called before a snapshot is taken. */
	FPreTakeSnapshotEvent& OnPreTakeSnapshot() { return PreTakeSnapshot; }
	
	DECLARE_EVENT_OneParam(ILevelSnapshotsModule, FPostTakeSnapshotEvent, const FPostTakeSnapshotEventData&);
	/** Called after a snapshot is taken */
	FPostTakeSnapshotEvent& OnPostTakeSnapshot() { return PostTakeSnapshot; }

	DECLARE_DELEGATE_RetVal_OneParam(bool, FCanTakeSnapshot, const FPreTakeSnapshotEventData&);
	/** Add a named delegate that determines if we can take a snapshot. */
	virtual void AddCanTakeSnapshotDelegate(FName DelegateName, FCanTakeSnapshot Delegate) = 0;

	/** Remove previously added named delegate that determines if we can take a snapshot. */
	virtual void RemoveCanTakeSnapshotDelegate(FName DelegateName) = 0;

	/** Queries the attached snapshot delegate and determines if we can take a snapshot.*/
	virtual bool CanTakeSnapshot(const FPreTakeSnapshotEventData& Event) const = 0;

	/** */
	/* Registers callbacks that override which actors, components, and properties are restored by default. */
	virtual void RegisterRestorabilityOverrider(TSharedRef<ISnapshotRestorabilityOverrider> Overrider) = 0;
	/* Unregisters an overrider previously registered. */
	virtual void UnregisterRestorabilityOverrider(TSharedRef<ISnapshotRestorabilityOverrider> Overrider) = 0;

	
	/* Registers a callback for deciding whether a property should be considered changed. Applies to all sub-classes. */
	virtual void RegisterPropertyComparer(UClass* Class, TSharedRef<IPropertyComparer> Comparer) = 0;
	virtual void UnregisterPropertyComparer(UClass* Class, TSharedRef<IPropertyComparer> Comparer) = 0;

	
	/**
	 * Registers callbacks for snapshotting / restoring certain classes. There can only be one per class. The typical use case using  Level Snapshots for restoring subobjects
	 * you want recreate / find manually.
	 *
	 * @param Class The class to register. This must be a native class (because Blueprint classes may be reinstanced when recompiled - not supported atm).
	 * @param CustomSerializer Your callbacks
	 * @param bIncludeBlueprintChildClasses Whether to use 'CustomSerializer' for Blueprint child classes of 'Class'
	 */
	virtual void RegisterCustomObjectSerializer(UClass* Class, TSharedRef<ICustomObjectSnapshotSerializer> CustomSerializer, bool bIncludeBlueprintChildClasses = true) = 0;
	virtual void UnregisterCustomObjectSerializer(UClass* Class) = 0;
	
	
	/**
	 * Adds properties that snapshots will capture and restore from now on. This allows support for properties that are skipped by default.
	 * Important: Only add add native properties; Blueprint properties may be invalidated (and left dangeling) when recompiled.
	 */
	virtual void AddWhitelistedProperties(const TSet<const FProperty*>& Properties) = 0;
	virtual void RemoveWhitelistedProperties(const TSet<const FProperty*>& Properties) = 0;

	/** Stops snapshots from capturing / restoring these properties.
	 * Important: Only add add native properties; Blueprint properties may be invalidated (and left dangeling) when recompiled.
	 */
	virtual void AddBlacklistedProperties(const TSet<const FProperty*>& Properties) = 0;
	virtual void RemoveBlacklistedProperties(const TSet<const FProperty*>& Properties) = 0;

protected:

	FPreTakeSnapshotEvent PreTakeSnapshot;
	FPostTakeSnapshotEvent PostTakeSnapshot;
};
