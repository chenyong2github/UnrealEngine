// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRuntimeBlueprintLibrary.h"

#include "DatasmithRuntime.h"

#include "DatasmithSceneFactory.h"
#include "DatasmithTranslatableSource.h"
#include "DatasmithTranslator.h"
#include "DirectLink/SceneIndex.h"
#include "IDatasmithSceneElements.h"

#if WITH_EDITOR
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#endif
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Widgets/SWindow.h"

bool UDatasmithRuntimeLibrary::LoadDatasmithScene(ADatasmithRuntimeActor* DatasmithRuntimeActor, const FString& FilePath)
{
	if (DatasmithRuntimeActor == nullptr)
	{
		return false;
	}

	if( !FPaths::FileExists( FilePath ) )
	{
		return false;
	}

	FDatasmithSceneSource Source;
	Source.SetSourceFile(FilePath);

	TUniquePtr< FDatasmithTranslatableSceneSource > TranslatableSourcePtr = MakeUnique< FDatasmithTranslatableSceneSource >(Source);
	if (!TranslatableSourcePtr->IsTranslatable())
	{
		return false;
	}

	// Set all import options to defaults for DatasmithRuntime
	TSharedPtr< IDatasmithTranslator > TranslatorPtr = TranslatableSourcePtr->GetTranslator();
	if (IDatasmithTranslator* Translator = TranslatorPtr.Get())
	{
		static FDatasmithTessellationOptions DefaultTessellationOptions(0.3f, 0.0f, 30.0f, EDatasmithCADStitchingTechnique::StitchingSew);

		TArray< TStrongObjectPtr<UDatasmithOptionsBase> > Options;
		Translator->GetSceneImportOptions(Options);

		bool bUpdateOptions = false;
		for (TStrongObjectPtr<UDatasmithOptionsBase>& ObjectPtr : Options)
		{
			if (UDatasmithCommonTessellationOptions* TessellationOption = Cast< UDatasmithCommonTessellationOptions >(ObjectPtr.Get()))
			{
				bUpdateOptions = true;
				TessellationOption->Options = DefaultTessellationOptions;
			}
		}

		if (bUpdateOptions == true)
		{
			Translator->SetSceneImportOptions(Options);
		}

		// Reset scene element
		FString SceneName = FPaths::GetBaseFilename(FilePath);
		TSharedRef< IDatasmithScene > SceneElement = FDatasmithSceneFactory::CreateScene(*SceneName);

		// Fill up scene element with content of source file
		if (!TranslatableSourcePtr->Translate( SceneElement ))
		{
			return false;
		}

		DirectLink::FIndexedScene IndexedScene(&SceneElement.Get());

		DatasmithRuntimeActor->SetScene(SceneElement);

		return true;
	}

	return false;
}


void UDatasmithRuntimeLibrary::LoadDatasmithSceneFromExplorer(ADatasmithRuntimeActor* DatasmithRuntimeActor, const FString& DefaultPath, const FString& FileTypes)
{
	if (DatasmithRuntimeActor == nullptr)
	{
		return;
	}

	if (GEngine && GEngine->GameViewport)
	{
#if WITH_EDITOR
		TArray<FString> OutFileNames;

		void* ParentWindowHandle = GEngine->GameViewport->GetWindow()->GetNativeWindow()->GetOSWindowHandle();
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (DesktopPlatform)
		{
			//Opening the file picker!
			uint32 SelectionFlag = 0; //A value of 0 represents single file selection while a value of 1 represents multiple file selection
			DesktopPlatform->OpenFileDialog(ParentWindowHandle, TEXT("Choose A File"), DefaultPath, FString(""), FileTypes, SelectionFlag, OutFileNames);
		}

		if (OutFileNames.Num() > 0)
		{
			LoadDatasmithScene( DatasmithRuntimeActor, OutFileNames[0]);
		}
#endif
	}
}

void UDatasmithRuntimeLibrary::ResetActor(ADatasmithRuntimeActor* DatasmithRuntimeActor)
{
	if (DatasmithRuntimeActor)
	{
		DatasmithRuntimeActor->Reset();
	}
}
