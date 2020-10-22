// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterEditorSubsystem.h"
#include "WaterBodyActor.h"
#include "EngineUtils.h"
#include "WaterMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Editor.h"
#include "ISettingsModule.h"
#include "WaterEditorSettings.h"
#include "HAL/IConsoleManager.h"
#include "Editor/UnrealEdEngine.h"
#include "WaterSplineComponentVisualizer.h"
#include "WaterSplineComponent.h"
#include "UnrealEdGlobals.h"
#include "WaterSubsystem.h"
#include "WaterMeshComponent.h"
#include "LevelEditorViewport.h"
#include "Materials/MaterialParameterCollection.h"

#define LOCTEXT_NAMESPACE "WaterEditorSubsystem"

void UpdateSingleTexture(UTexture2D*& DestTexure, UTextureRenderTarget2D* SrcRenderTarget, UObject* Outer, const TCHAR* TextureName)
{
	uint32 TextureFlags = CTF_Default;
	if (!DestTexure)
	{
		DestTexure = SrcRenderTarget->ConstructTexture2D(Outer, TextureName, RF_NoFlags, TextureFlags);
	}
	else
	{
		const EPixelFormat PixelFormat = SrcRenderTarget->GetFormat();
		ETextureSourceFormat TextureFormat = TSF_Invalid;
		switch (PixelFormat)
		{
		case PF_B8G8R8A8:
			TextureFormat = TSF_BGRA8;
			break;
		case PF_FloatRGBA:
			TextureFormat = TSF_RGBA16F;
			break;
		}

		SrcRenderTarget->UpdateTexture2D(DestTexure, TextureFormat, TextureFlags);
		{
			DestTexure->LODGroup = GetDefault<UWaterEditorSettings>()->TextureGroupForGeneratedTextures;
			DestTexure->MipGenSettings = TMGS_NoMipmaps;
			DestTexure->MaxTextureSize = GetDefault<UWaterEditorSettings>()->MaxWaterVelocityAndHeightTextureSize;
			DestTexure->PostEditChange();
		}
	}
}

void UWaterEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LandscapeMaterialParameterCollection = GetDefault<UWaterEditorSettings>()->LandscapeMaterialParameterCollection.LoadSynchronous();
}

void UWaterEditorSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UWaterEditorSubsystem::UpdateWaterTextures(
	UWorld* World, 
	UTextureRenderTarget2D* SourceVelocityTarget, 
	UTexture2D*& OutWaterVelocityTexture)
{
	AWaterMeshActor* FoundMeshActor = nullptr;
	
	TActorIterator<AWaterMeshActor> It(World);
	AWaterMeshActor* MeshActor = It ? *It : nullptr;
	if (MeshActor)
	{
		FoundMeshActor = MeshActor;
		if (SourceVelocityTarget)
		{
			UpdateSingleTexture(FoundMeshActor->WaterVelocityTexture, SourceVelocityTarget, FoundMeshActor, TEXT("WaterVelocityTexture"));

			OutWaterVelocityTexture = FoundMeshActor->WaterVelocityTexture;
		}
	}
}

#undef LOCTEXT_NAMESPACE
