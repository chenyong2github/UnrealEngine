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
	virtual void RegisterPropertyComparer(UClass* Class, TSharedRef<IPropertyComparer> Comparer) override;
	virtual void UnregisterPropertyComparer(UClass* Class, TSharedRef<IPropertyComparer> Comparer) override;
	virtual void AddWhitelistedProperties(const TSet<const FProperty*>& Properties) override;
	virtual void RemoveWhitelistedProperties(const TSet<const FProperty*>& Properties) override;
	virtual void AddBlacklistedProperties(const TSet<const FProperty*>& Properties) override;
	virtual void RemoveBlacklistedProperties(const TSet<const FProperty*>& Properties) override;
	//~ Begin ILevelSnapshotsModule Interface
	
	const TArray<TSharedRef<ISnapshotRestorabilityOverrider>>& GetOverrides() const;
	bool IsPropertyWhitelisted(const FProperty* Property) const;
	bool IsPropertyBlacklisted(const FProperty* Property) const;

	FPropertyComparerArray GetPropertyComparerForClass(UClass* Class) const;
	IPropertyComparer::EPropertyComparison ShouldConsiderPropertyEqual(const FPropertyComparerArray& Comparers, const FPropertyComparisonParams& Params) const;
	
private:

	/* Allows external modules to override what objects and properties are considered by the snapshot system. */
	TArray<TSharedRef<ISnapshotRestorabilityOverrider>> Overrides;
	/**/
	TMap<FSoftClassPath, TArray<TSharedRef<IPropertyComparer>>> PropertyComparers;

	/* Allows these properties even when the default behaviour would exclude them. */
	TSet<const FProperty*> WhitelistedProperties;
	/* Forbid these properties even when the default behaviour would include them. */
	TSet<const FProperty*> BlacklistedProperties;
	
};
