// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingModule.h"

namespace UE::EditorPixelStreaming
{
    class FPixelStreamingToolbar;
}

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
    void InitEditorStreaming(IPixelStreamingModule& Module);
	bool ParseResolution(const TCHAR* InResolution, uint32& OutX, uint32& OutY);
    void MaybeResizeEditor(TSharedPtr<SWindow> RootWindow);

    TSharedPtr<UE::EditorPixelStreaming::FPixelStreamingToolbar> Toolbar;
    static FPixelStreamingEditorModule* PixelStreamingEditorModule;	
};
