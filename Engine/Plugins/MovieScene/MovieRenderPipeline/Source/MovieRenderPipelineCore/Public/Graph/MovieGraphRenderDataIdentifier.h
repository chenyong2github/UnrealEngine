// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MovieGraphRenderDataIdentifier.generated.h"

/**
* This data structure can be used to identify what render data a set of pixels represents
* by knowing what the render layer name is, what renderer produced it, if it's a sub-resource,
* and what camera it is for. Can be used as the key in a TMap.
*/
USTRUCT(BlueprintType)
struct FMovieGraphRenderDataIdentifier
{
	GENERATED_BODY()

	FMovieGraphRenderDataIdentifier()
	{}

	FMovieGraphRenderDataIdentifier(const FString& InRenderLayerName, const FString& InRendererName,
		const FString& InSubRenderResourceName, const FString& InCameraName)
		: RenderLayerName(InRenderLayerName)
		, RendererName(InRendererName)
		, SubResourceName(InSubRenderResourceName)
		, CameraName(InCameraName)
	{
	}

	bool operator == (const FMovieGraphRenderDataIdentifier& InRHS) const
	{
		return RenderLayerName == InRHS.RenderLayerName && 
			RendererName == InRHS.RendererName &&
			SubResourceName == InRHS.SubResourceName &&
			CameraName == InRHS.CameraName;
	}

	bool operator != (const FMovieGraphRenderDataIdentifier& InRHS) const
	{
		return !(*this == InRHS);
	}

	friend uint32 GetTypeHash(FMovieGraphRenderDataIdentifier InIdentifier)
	{
		return HashCombineFast(GetTypeHash(InIdentifier.RenderLayerName),
			HashCombineFast(GetTypeHash(InIdentifier.RendererName),
				HashCombineFast(GetTypeHash(InIdentifier.SubResourceName),
					GetTypeHash(InIdentifier.CameraName))));
	}

	friend FString LexToString(const FMovieGraphRenderDataIdentifier InIdentifier)
	{
		return FString::Printf(TEXT("RenderLayer: %s Renderer:%s SubResource: %s Camera: %s"), *InIdentifier.RenderLayerName, *InIdentifier.RendererName, *InIdentifier.SubResourceName, *InIdentifier.CameraName);
	}

public:
	/** The user provided name for the whole Render Layer ("character", "background", etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	FString RenderLayerName;

	/** Which renderer was used to produce this image ("panoramic" "deferred" "path tracer", etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	FString RendererName;

	/** A sub-resource name for the renderer (ie: "beauty", "object id", "depth", etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	FString SubResourceName;

	/** The name of the camera being used for this render. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	FString CameraName;
};