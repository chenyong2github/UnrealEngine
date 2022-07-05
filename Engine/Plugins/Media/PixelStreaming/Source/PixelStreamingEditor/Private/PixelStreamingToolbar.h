// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "IPixelStreamingModule.h"

namespace UE::PixelStreaming
{
    class FPixelStreamingToolbar
    {
        public:
            FPixelStreamingToolbar();
            virtual ~FPixelStreamingToolbar();
            void StartStreaming();
            void StopStreaming();
            static TSharedRef<SWidget> GeneratePixelStreamingMenuContent(TSharedPtr<FUICommandList> InCommandList);
            static FText GetActiveViewportName();
            static const FSlateBrush* GetActiveViewportIcon();
        
        private:
            void RegisterMenus();
            TSharedPtr<class FUICommandList> PluginCommands;

            IPixelStreamingModule& PixelStreamingModule;
    };
}
