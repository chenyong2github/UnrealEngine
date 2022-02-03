// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorBackgroundPreview.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Drawing/LineSetComponent.h"
#include "Drawing/MeshWireframeComponent.h"
#include "ToolSetupUtil.h"
#include "Async/Async.h"
#include "UVEditorUXSettings.h"

using namespace UE::Geometry;

void UUVEditorBackgroundPreview::OnCreated()
{
	Settings = NewObject<UUVEditorBackgroundPreviewProperties>(this);
	Settings->WatchProperty(Settings->bVisible, [this](bool) { bSettingsModified = true; });
	Settings->WatchProperty(Settings->SourceType, [this](EUVEditorBackgroundSourceType) { bSettingsModified = true; });
	Settings->WatchProperty(Settings->SourceTexture, [this](UTexture2D*) {bSettingsModified = true; });
	Settings->WatchProperty(Settings->SourceMaterial, [this](UMaterial*) {bSettingsModified = true; });

	bSettingsModified = false;

	BackgroundComponent = NewObject<UTriangleSetComponent>(GetActor());
	BackgroundComponent->SetupAttachment(GetActor()->GetRootComponent());
	BackgroundComponent->RegisterComponent();
}

void UUVEditorBackgroundPreview::OnTick(float DeltaTime)
{
	if (bSettingsModified)
	{		
		UpdateBackground();
		UpdateVisibility();
		bSettingsModified = false;
		OnBackgroundMaterialChange.Broadcast(BackgroundMaterial);
	}
}

void UUVEditorBackgroundPreview::UpdateVisibility()
{
	if (Settings->bVisible == false)
	{
		BackgroundComponent->SetVisibility(false);
		ActiveUDIMBlocks.UDIMBlocks.SetNum(0);
		return;
	}

	BackgroundComponent->SetVisibility(true);
	BackgroundComponent->MarkRenderStateDirty();
}

void UUVEditorBackgroundPreview::UpdateBackground()
{
	bool bEnableUDIMSupport = (FUVEditorUXSettings::CVarEnablePrototypeUDIMSupport.GetValueOnGameThread() > 0);

	const FVector Normal(0, 0, 1);
	const FColor BackgroundColor = FColor::Blue;

	UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/UVEditor/Materials/UVEditorBackground"));
	check(Material);	
	BackgroundMaterial = UMaterialInstanceDynamic::Create(Material, this);
	ActiveUDIMBlocks.UDIMBlocks.SetNum(0);
	switch (Settings->SourceType)
	{
		case EUVEditorBackgroundSourceType::Checkerboard:
		{
			// Do nothing, since the default material is already set up for a checkerboard.
		}
		break;

		case EUVEditorBackgroundSourceType::Material:
		{
			if (Settings->SourceMaterial)
			{
				BackgroundMaterial = UMaterialInstanceDynamic::Create(Settings->SourceMaterial.Get(), this);
			}
		}
		break;

		case EUVEditorBackgroundSourceType::Texture:
		{
			if (Settings->SourceTexture)
			{
				if (Settings->SourceTexture->IsCurrentlyVirtualTextured())
				{
					BackgroundMaterial->SetTextureParameterValue(TEXT("BackgroundVTBaseMap"), Settings->SourceTexture);
					BackgroundMaterial->SetScalarParameterValue(TEXT("BackgroundVirtualTextureSwitch"), 1);

					// Check for UDIMs
					if (bEnableUDIMSupport && Settings->SourceTexture->Source.GetNumBlocks() > 1) {
						ActiveUDIMBlocks.UDIMBlocks.SetNum(Settings->SourceTexture->Source.GetNumBlocks());						
					
						for (int32 Block = 0; Block < Settings->SourceTexture->Source.GetNumBlocks(); ++Block) {
							FTextureSourceBlock SourceBlock;
							Settings->SourceTexture->Source.GetBlock(Block, SourceBlock);

							ActiveUDIMBlocks.UDIMBlocks[Block].BlockX = SourceBlock.BlockX;
							ActiveUDIMBlocks.UDIMBlocks[Block].BlockY = SourceBlock.BlockY;
							ActiveUDIMBlocks.UDIMBlocks[Block].SizeX = SourceBlock.SizeX;
							ActiveUDIMBlocks.UDIMBlocks[Block].SizeY = SourceBlock.SizeY;
						}
					}
				}
				else
				{
					BackgroundMaterial->SetTextureParameterValue(TEXT("BackgroundBaseMap"), Settings->SourceTexture);
					BackgroundMaterial->SetScalarParameterValue(TEXT("BackgroundVirtualTextureSwitch"), 0);
				}
			}
		}
		break;

		default:
			ensure(false);
	}

	BackgroundMaterial->SetScalarParameterValue(TEXT("BackgroundPixelDepthOffset"), FUVEditorUXSettings::BackgroundQuadDepthOffset);	
	BackgroundComponent->Clear();

	TArray<FVector2f> UDimBlocksToRender;
	for (int32 BlockIndex = 0; BlockIndex < ActiveUDIMBlocks.UDIMBlocks.Num(); ++BlockIndex)
	{
		UDimBlocksToRender.Push(FVector2f(
			ActiveUDIMBlocks.UDIMBlocks[BlockIndex].BlockX,
			ActiveUDIMBlocks.UDIMBlocks[BlockIndex].BlockY
			));
	}
	if (UDimBlocksToRender.Num() == 0)
	{
		UDimBlocksToRender.Push(FVector2f(0, 0));
	}

	for (const FVector2f& GridStep : UDimBlocksToRender)
	{
		FVector2f UV00 = { (GridStep.X + 0.0f) , (GridStep.Y + 0.0f) };
		FVector2f UV01 = { (GridStep.X + 1.0f) , (GridStep.Y + 0.0f) };		
		FVector2f UV10 = { (GridStep.X + 0.0f) , (GridStep.Y + 1.0f) };
		FVector2f UV11 = { (GridStep.X + 1.0f) , (GridStep.Y + 1.0f) };

		UV00 = FUVEditorUXSettings::ExternalUVToInternalUV(UV00);
		UV01 = FUVEditorUXSettings::ExternalUVToInternalUV(UV01);
		UV10 = FUVEditorUXSettings::ExternalUVToInternalUV(UV10);
		UV11 = FUVEditorUXSettings::ExternalUVToInternalUV(UV11);

		FRenderableTriangleVertex A((FVector)FUVEditorUXSettings::UVToVertPosition(UV00), (FVector2D)UV00, Normal, BackgroundColor);
		FRenderableTriangleVertex B((FVector)FUVEditorUXSettings::UVToVertPosition(UV10), (FVector2D)UV10, Normal, BackgroundColor);
		FRenderableTriangleVertex C((FVector)FUVEditorUXSettings::UVToVertPosition(UV01), (FVector2D)UV01, Normal, BackgroundColor);
		FRenderableTriangleVertex D((FVector)FUVEditorUXSettings::UVToVertPosition(UV11), (FVector2D)UV11, Normal, BackgroundColor);

		FRenderableTriangle Lower(BackgroundMaterial, A, D, B);
		FRenderableTriangle Upper(BackgroundMaterial, A, C, D);

		BackgroundComponent->AddTriangle(Lower);
		BackgroundComponent->AddTriangle(Upper);
	}
}
