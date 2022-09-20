// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingEditorModule.h"
#include "Editor/EditorPerformanceSettings.h"
#include "PixelStreamingToolbar.h"
#include "IPixelStreamingModule.h"
#include "PixelStreamingStyle.h"
#include "IPixelStreamingStreamer.h"
#include "PixelStreamingVideoInputBackBufferComposited.h"
#include "Settings.h"
#include "UnrealEngine.h"
#include "Interfaces/IMainFrameModule.h"
#include "PixelStreamingProtocol.h"
#include "Utils.h"

FPixelStreamingEditorModule* FPixelStreamingEditorModule::PixelStreamingEditorModule = nullptr;

/**
 * IModuleInterface implementation
 */
void FPixelStreamingEditorModule::StartupModule()
{
	using namespace UE::EditorPixelStreaming;
	// Initialize the editor toolbar
	FPixelStreamingStyle::Initialize();
	FPixelStreamingStyle::ReloadTextures();
	Toolbar = MakeShared<FPixelStreamingToolbar>();

	// Update editor settings so that editor won't slow down if not in focus
	UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
	Settings->bThrottleCPUWhenNotForeground = false;
	Settings->PostEditChange();

	Settings::InitialiseSettings();
		
	IPixelStreamingModule& Module = IPixelStreamingModule::Get();
	Module.OnReady().AddRaw(this, &FPixelStreamingEditorModule::InitEditorStreaming);
}

void FPixelStreamingEditorModule::ShutdownModule()
{
	StopStreaming();
}

void FPixelStreamingEditorModule::InitEditorStreaming(IPixelStreamingModule& Module)
{
	TSharedPtr<IPixelStreamingStreamer> Streamer = Module.GetStreamer(Module.GetDefaultStreamerID());
	if (!Streamer.IsValid())
	{
		return;
	}

	// The current handler is the function that will currently be executed if a message with type "Command" is received
	TFunction<void(FMemoryReader)> CurrentHandler = Module.FindMessageHandler("Command");
	TFunction<void(FMemoryReader)> ExtendedHandler = [CurrentHandler](FMemoryReader Ar)
	{
		// We then create our new handler which will execute the "base" handler
		CurrentHandler(Ar);
		// and then perform out extended functionality after.
		// equivalent to the super::DoSomeFunc pattern
		FString Res;
		Res.GetCharArray().SetNumUninitialized(Ar.TotalSize() / 2 + 1);
		Ar.Serialize(Res.GetCharArray().GetData(), Ar.TotalSize());
		FString Descriptor = Res.Mid(1);
		FString WidthString;
		FString HeightString;
		bool bSuccess;
		UE::PixelStreaming::ExtractJsonFromDescriptor(Descriptor, TEXT("Resolution.Width"), WidthString, bSuccess);
		if (bSuccess)
		{
			UE::PixelStreaming::ExtractJsonFromDescriptor(Descriptor, TEXT("Resolution.Height"), HeightString, bSuccess);
			if (bSuccess)
			{
				int Width = FCString::Atoi(*WidthString);
				int Height = FCString::Atoi(*HeightString);
				if (Width < 1 || Height < 1)
				{
					return;
				}

				TSharedPtr<SWindow> ParentWindow = IMainFrameModule::Get().GetParentWindow();
				ParentWindow->Resize(FVector2D(Width, Height));
				FSlateApplication::Get().OnSizeChanged(ParentWindow->GetNativeWindow().ToSharedRef(), Width, Height);
			}
		}
	};
	Module.RegisterMessage( Protocol::EPixelStreamingMessageDirection::ToStreamer, 
							"Command", 
							Protocol::FPixelStreamingInputMessage(51, 0, {}),
							[ExtendedHandler](FMemoryReader Ar) {
								ExtendedHandler(Ar);
							});

	// Give the editor streamer the default url if the user hasn't specified one when launching the editor
	if (Streamer->GetSignallingServerURL().IsEmpty())
	{
		Streamer->SetSignallingServerURL(Module.GetDefaultSignallingURL());
	}

	IMainFrameModule::Get().OnMainFrameCreationFinished().AddLambda([&](TSharedPtr<SWindow> RootWindow, bool bIsRunningStartupDialog)
	{
		MaybeResizeEditor(RootWindow);

		// We don't want to show tooltips in render off screen as they're currently broken
		bool bIsRenderingOffScreen = FParse::Param(FCommandLine::Get(), TEXT("RenderOffScreen"));
		FSlateApplication::Get().SetAllowTooltips(!bIsRenderingOffScreen);

		if(UE::EditorPixelStreaming::Settings::CVarEditorPixelStreamingStartOnLaunch.GetValueOnAnyThread())
		{
			StartStreaming();
		}
	});
}

