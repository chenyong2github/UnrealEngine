// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ILevelSnapshotsModule.h"
#include "Restorability/IPropertyComparer.h"
#include "Restorability/ISnapshotRestorabilityOverrider.h"
#include "UObject/SoftObjectPath.h"

struct FPropertyComparisonParams;

// The array is used very often in many loops: optimize heap allocation
using FPropertyComparerArray = TArray<TSharedRef<IPropertyComparer>, TInlineAllocator<4>>;

class LEVELSNAPSHOTS_API FLevelSnapshotsModule : public ILevelSnapshotsModule
{
public:

	static FLevelSnapshotsModule& GetInternalModuleInstance();
	
	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface

	//~ Begin ILevelSnapshotsModule Interface
	virtual void RegisterRestorabilityOverrider(TSharedRef<ISnapshotRestorabilityOverrider> Overrider) override;
	virtual void UnregisterRestorabilityOverrider(TSharedRef<ISnapshotRestorabilityOverrider> Overrider) override;
	virtual void AddBlacklistedSubobjectClasses(const TSet<UClass*>& Classes) override;
	virtual void RemoveBlacklistedSubobjectClasses(const TSet<UClass*>& Classes) override;
	virtual void RegisterPropertyComparer(UClass* Class, TSharedRef<IPropertyComparer> Comparer) override;
	virtual void UnregisterPropertyComparer(UClass* Class, TSharedRef<IPropertyComparer> Comparer) override;
	virtual void RegisterCustomObjectSerializer(UClass* Class, TSharedRef<ICustomObjectSnapshotSerializer> CustomSerializer, bool bIncludeBlueprintChildClasses = true);
	virtual void UnregisterCustomObjectSerializer(UClass* Class) override;
	virtual void RegisterSnapshotLoader(TSharedRef<ISnapshotLoader> Loader) override;
	virtual void UnregisterSnapshotLoader(TSharedRef<ISnapshotLoader> Loader) override;
	virtual void RegisterRestorationListener(TSharedRef<IRestorationListener> Listener) override;
	virtual void UnregisterRestorationListener(TSharedRef<IRestorationListener> Listener) override;
	virtual void AddWhitelistedProperties(const TSet<const FProperty*>& Properties) override;
	virtual void RemoveWhitelistedProperties(const TSet<const FProperty*>& Properties) override;
	virtual void AddBlacklistedProperties(const TSet<const FProperty*>& Properties) override;
	virtual void RemoveBlacklistedProperties(const TSet<const FProperty*>& Properties) override;
	virtual void AddBlacklistedClassDefault(const UClass* Class) override;
	virtual void RemoveBlacklistedClassDefault(const UClass* Class) override;
	virtual bool IsClassDefaultBlacklisted(const UClass* Class) const override;
	//~ Begin ILevelSnapshotsModule Interface

	bool IsSubobjectClassBlacklisted(const UClass* Class) const;
	
	const TArray<TSharedRef<ISnapshotRestorabilityOverrider>>& GetOverrides() const;
	bool IsPropertyWhitelisted(const FProperty* Property) const;
	bool IsPropertyBlacklisted(const FProperty* Property) const;

	FPropertyComparerArray GetPropertyComparerForClass(UClass* Class) const;
	IPropertyComparer::EPropertyComparison ShouldConsiderPropertyEqual(const FPropertyComparerArray& Comparers, const FPropertyComparisonParams& Params) const;

	TSharedPtr<ICustomObjectSnapshotSerializer> GetCustomSerializerForClass(UClass* Class) const;

	virtual void AddCanTakeSnapshotDelegate(FName DelegateName, FCanTakeSnapshot Delegate) override;
	virtual void RemoveCanTakeSnapshotDelegate(FName DelegateName) override;
	virtual bool CanTakeSnapshot(const FPreTakeSnapshotEventData& Event) const override;

	void OnPostLoadSnapshotObject(const FPostLoadSnapshotObjectParams& Params);

	void OnPreApplySnapshotProperties(const FApplySnapshotPropertiesParams& Params);
	void OnPostApplySnapshotProperties(const FApplySnapshotPropertiesParams& Params);

	void OnPreApplySnapshotToActor(const FApplySnapshotToActorParams& Params);
	void OnPostApplySnapshotToActor(const FApplySnapshotToActorParams& Params);
	
	void OnPreRecreateComponent(const FPreRecreateComponentParams& Params);
	void OnPostRecreateComponent(UActorComponent* RecreatedComponent);

	void OnPreRemoveComponent(UActorComponent* ComponentToRemove);
	void OnPostRemoveComponent(const FPostRemoveComponentParams& Params);
	
private:

	struct FCustomSerializer
	{
		TSharedRef<ICustomObjectSnapshotSerializer> Serializer;
		bool bIncludeBlueprintChildren;
	};
	
	/* Allows external modules to override what objects and properties are considered by the snapshot system. */
	TArray<TSharedRef<ISnapshotRestorabilityOverrider>> Overrides;

	/** Subobject classes we do not capture nor restore */
	TSet<UClass*> BlacklistedSubobjectClasses;
	
	TMap<FSoftClassPath, TArray<TSharedRef<IPropertyComparer>>> PropertyComparers;
	TMap<FSoftClassPath, FCustomSerializer> CustomSerializers;
	
	TArray<TSharedRef<ISnapshotLoader>> SnapshotLoaders;
	TArray<TSharedRef<IRestorationListener>> RestorationListeners;

	/* Allows these properties even when the default behaviour would exclude them. */
	TSet<const FProperty*> WhitelistedProperties;
	/* Forbid these properties even when the default behaviour would include them. */
	TSet<const FProperty*> BlacklistedProperties;

	/** Classes for which to not serialize class default */
	TSet<FSoftClassPath> BlacklistedCDOs;

	/** Map of named delegates for confirming that a level snapshot is possible. */
	TMap<FName, FCanTakeSnapshot> CanTakeSnapshotDelegates;
};
