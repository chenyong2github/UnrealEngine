// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILevelSnapshotsEditorView.h"

class ULevelSnapshotFilter;

class ILevelSnapshotsEditorFilters : public ILevelSnapshotsEditorView
{
public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSetActiveFilter, ULevelSnapshotFilter* /* bInEnabled */);

	virtual FOnSetActiveFilter& GetOnSetActiveFilter() = 0;

	virtual void SetActiveFilter(ULevelSnapshotFilter* InFilter) = 0;

	virtual const ULevelSnapshotFilter* GetActiveFilter() const = 0;
};
