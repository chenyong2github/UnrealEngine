// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorConfigSubsystem.h"

#include "Async/Async.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

UEditorConfigSubsystem::UEditorConfigSubsystem()
{
	
}

void UEditorConfigSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	AddSearchDirectory(FPaths::Combine(FPaths::Combine(FPlatformProcess::UserSettingsDir(), *FApp::GetEpicProductIdentifier(), TEXT("Editor")))); // AppData
	AddSearchDirectory(FPaths::Combine(FPaths::EngineConfigDir(), TEXT("Editor"))); // Engine
	AddSearchDirectory(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("Editor"))); // ProjectName
}

TSharedPtr<FEditorConfig> UEditorConfigSubsystem::FindOrLoadConfig(FStringView ConfigName)
{
	FString ConfigNameString(ConfigName);
		
	TSharedPtr<FEditorConfig> Current;

	for (const FString& Dir : SearchDirectories)
	{
		FString FullPath = FPaths::Combine(Dir, ConfigNameString) + TEXT(".json");

		const TSharedPtr<FEditorConfig>* Existing = LoadedConfigs.Find(FullPath);
		if (Existing != nullptr && Existing->IsValid())
		{
			Current = *Existing;
		}
		else
		{
			// didn't exist yet, load now
			TSharedPtr<FEditorConfig> NewConfig = MakeShared<FEditorConfig>();
			if (NewConfig->LoadFromFile(FullPath))
			{
				LoadedConfigs.Add(FullPath, NewConfig);

				if (Current.IsValid())
				{
					NewConfig->SetParent(Current);
				}

				Current = NewConfig;
			}
		}
	}

	FString FinalPath = FPaths::Combine(SearchDirectories.Last(), ConfigNameString) + TEXT(".json");

	const TSharedPtr<FEditorConfig>* FinalConfig = LoadedConfigs.Find(FinalPath);
	if (FinalConfig != nullptr)
	{
		return *FinalConfig;
	}

	// no config in the last search directory, create one now
	// this will be the config that changes are written to
	TSharedPtr<FEditorConfig> NewConfig = MakeShared<FEditorConfig>();
	if (Current.IsValid())
	{
		// parent to the previous config
		NewConfig->SetParent(Current);
	}

	LoadedConfigs.Add(FinalPath, NewConfig);

	return NewConfig;
}

void UEditorConfigSubsystem::SaveConfig(TSharedPtr<FEditorConfig> Config, FOnCompletedDelegate OnCompleted)
{
	const FString* FilePath = LoadedConfigs.FindKey(Config);
	if (FilePath == nullptr)
	{
		return;
	}

	SaveLock.WriteLock();
	ON_SCOPE_EXIT { SaveLock.WriteUnlock(); };

	for (const FPendingSave& Save : PendingSaves)
	{
		if (Save.Config == Config)
		{
			// save already pending
			return;
		}
	}

	FString File = *FilePath;

	FPendingSave& Save = PendingSaves.AddDefaulted_GetRef();
	Save.Config = Config;
	Save.FileName = File;
	Save.OnCompleted = OnCompleted;
	Save.WasSuccess = Async(EAsyncExecution::Thread, [Config, File]()
		{
			return Config->SaveToFile(File);
		}, 
		[this, Config]() 
		{
			OnSaveCompleted(Config);
		});
}

void UEditorConfigSubsystem::OnSaveCompleted(TSharedPtr<FEditorConfig> Config)
{
	SaveLock.WriteLock();
	ON_SCOPE_EXIT { SaveLock.WriteUnlock(); };

	int32 Index = PendingSaves.IndexOfByPredicate([Config](const FPendingSave& Element)
		{
			return Element.Config == Config;
		});

	if (Index != INDEX_NONE)
	{
		const FPendingSave& PendingSave = PendingSaves[Index];
		PendingSave.OnCompleted.ExecuteIfBound(PendingSave.WasSuccess.Get());

		PendingSaves.RemoveAt(Index);
	}
}

void UEditorConfigSubsystem::AddSearchDirectory(FStringView SearchDir)
{
	SearchDirectories.AddUnique(FString(SearchDir));
}