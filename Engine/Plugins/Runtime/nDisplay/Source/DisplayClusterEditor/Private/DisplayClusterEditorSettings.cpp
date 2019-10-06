// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterEditorSettings.h"
#include "DisplayClusterEditorEngine.h"
#include "Misc/ConfigCacheIni.h"


UDisplayClusterEditorSettings::UDisplayClusterEditorSettings(class FObjectInitializer const & ObjectInitializer)
	: Super(ObjectInitializer) 
{
	GET_MEMBER_NAME_CHECKED(UDisplayClusterEditorSettings, bEnabled);
}

#if WITH_EDITOR
void UDisplayClusterEditorSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property != nullptr)
	{
		static const FString DefaultEnginePath = FString::Printf(TEXT("%sDefaultEngine.ini"), *FPaths::SourceConfigDir());
		static const FString DefaultGamePath   = FString::Printf(TEXT("%sDefaultGame.ini"),   *FPaths::SourceConfigDir());

		FName PropertyName(PropertyChangedEvent.Property->GetFName());

		if (PropertyName == GET_MEMBER_NAME_CHECKED(UDisplayClusterEditorSettings, bEnabled))
		{
			if (bEnabled)
			{
				// DefaultEngine.ini
				GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("GameEngine"),                  TEXT("/Script/DisplayCluster.DisplayClusterGameEngine"),         DefaultEnginePath);
				GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("UnrealEdEngine"),              TEXT("/Script/DisplayClusterEditor.DisplayClusterEditorEngine"), DefaultEnginePath);
				GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("GameViewportClientClassName"), TEXT("/Script/DisplayCluster.DisplayClusterViewportClient"),     DefaultEnginePath);

				// DefaultGame.ini
				GConfig->SetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("bUseBorderlessWindow"), TEXT("True"), DefaultGamePath);
			}
			else
			{
				GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("GameEngine"),                  TEXT("/Script/Engine.GameEngine"),         DefaultEnginePath);
				GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("UnrealEdEngine"),              TEXT("/Script/UnrealEd.UnrealEdEngine"),   DefaultEnginePath);
				GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("GameViewportClientClassName"), TEXT("/Script/Engine.GameViewportClient"), DefaultEnginePath);

				// DefaultGame.ini
				GConfig->SetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("bUseBorderlessWindow"), TEXT("False"), DefaultGamePath);
			}

			GConfig->Flush(false, DefaultEnginePath);
			GConfig->Flush(false, DefaultGamePath);
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
