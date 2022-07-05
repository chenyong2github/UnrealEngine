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
		if(!Streamer.IsValid())
		{
			return;
		}

		Streamer->SetVideoInput(FPixelStreamingVideoInputViewport::Create());
    }

    void FPixelStreamingEditorModule::StartStreaming()
    {   
        IPixelStreamingModule::Get().ForEachStreamer([](TSharedPtr<IPixelStreamingStreamer> Streamer)
		{
            if(!Streamer.IsValid())
            {
                return;
            }
            
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
			TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
			if (ActiveLevelViewport.IsValid())
			{
				FLevelEditorViewportClient& LevelViewportClient = ActiveLevelViewport->GetLevelViewportClient();
				FSceneViewport* SceneViewport = static_cast<FSceneViewport*>(LevelViewportClient.Viewport);
				Streamer->SetTargetViewport(SceneViewport);
				Streamer->SetTargetWindow(SceneViewport->FindWindow());
			}
            Streamer->StartStreaming();
		});
    }

	void FPixelStreamingEditorModule::StopStreaming()
	{
		IPixelStreamingModule::Get().ForEachStreamer([](TSharedPtr<IPixelStreamingStreamer> Streamer)
		{
            if(!Streamer.IsValid())
			{
				return;
			}
			Streamer->StopStreaming();
			Streamer->SetTargetViewport(nullptr);
		});

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