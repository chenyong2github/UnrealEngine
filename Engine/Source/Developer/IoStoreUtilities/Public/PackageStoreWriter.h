// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/PackageStore.h"
#include "Serialization/PackageWriter.h"

class IPackageStoreWriter : public ICookedPackageWriter
{
public:
	/** Identify as a member of this interface from the ICookedPackageWriter api. */
	virtual IPackageStoreWriter* AsPackageStoreWriter() override
	{
		return this;
	}

	/**
	 * Returns all cooked package store entries.
	 */
	virtual void GetEntries(TFunction<void(TArrayView<const FPackageStoreEntryResource>)>&&) = 0;

	/**
	 * Package commit event arguments
	 */
	struct FCommitEventArgs
	{
		FName PlatformName;
		FName PackageName;
		int32 EntryIndex = INDEX_NONE;
		TArrayView<const FPackageStoreEntryResource> Entries;
		TArray<FAdditionalFileInfo> AdditionalFiles;
	};

	/**
	 * Broadcasted after a package has been committed, i.e cooked.
	 */
	DECLARE_EVENT_OneParam(IPackageStoreWriter, FCommitEvent, const FCommitEventArgs&);
	virtual FCommitEvent& OnCommit() = 0;
};