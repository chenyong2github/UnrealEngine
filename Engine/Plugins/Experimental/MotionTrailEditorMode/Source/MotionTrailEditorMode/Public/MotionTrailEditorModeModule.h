// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class ISequencer;

namespace UE
{
namespace MotionTrailEditor
{

class FMotionTrailEditorModeModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void OnSequencerCreated(TSharedRef<ISequencer> Sequencer);

	FDelegateHandle OnSequencerCreatedHandle;
	FDelegateHandle OnCreateTrackEditorHandle;
};

} // namespace MotionTrailEditor
} // namespace UE
