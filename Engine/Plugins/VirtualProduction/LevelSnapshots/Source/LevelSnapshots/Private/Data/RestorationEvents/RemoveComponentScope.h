// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IRestorationListener.h"

/**
 * Convenience type that calls FLevelSnaphshotsModule::OnPreRemoveComponent and FLevelSnaphshotsModule::OnPostRemoveComponent.
 */
class FRemoveComponentScope : public FNoncopyable
{
	FPostRemoveComponentParams Params;
public:

	FRemoveComponentScope(UActorComponent* RemovedComponent);
	~FRemoveComponentScope();
};
