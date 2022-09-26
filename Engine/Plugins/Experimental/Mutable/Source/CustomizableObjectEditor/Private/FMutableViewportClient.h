// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AdvancedPreviewScene.h"
#include "SEditorViewport.h"
#include "UnrealWidget.h"

/**
 * Super simple viewport client designed to work SMutableMeshViewport
 */
class FMutableMeshViewportClient : public FEditorViewportClient
{
public:
	FMutableMeshViewportClient(const TSharedRef<FAdvancedPreviewScene>& InPreviewScene);
};

