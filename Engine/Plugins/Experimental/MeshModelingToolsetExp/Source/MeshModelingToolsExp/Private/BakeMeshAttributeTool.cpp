// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeMeshAttributeTool.h"
#include "InteractiveToolManager.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UBakeMeshAttributeTool"


void UBakeMeshAttributeTool::Setup()
{
	Super::Setup();

	// Setup preview materials
	UMaterial* WorkingMaterial = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/InProgressMaterial"));
	check(WorkingMaterial);
	if (WorkingMaterial != nullptr)
	{
		WorkingPreviewMaterial = UMaterialInstanceDynamic::Create(WorkingMaterial, GetToolManager());
	}
}


void UBakeMeshAttributeTool::SetWorld(UWorld* World)
{
	TargetWorld = World;
}


int UBakeMeshAttributeTool::SelectColorTextureToBake(const TArray<UTexture*>& Textures)
{
	TArray<int> TextureVotes;
	TextureVotes.Init(0, Textures.Num());

	for (int TextureIndex = 0; TextureIndex < Textures.Num(); ++TextureIndex)
	{
		UTexture* Tex = Textures[TextureIndex];
		UTexture2D* Tex2D = Cast<UTexture2D>(Tex);

		if (Tex2D)
		{
			// Texture uses SRGB
			if (Tex->SRGB != 0)
			{
				++TextureVotes[TextureIndex];
			}

#if WITH_EDITORONLY_DATA
			// Texture has multiple channels
			ETextureSourceFormat Format = Tex->Source.GetFormat();
			if (Format == TSF_BGRA8 || Format == TSF_BGRE8 || Format == TSF_RGBA16 || Format == TSF_RGBA16F)
			{
				++TextureVotes[TextureIndex];
			}
#endif

			// What else? Largest texture? Most layers? Most mipmaps?
		}
	}

	int MaxIndex = -1;
	int MaxVotes = -1;
	for (int TextureIndex = 0; TextureIndex < Textures.Num(); ++TextureIndex)
	{
		if (TextureVotes[TextureIndex] > MaxVotes)
		{
			MaxIndex = TextureIndex;
			MaxVotes = TextureVotes[TextureIndex];
		}
	}

	return MaxIndex;
}

#undef LOCTEXT_NAMESPACE

