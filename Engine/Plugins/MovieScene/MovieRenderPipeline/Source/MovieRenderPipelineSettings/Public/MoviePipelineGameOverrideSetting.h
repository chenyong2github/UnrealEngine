// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineSetting.h"
#include "GameFramework/GameModeBase.h"
#include "Templates/SubclassOf.h"
#include "MoviePipelineGameOverrideSetting.generated.h"

// Forward Declares
class AGameModeBase;

UCLASS(Blueprintable)
class MOVIERENDERPIPELINESETTINGS_API UMoviePipelineGameOverrideSetting : public UMoviePipelineSetting
{
	GENERATED_BODY()
public:
	UMoviePipelineGameOverrideSetting()
		: bDisableCinematicMode(false)
		, bShowPlayer(false)
	{
	}

public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "GameOverrideSettingDisplayName", "Game Overrides"); }
#endif
	virtual bool IsValidOnShots() const override { return false; }
	virtual bool IsValidOnMaster() const override { return true; }

public:
	/** Optional Game Mode to override the map's default game mode with. This can be useful if the game's normal mode displays UI elements or loading screens that you don't want captured. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	TSubclassOf<AGameModeBase> GameModeOverride;
		
	/** Cinematic Mode makes various systems to flush async loading to ensure first frame accuracy, which also disables Player Controller input, and the HUD spawned by AGameModeBase::HUDClass */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	bool bDisableCinematicMode;
	
	/** Should the Player Pawn spawned by the maps gamemode be visible? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	bool bShowPlayer;
};