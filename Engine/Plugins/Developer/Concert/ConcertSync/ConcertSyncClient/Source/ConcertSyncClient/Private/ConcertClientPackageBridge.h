// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IConcertClientPackageBridge.h"

class UPackage;
class FPackageReloadedEvent;

struct FAssetData;

enum class EMapChangeType : uint8;
enum class EPackageReloadPhase : uint8;

class FConcertClientPackageBridge : public IConcertClientPackageBridge
{
public:
	FConcertClientPackageBridge();
	virtual ~FConcertClientPackageBridge();

	//~ IConcertClientPackageBridge interface
	virtual FOnConcertClientLocalPackageEvent& OnLocalPackageEvent() override;
	virtual FOnConcertClientLocalPackageDiscarded& OnLocalPackageDiscarded() override;
	virtual bool& GetIgnoreLocalSaveRef() override;
	virtual bool& GetIgnoreLocalDiscardRef() override;

private:
	/** Called prior to a package being saved to disk */
	void HandlePackagePreSave(UPackage* Package);

	/** Called after a package has been saved to disk */
	void HandlePackageSaved(const FString& PackageFilename, UObject* Outer);

	/** Called when a new asset is added */
	void HandleAssetAdded(UObject *Object);

	/** Called when an existing asset is deleted */
	void HandleAssetDeleted(UObject *Object);

	/** Called when an existing asset is renamed */
	void HandleAssetRenamed(const FAssetData& Data, const FString& OldName);

	/** Called when an asset is hot-reloaded */
	void HandleAssetReload(const EPackageReloadPhase InPackageReloadPhase, FPackageReloadedEvent* InPackageReloadedEvent);

	/** Called when the editor map is changed */
	void HandleMapChanged(UWorld* InWorld, EMapChangeType InMapChangeType);

	/** Called when a local package event happens */
	FOnConcertClientLocalPackageEvent OnLocalPackageEventDelegate;

	/** Called when a local package discard happens */
	FOnConcertClientLocalPackageDiscarded OnLocalPackageDiscardedDelegate;

	/** Flag to ignore package change events, used when we do not want to record package changes we generate ourselves */
	bool bIgnoreLocalSave;

	/** Flag to ignore package discards, used when we do not want to record package changes we generate ourselves */
	bool bIgnoreLocalDiscard;

	/** Map of packages that are in the process of being renamed */
	TMap<FName, FName> PackagesBeingRenamed;
};
