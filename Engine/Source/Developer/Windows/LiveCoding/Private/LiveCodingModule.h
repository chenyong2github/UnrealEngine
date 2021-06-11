// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILiveCodingModule.h"
#include "Delegates/Delegate.h"
#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"

struct IConsoleCommand;
class IConsoleVariable;
class ISettingsSection;
class ULiveCodingSettings;

#if WITH_EDITOR
class FReload;
#else
class FNullReload;
#endif

class FLiveCodingModule final : public ILiveCodingModule
{
public:
	FLiveCodingModule();
	~FLiveCodingModule();

	// IModuleInterface implementation
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// ILiveCodingModule implementation
	virtual void EnableByDefault(bool bInEnabled) override;
	virtual bool IsEnabledByDefault() const override;
	virtual void EnableForSession(bool bInEnabled) override;
	virtual bool IsEnabledForSession() const override;
	virtual bool CanEnableForSession() const override;
	virtual bool HasStarted() const override;
	virtual void ShowConsole() override;
	virtual void Compile() override;
	virtual bool IsCompiling() const override;
	virtual void Tick() override;
	virtual FOnPatchCompleteDelegate& GetOnPatchCompleteDelegate() override;

	static void BeginReload();

private:
	void AttemptSyncLivePatching();

private:
	ULiveCodingSettings* Settings;
	TSharedPtr<ISettingsSection> SettingsSection;
	bool bEnabledLastTick = false;
	bool bEnabledForSession = false;
	bool bStarted = false;
	bool bUpdateModulesInTick = false;
	bool bHasReinstancingOccurred = false;
	bool bHasPatchBeenLoaded = false;
	TSet<FName> ConfiguredModules;
	TArray<void*> LppPendingTokens;

	const FString FullEnginePluginsDir;
	const FString FullProjectDir;
	const FString FullProjectPluginsDir;

	IConsoleCommand* EnableCommand;
	IConsoleCommand* CompileCommand;
	IConsoleVariable* ConsolePathVariable;
	IConsoleVariable* SourceProjectVariable;
	FDelegateHandle EndFrameDelegateHandle;
	FDelegateHandle ModulesChangedDelegateHandle;
	FOnPatchCompleteDelegate OnPatchCompleteDelegate;

#if WITH_EDITOR
	TUniquePtr<FReload> Reload;
#else
	TUniquePtr<FNullReload> Reload;
#endif

	bool StartLiveCoding();

	void OnModulesChanged(FName ModuleName, EModuleChangeReason Reason);

	void UpdateModules();

	bool ShouldPreloadModule(const FName& Name, const FString& FullFilePath) const;

#if WITH_EDITOR
	void ShowNotification(bool Success, const FText& Title, const FText* SubText);
#endif
};

