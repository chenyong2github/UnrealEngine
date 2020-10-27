// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRuntimeBlueprintLibrary.h"

#include "DatasmithRuntime.h"
#include "DirectLinkUtils.h"

#include "DatasmithSceneFactory.h"
#include "DatasmithTranslatableSource.h"
#include "DatasmithTranslator.h"
#include "IDatasmithSceneElements.h"
#include "DirectLinkSceneSnapshot.h"

#if WITH_EDITOR
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#elif PLATFORM_WINDOWS
#include "HAL/FileManager.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/COMPointer.h"
#include <commdlg.h>
#include <shlobj.h>
#include <Winver.h>
#include "Windows/HideWindowsPlatformTypes.h"
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

	DatasmithRuntimeActor->CloseConnection();

	DatasmithRuntimeActor->OnOpenDelta();


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

		DirectLink::BuildIndexForScene(&SceneElement.Get());

		DatasmithRuntimeActor->OnCloseDelta();

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
		TArray<FString> OutFilenames;

#if WITH_EDITOR
		void* ParentWindowHandle = GEngine->GameViewport->GetWindow()->GetNativeWindow()->GetOSWindowHandle();
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (DesktopPlatform)
		{
			//Opening the file picker!
			uint32 SelectionFlag = 0; //A value of 0 represents single file selection while a value of 1 represents multiple file selection
			DesktopPlatform->OpenFileDialog(ParentWindowHandle, TEXT("Choose A File"), DefaultPath, FString(""), FileTypes, SelectionFlag, OutFilenames);
		}
#elif PLATFORM_WINDOWS
		TComPtr<IFileDialog> FileDialog;
		if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, IID_PPV_ARGS_Helper(&FileDialog))))
		{
			// Set up common settings
			FileDialog->SetTitle(TEXT("Choose A File"));
			if (!DefaultPath.IsEmpty())
			{
				// SHCreateItemFromParsingName requires the given path be absolute and use \ rather than / as our normalized paths do
				FString DefaultWindowsPath = FPaths::ConvertRelativePathToFull(DefaultPath);
				DefaultWindowsPath.ReplaceInline(TEXT("/"), TEXT("\\"), ESearchCase::CaseSensitive);

				TComPtr<IShellItem> DefaultPathItem;
				if (SUCCEEDED(::SHCreateItemFromParsingName(*DefaultWindowsPath, nullptr, IID_PPV_ARGS(&DefaultPathItem))))
				{
					FileDialog->SetFolder(DefaultPathItem);
				}
			}

			// Set-up the file type filters
			TArray<FString> UnformattedExtensions;
			TArray<COMDLG_FILTERSPEC> FileDialogFilters;
			{
				const FString DefaultFileTypes = TEXT("Datasmith Scene (*.udatasmith)|*.udatasmith");
				DefaultFileTypes.ParseIntoArray(UnformattedExtensions, TEXT("|"), true);

				if (UnformattedExtensions.Num() % 2 == 0)
				{
					FileDialogFilters.Reserve(UnformattedExtensions.Num() / 2);
					for (int32 ExtensionIndex = 0; ExtensionIndex < UnformattedExtensions.Num();)
					{
						COMDLG_FILTERSPEC& NewFilterSpec = FileDialogFilters[FileDialogFilters.AddDefaulted()];
						NewFilterSpec.pszName = *UnformattedExtensions[ExtensionIndex++];
						NewFilterSpec.pszSpec = *UnformattedExtensions[ExtensionIndex++];
					}
				}
			}
			FileDialog->SetFileTypes(FileDialogFilters.Num(), FileDialogFilters.GetData());

			// Show the picker
			if (SUCCEEDED(FileDialog->Show(NULL)))
			{
				int32 OutFilterIndex = 0;
				if (SUCCEEDED(FileDialog->GetFileTypeIndex((UINT*)&OutFilterIndex)))
				{
					OutFilterIndex -= 1; // GetFileTypeIndex returns a 1-based index
				}

				auto AddOutFilename = [&OutFilenames](const FString& InFilename)
				{
					FString& OutFilename = OutFilenames[OutFilenames.Add(InFilename)];
					OutFilename = IFileManager::Get().ConvertToRelativePath(*OutFilename);
					FPaths::NormalizeFilename(OutFilename);
				};

				{
					IFileOpenDialog* FileOpenDialog = static_cast<IFileOpenDialog*>(FileDialog.Get());

					TComPtr<IShellItemArray> Results;
					if (SUCCEEDED(FileOpenDialog->GetResults(&Results)))
					{
						DWORD NumResults = 0;
						Results->GetCount(&NumResults);
						for (DWORD ResultIndex = 0; ResultIndex < NumResults; ++ResultIndex)
						{
							TComPtr<IShellItem> Result;
							if (SUCCEEDED(Results->GetItemAt(ResultIndex, &Result)))
							{
								PWSTR pFilePath = nullptr;
								if (SUCCEEDED(Result->GetDisplayName(SIGDN_FILESYSPATH, &pFilePath)))
								{
									AddOutFilename(pFilePath);
									::CoTaskMemFree(pFilePath);
								}
							}
						}
					}
				}
			}
		}
#endif

		if (OutFilenames.Num() > 0)
		{
			LoadDatasmithScene( DatasmithRuntimeActor, OutFilenames[0]);
		}
	}
}

void UDatasmithRuntimeLibrary::ResetActor(ADatasmithRuntimeActor* DatasmithRuntimeActor)
{
	if (DatasmithRuntimeActor)
	{
		DatasmithRuntimeActor->Reset();
	}
}

UDirectLinkProxy* UDatasmithRuntimeLibrary::GetDirectLinkProxy()
{
	return DatasmithRuntime::GetDirectLinkProxy();
}