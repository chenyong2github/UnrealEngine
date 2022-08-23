// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingEditorModule.h"
#include "SLevelViewport.h"
#include "Slate/SceneViewport.h"
#include "LevelEditorViewport.h"
#include "Editor/EditorPerformanceSettings.h"
#include "PixelStreamingToolbar.h"
#include "PixelStreamingVideoInputViewport.h"
#include "IPixelStreamingModule.h"
#include "PixelStreamingStyle.h"
#include "IPixelStreamingStreamer.h"
#include "Math/Vector4.h"
#include "Math/Matrix.h"
#include "Math/TransformNonVectorized.h"
#include "Serialization/MemoryReader.h"
#include "PixelStreamingPlayerId.h"
#include "Kismet/KismetMathLibrary.h"
#include "PixelStreamingVideoInputBackBuffer.h"
#include "PixelStreamingVideoInputBackBufferComposited.h"
#include "Settings.h"

UE::PixelStreaming::FPixelStreamingEditorModule* UE::PixelStreaming::FPixelStreamingEditorModule::PixelStreamingEditorModule = nullptr;

namespace UE::PixelStreaming
{
	/**
	 * IModuleInterface implementation
	 */
	void FPixelStreamingEditorModule::StartupModule()
	{
		// Initialize the editor toolbar
		FPixelStreamingStyle::Initialize();
		FPixelStreamingStyle::ReloadTextures();
		Toolbar = MakeShared<FPixelStreamingToolbar>();

		// Update editor settings so that editor won't slow down if not in focus
		UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
		Settings->bThrottleCPUWhenNotForeground = false;
		Settings->PostEditChange();

		Settings::Editor::InitialiseSettings();
		
		IPixelStreamingModule& Module = IPixelStreamingModule::Get();
		Module.OnReady().AddRaw(this, &FPixelStreamingEditorModule::InitEditorStreamer);
	}

	void FPixelStreamingEditorModule::ShutdownModule()
	{
		StopStreaming();
	}

	void FPixelStreamingEditorModule::InitEditorStreamer(IPixelStreamingModule& Module)
	{
		TSharedPtr<IPixelStreamingStreamer> Streamer = Module.GetStreamer(Module.GetDefaultStreamerID());
		if (!Streamer.IsValid())
		{
			return;
		}

		Streamer->SetVideoInput(FPixelStreamingVideoInputBackBufferComposited::Create());
		// Give the editor streamer the default url if the user hasn't specified one when launching the editor
		if (Streamer->GetSignallingServerURL().IsEmpty())
		{
			Streamer->SetSignallingServerURL(Module.GetDefaultSignallingURL());
		}

		if(Settings::Editor::CVarEditorPixelStreamingStartOnLaunch.GetValueOnAnyThread())
		{
			StartStreaming();
		}
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

		// use the modules start streaming method to start all streamers
		Module.StartStreaming();
	}

	void FPixelStreamingEditorModule::StopStreaming()
	{
		// De-activate our level editor streamer
		IPixelStreamingModule& Module = IPixelStreamingModule::Get();
		TSharedPtr<IPixelStreamingStreamer> Streamer = Module.GetStreamer(Module.GetDefaultStreamerID());
		if (!Streamer.IsValid())
		{
			return;
		}
		Streamer->SetTargetViewport(nullptr);
		// use the modules stop streaming method to stop all streamers
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
} // namespace UE::PixelStreaming

IMPLEMENT_MODULE(UE::PixelStreaming::FPixelStreamingEditorModule, PixelStreamingEditor)