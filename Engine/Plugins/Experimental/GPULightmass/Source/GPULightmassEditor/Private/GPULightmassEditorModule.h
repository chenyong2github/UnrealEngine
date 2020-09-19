// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "TickableEditorObject.h"
#include "Input/Reply.h"

class FExtender;
class FMenuBuilder;
class FSpawnTabArgs;
class FUICommandList;
class IDetailsView;
class SDockTab;
class STextBlock;

enum class EMapChangeType : uint8;

class FGPULightmassEditorModule : public IModuleInterface, public FTickableEditorObject
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// Begin FTickableObjectBase interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	// End FTickableObjectBase interface

	TSharedPtr<IDetailsView> SettingsView;
	TSharedRef<SDockTab> SpawnSettingsTab(const FSpawnTabArgs& Args);
	void RegisterTabSpawner();
	FReply OnStartStopClicked();
	void OnMapChanged(UWorld* InWorld, EMapChangeType MapChangeType);

	TSharedRef<FExtender> OnExtendLevelEditorBuildMenu(const TSharedRef<FUICommandList> CommandList);
	void CreateBuildMenu(FMenuBuilder& Builder);

	TSharedPtr<STextBlock> StartStopButtonText;
	TSharedPtr<STextBlock> Messages;
};
