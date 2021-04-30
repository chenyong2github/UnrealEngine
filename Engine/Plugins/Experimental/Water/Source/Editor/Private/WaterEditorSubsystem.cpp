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
#include "WaterModule.h"
#include "Modules/ModuleManager.h"

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

	DestTexure->Modify();
	DestTexure->LODGroup = GetDefault<UWaterEditorSettings>()->TextureGroupForGeneratedTextures;
	DestTexure->MipGenSettings = TMGS_NoMipmaps;
	DestTexure->MaxTextureSize = GetDefault<UWaterEditorSettings>()->MaxWaterVelocityAndHeightTextureSize;
	SrcRenderTarget->UpdateTexture2D(DestTexure, TextureFormat, TextureFlags);
	DestTexure->PostEditChange();
}

UWaterEditorSubsystem::UWaterEditorSubsystem()
{
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinderOptional<UTexture2D> DefaultWaterActorSprite;
		ConstructorHelpers::FObjectFinderOptional<UTexture2D> ErrorSprite;

		FConstructorStatics()
			: DefaultWaterActorSprite(TEXT("/Water/Icons/WaterSprite"))
			, ErrorSprite(TEXT("/Water/Icons/WaterErrorSprite"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	DefaultWaterActorSprite = ConstructorStatics.DefaultWaterActorSprite.Get();
	ErrorSprite = ConstructorStatics.ErrorSprite.Get();
}

void UWaterEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LandscapeMaterialParameterCollection = GetDefault<UWaterEditorSettings>()->LandscapeMaterialParameterCollection.LoadSynchronous();

	IWaterModuleInterface& WaterModule = FModuleManager::GetModuleChecked<IWaterModuleInterface>("Water");
	WaterModule.SetWaterEditorServices(this);
}

void UWaterEditorSubsystem::Deinitialize()
{
	IWaterModuleInterface& WaterModule = FModuleManager::GetModuleChecked<IWaterModuleInterface>("Water");
	if (IWaterEditorServices* WaterEditorServices = WaterModule.GetWaterEditorServices())
	{
		if (WaterEditorServices == this)
		{
			WaterModule.SetWaterEditorServices(nullptr);
		}
	}

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

void UWaterEditorSubsystem::RegisterWaterActorSprite(UClass* InClass, UTexture2D* Texture)
{
	WaterActorSprites.Add(InClass, Texture);
}

UTexture2D* UWaterEditorSubsystem::GetWaterActorSprite(UClass* InClass) const
{
	UClass const* Class = InClass;
	UTexture2D* const* SpritePtr = nullptr;

	// Traverse the class hierarchy and find the first available sprite
	while (Class != nullptr && SpritePtr == nullptr)
	{
		SpritePtr = WaterActorSprites.Find(Class);
		Class = Class->GetSuperClass();
	}

	if (SpritePtr != nullptr)
	{
		return *SpritePtr;
	}

	return DefaultWaterActorSprite;
}

#undef LOCTEXT_NAMESPACE
