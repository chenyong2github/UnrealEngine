// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "TextureEditorSettings.h"


UTextureEditorSettings::UTextureEditorSettings( const FObjectInitializer& ObjectInitializer )
	: Super(ObjectInitializer)
	, Background(TextureEditorBackground_Checkered)
	, BackgroundColor(FColor::FromHex("242424FF"))
	, CheckerColorOne(FColor(128, 128, 128))
	, CheckerColorTwo(FColor(64, 64, 64))
	, CheckerSize(16)
	, FitToViewport(true)
	, ZoomMode(ETextureEditorZoomMode::Fit)
	, TextureBorderColor(FColor::White)
	, TextureBorderEnabled(true)
{ }
