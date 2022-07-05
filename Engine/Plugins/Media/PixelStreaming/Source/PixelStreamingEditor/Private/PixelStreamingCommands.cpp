// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingCommands.h"

namespace UE::PixelStreaming
{
#define LOCTEXT_NAMESPACE "PixelStreamingToolBar"
    void FPixelStreamingCommands::RegisterCommands()
    {
        UI_COMMAND(StartStreaming, "Start Streaming", "Start Streaming", EUserInterfaceActionType::Button, FInputChord());
        UI_COMMAND(StopStreaming, "Stop Streaming", "Stop Streaming", EUserInterfaceActionType::Button, FInputChord());
        UI_COMMAND(VP8, "VP8", "VP8", EUserInterfaceActionType::Check, FInputChord());
        UI_COMMAND(VP9, "VP9", "VP9", EUserInterfaceActionType::Check, FInputChord());
        UI_COMMAND(H264, "H264", "H264", EUserInterfaceActionType::Check, FInputChord());
    }
#undef LOCTEXT_NAMESPACE
}