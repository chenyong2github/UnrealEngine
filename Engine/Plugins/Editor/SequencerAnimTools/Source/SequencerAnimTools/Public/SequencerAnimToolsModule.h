// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class ILevelEditor;

namespace UE
{
namespace SequencerAnimTools
{


class FSequencerAnimToolsModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TWeakPtr<ILevelEditor> LevelEditorPtr;
	TSharedPtr<FUICommandList> CommandBindings;

	//delegates
	void OnLevelEditorCreated(TSharedPtr<ILevelEditor> InLevelEditor);
	void OnMotionTralOptionChanged(FName PropretyName);

	//need to keep track of which levels we registered the tools for otherwise we can hit an ensure
	TSet<ILevelEditor*>  AlreadyRegisteredTools;
};

} // namespace SequencerAnimTools
} // namespace UE
