// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionMiniMap.h"
#include "Engine/Texture2D.h"
#include "Logging/MessageLog.h"
#include "Misc/MapErrors.h"
#include "Misc/UObjectToken.h"
#include "RenderUtils.h"
#include "RHI.h"

#define LOCTEXT_NAMESPACE "WorldPartitionEditor"

AWorldPartitionMiniMap::AWorldPartitionMiniMap(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.DoNotCreateDefaultSubobject(TEXT("Sprite")))
	, MiniMapWorldBounds(ForceInit)
	, MiniMapTexture(nullptr)
	, WorldUnitsPerPixel(50)
	, BuilderCellSize(102400)
	, CaptureSource(ESceneCaptureSource::SCS_BaseColor)
	, CaptureWarmupFrames(5)
{
}

void AWorldPartitionMiniMap::PostLoad()
{
	Super::PostLoad();

	if (MiniMapTileSize_DEPRECATED != 0)
	{
		const int32 MinimapBuilderIterativeCellSize = 102400;
		WorldUnitsPerPixel = MinimapBuilderIterativeCellSize / MiniMapTileSize_DEPRECATED;
		WorldUnitsPerPixel = FMath::Clamp(WorldUnitsPerPixel, 10, 100000);
		MiniMapTileSize_DEPRECATED = 0;
	}
}

#if WITH_EDITOR
void AWorldPartitionMiniMap::CheckForErrors()
{
	Super::CheckForErrors();

	if (MiniMapTexture != nullptr)
	{
		const int32 MaxTextureDimension = GetMax2DTextureDimension();
		const bool bExceedMaxDimension = MiniMapTexture->GetImportedSize().GetMax() > MaxTextureDimension;
		if (!UseVirtualTexturing(GMaxRHIFeatureLevel) && bExceedMaxDimension)
		{
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("ActorName"), FText::FromString(GetName()));
			FMessageLog("MapCheck").Warning()
				->AddToken(FUObjectToken::Create(this))
				->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_MinimapTextureSize", "{ActorName} : Texture size is too big, minimap won't be rendered. Reduce the MiniMapTileSize property or enable Virtual Texture Support for your project."), Arguments)))
				->AddToken(FMapErrorToken::Create("MinimapTextureSize"));
		}
	}
}

#endif

#undef LOCTEXT_NAMESPACE
