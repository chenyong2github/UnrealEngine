// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingModule.h"

namespace UE::PixelStreaming
{
    class PIXELSTREAMINGEDITOR_API FPixelStreamingEditorModule : public IModuleInterface
    {
    public:
        /** IModuleInterface implementation */
        virtual void StartupModule() override;
        virtual void ShutdownModule() override;

        void StartStreaming();
        void StopStreaming();
        
        static FPixelStreamingEditorModule* GetModule();
    private:
        void InitEditorStreamer(IPixelStreamingModule& Module);

        TSharedPtr<class FPixelStreamingToolbar> Toolbar;

        static FPixelStreamingEditorModule* PixelStreamingEditorModule;	
    };
} // namespace UE::PixelStreaming
