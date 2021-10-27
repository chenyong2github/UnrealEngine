// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IRestorationListener.h"
#include "Templates/UnrealTemplate.h"

struct FPropertySelectionMap;

/**
 * Convenience type that calls FLevelSnaphshotsModule::OnPreApplySnapshot and FLevelSnaphshotsModule::OnPostApplySnapshot.
 */
class FApplySnapshotPropertiesScope : public FNoncopyable
{
	const FApplySnapshotPropertiesParams Params;
public:

	FApplySnapshotPropertiesScope(const FApplySnapshotPropertiesParams& InParams);
	~FApplySnapshotPropertiesScope();
};