void FPixelStreamingEditorModule::StartStreaming()
{
	// Activate our level editor streamer
	IPixelStreamingModule& Module = IPixelStreamingModule::Get();
	TSharedPtr<IPixelStreamingStreamer> Streamer = Module.GetStreamer(Module.GetDefaultStreamerID());
	if (!Streamer.IsValid())
	{
		return;
	}

	// The streamer doesn't currently have a video input so create one
	TWeakPtr<FPixelStreamingVideoInput> VideoInput = Streamer->GetVideoInput();
	if(!VideoInput.Pin())
	{
		Streamer->SetVideoInput(FPixelStreamingVideoInputBackBufferComposited::Create());
	}

	// Use the Pixel Streaming module's start streaming method to start all streamers
	Module.StartStreaming();
}

void FPixelStreamingEditorModule::StopStreaming()
{
	IPixelStreamingModule& Module = IPixelStreamingModule::Get();
	TSharedPtr<IPixelStreamingStreamer> Streamer = Module.GetStreamer(Module.GetDefaultStreamerID());
	if (!Streamer.IsValid())
	{
		return;
	}

	Streamer->SetTargetViewport(nullptr);
	// Use the Pixel Streaming module's stop streaming method to stop all streamers
	Module.StopStreaming();
}

FPixelStreamingEditorModule* FPixelStreamingEditorModule::GetModule()
{
	if (PixelStreamingEditorModule)
	{
		return PixelStreamingEditorModule;
	}
	FPixelStreamingEditorModule* Module = FModuleManager::Get().LoadModulePtr<FPixelStreamingEditorModule>("PixelStreamingEditor");
	if (Module)
	{
		PixelStreamingEditorModule = Module;
	}
	return PixelStreamingEditorModule;
}

bool FPixelStreamingEditorModule::ParseResolution(const TCHAR* InResolution, uint32& OutX, uint32& OutY)
{
	if(*InResolution)
	{
		FString CmdString(InResolution);
		CmdString = CmdString.TrimStartAndEnd().ToLower();

		// Retrieve the X dimensional value
		const uint32 X = FMath::Max(FCString::Atof(*CmdString), 0.0f);

		// Determine whether the user has entered a resolution and extract the Y dimension.
		FString YString;

		// Find separator between values (Example of expected format: 1280x768)
		const TCHAR* YValue = NULL;
		if(FCString::Strchr(*CmdString,'x'))
		{
			YValue = const_cast<TCHAR*> (FCString::Strchr(*CmdString,'x')+1);
			YString = YValue;
			// Remove any whitespace from the end of the string
			YString = YString.TrimStartAndEnd();
		}

		// If the Y dimensional value exists then setup to use the specified resolution.
		uint32 Y = 0;
		if ( YValue && YString.Len() > 0 )
		{
			if (YString.IsNumeric())
			{
				Y = FMath::Max(FCString::Atof(YValue), 0.0f);
				OutX = X;
				OutY = Y;
				return true;
			}
		}
	}
	return false;
}

void FPixelStreamingEditorModule::MaybeResizeEditor(TSharedPtr<SWindow> RootWindow)
{
	uint32 ResolutionX, ResolutionY = 0;
	FString ResolutionStr;
	bool bSuccess = FParse::Value(FCommandLine::Get(), TEXT("EditorPixelStreamingRes="), ResolutionStr);
	if (bSuccess)
	{
		bSuccess = ParseResolution(*ResolutionStr, ResolutionX, ResolutionY);
	}
	else
	{
		bool UserSpecifiedWidth = FParse::Value(FCommandLine::Get(), TEXT("EditorPixelStreamingResX="), ResolutionX);
		bool UserSpecifiedHeight = FParse::Value(FCommandLine::Get(), TEXT("EditorPixelStreamingResY="), ResolutionY);
		bSuccess = UserSpecifiedWidth | UserSpecifiedHeight;

		const float AspectRatio = 16.0 / 9.0;
		if (UserSpecifiedWidth && !UserSpecifiedHeight)
		{
			ResolutionY = int32(ResolutionX / AspectRatio);
		}
		else if (UserSpecifiedHeight && !UserSpecifiedWidth)
		{
			ResolutionX = int32(ResolutionY * AspectRatio);
		}
	}

	if(bSuccess)
	{
		RootWindow->Resize(FVector2D(ResolutionX, ResolutionY));
		FSlateApplication::Get().OnSizeChanged(RootWindow->GetNativeWindow().ToSharedRef(), ResolutionX, ResolutionY);
	}
}

IMPLEMENT_MODULE(FPixelStreamingEditorModule, PixelStreamingEditor)