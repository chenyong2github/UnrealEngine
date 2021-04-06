// Copyright Epic Games, Inc. All Rights Reserved.

#include "ILensDistortion.h"

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "LensDistortionLog.h"
#include "LensDistortionSettings.h"
#include "LensDistortionSubsystem.h"
#include "LensFile.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Models/SphericalLensModel.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"
#include "UObject/UObjectGlobals.h"


DEFINE_LOG_CATEGORY(LogLensDistortion);


static TAutoConsoleVariable<FString> CVarLensDistortionStartupLensFile(
	TEXT("LensDistortion.StartupLensFile"),
	TEXT(""),
	TEXT("Startup Lens File\n"),
	ECVF_ReadOnly
);


class FLensDistortion : public ILensDistortion
{
public:
	
	//~ Begin IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface interface

	void ApplyStartupLensFile();
	void RegisterDistortionModels();
	void UnregisterDistortionModels();

private:
	
	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_MODULE( FLensDistortion, LensDistortion )


void FLensDistortion::StartupModule()
{
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("LensDistortion"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/LensDistortion"), PluginShaderDir);

	ApplyStartupLensFile();
	RegisterDistortionModels();
}


void FLensDistortion::ShutdownModule()
{
	UnregisterDistortionModels();

	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
	}
}

void FLensDistortion::ApplyStartupLensFile()
{
	auto ApplyLensFile = [this]()
	{
		ULensFile* StartupLensFile = nullptr;

		// Try to load from CVar
		{
			const FString LensFileName = CVarLensDistortionStartupLensFile.GetValueOnGameThread();

			if (LensFileName.Len())
			{
				if (UObject* Object = StaticLoadObject(ULensFile::StaticClass(), nullptr, *LensFileName))
				{
					StartupLensFile = CastChecked<ULensFile>(Object);
				}

				if (StartupLensFile)
				{
					UE_LOG(LogLensDistortion, Display, TEXT("Loading Lens File specified in CVar LensDistortion.StartupLensFile: '%s'"), *LensFileName);
				}
			}
		}

#if WITH_EDITOR
		// Try to load user settings
		if (StartupLensFile == nullptr)
		{
			StartupLensFile = GetDefault<ULensDistortionEditorSettings>()->GetUserLensFile();
			if (StartupLensFile)
			{
				UE_LOG(LogLensDistortion, Display, TEXT("Loading Lens File specified in user settings: '%s'"), *StartupLensFile->GetName());
			}
		}
#endif

		// Try loading default lens file from project settings
		if (StartupLensFile == nullptr)
		{
			StartupLensFile = GetDefault<ULensDistortionSettings>()->GetStartupLensFile();

			if (StartupLensFile)
			{
				UE_LOG(LogLensDistortion, Display, TEXT("Loading Lens File specified in project settings: '%s'"), *StartupLensFile->GetName());
			}
		}

		ULensDistortionSubsystem* SubSystem = GEngine->GetEngineSubsystem<ULensDistortionSubsystem>();
		check(SubSystem);
		SubSystem->SetDefaultLensFile(StartupLensFile);
	};

	if (FApp::CanEverRender())
	{
		if (GEngine && GEngine->IsInitialized())
		{
			ApplyLensFile();
		}
		else
		{
			PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddLambda(ApplyLensFile);
		}
	}
}

void FLensDistortion::RegisterDistortionModels()
{
	auto RegisterModels = [this]()
	{
		// Register all lens models defined in this module
		ULensDistortionSubsystem* SubSystem = GEngine->GetEngineSubsystem<ULensDistortionSubsystem>();
		SubSystem->RegisterDistortionModel(USphericalLensModel::StaticClass());
	};

	if (FApp::CanEverRender())
	{
		if (GEngine && GEngine->IsInitialized())
		{
			RegisterModels();
		}
		else
		{
			PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddLambda(RegisterModels);
		}
	}
}

void FLensDistortion::UnregisterDistortionModels()
{
	if (GEngine)
	{
		// Unregister all lens models defined in this module
		if (ULensDistortionSubsystem* SubSystem = GEngine->GetEngineSubsystem<ULensDistortionSubsystem>())
		{
			SubSystem->UnregisterDistortionModel(USphericalLensModel::StaticClass());
		}
	}
}