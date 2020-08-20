// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FMotionTrailEditorModeModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void OnSequencerCreated(TSharedRef<class ISequencer> Sequencer);

	FDelegateHandle OnSequencerCreatedHandle;
};
