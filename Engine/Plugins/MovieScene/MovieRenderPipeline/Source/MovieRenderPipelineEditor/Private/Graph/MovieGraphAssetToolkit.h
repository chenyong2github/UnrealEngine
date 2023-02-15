// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BaseAssetToolkit.h"

class FMovieGraphAssetToolkit :  public FBaseAssetToolkit
{
public:
	FMovieGraphAssetToolkit(UAssetEditor* InOwningAssetEditor);
	virtual ~FMovieGraphAssetToolkit() override {}

	//~ Begin FBaseAssetToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual FName GetToolkitFName() const override;
	//~ End FBaseAssetToolkit interface

private:
	TSharedRef<SDockTab> SpawnTab_RenderGraphEditor(const FSpawnTabArgs& Args);

private:
	static const FName ContentTabId;
};
