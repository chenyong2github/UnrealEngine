// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SnapshotArchives.h"

/* For writing data into an object */
class FApplySnapshotDataArchive : public FObjectSnapshotArchive
{
	using Super = FObjectSnapshotArchive;
public:

	static FApplySnapshotDataArchive MakeDeserializingIntoWorldObject(const FBaseObjectInfo& InObjectInfo, const FPropertySelection* InSelectedProperties);
	static FApplySnapshotDataArchive MakeForDeserializingTransientObject(const FBaseObjectInfo& InObjectInfo);
	
	//~ Begin FArchive Interface
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
	//~ End FArchive Interface

private:
	
	FApplySnapshotDataArchive(const FBaseObjectInfo& InObjectInfo, const FPropertySelection* InSelectedProperties = nullptr);

	/* Valid when serializing into world actor. */
	TOptional<const FPropertySelection*> SelectedProperties;	
};