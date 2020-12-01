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

	DestTexure->PreEditChange(nullptr); // Ensures synchronization with TextureCompilingManager.
	DestTexure->LODGroup = GetDefault<UWaterEditorSettings>()->TextureGroupForGeneratedTextures;
	DestTexure->MipGenSettings = TMGS_NoMipmaps;
	DestTexure->MaxTextureSize = GetDefault<UWaterEditorSettings>()->MaxWaterVelocityAndHeightTextureSize;
	SrcRenderTarget->UpdateTexture2D(DestTexure, TextureFormat, TextureFlags);
	DestTexure->PostEditChange();
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
	
	TActorIterator<AWaterMeshActor> MeshActorIt(World);
	AWaterMeshActor* MeshActor = MeshActorIt ? *MeshActorIt : nullptr;
	if (MeshActor)
	{
		FoundMeshActor = MeshActor;
		if (SourceVelocityTarget)
		{
			UTexture2D* PreviousTexture = FoundMeshActor->WaterVelocityTexture;
			UpdateSingleTexture(FoundMeshActor->WaterVelocityTexture, SourceVelocityTarget, FoundMeshActor, TEXT("WaterVelocityTexture"));

			// The water bodies' material instances are referencing the water velocity texture so they need to be in sync : 
			if (FoundMeshActor->WaterVelocityTexture != PreviousTexture)
			{
				for (TActorIterator<AWaterBody> WaterBodyActorIt(World); WaterBodyActorIt; ++WaterBodyActorIt)
				{
					if (AWaterBody* WaterBody = WaterBodyActorIt ? *WaterBodyActorIt : nullptr)
					{
						WaterBody->UpdateMaterialInstances();
					}
				}
			}

			OutWaterVelocityTexture = FoundMeshActor->WaterVelocityTexture;
		}
	}
}

#undef LOCTEXT_NAMESPACE
