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
	}
}

void UUVEditorBackgroundPreview::UpdateVisibility()
{
	if (Settings->bVisible == false)
	{
		BackgroundComponent->SetVisibility(false);
		return;
	}

	BackgroundComponent->SetVisibility(true);
	BackgroundComponent->MarkRenderStateDirty();
}

void UUVEditorBackgroundPreview::UpdateBackground()
{

	const int32 GridCellCountX = 1;
	const int32 GridCellCountY = 1;
	const FVector Origin = { 0,0,0 };
	const FVector GridDx = { 1000, 1000, 0 };
	const FVector Normal(0, 0, 1);
	const FColor BackgroundColor = FColor::Blue;

	UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/UVEditor/Materials/UVEditorBackground"));
	check(Material);	
	BackgroundMaterial = UMaterialInstanceDynamic::Create(Material, this);
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
	OnBackgroundMaterialChange.Broadcast(BackgroundMaterial);
	BackgroundComponent->Clear();

	for (int32 GridStepX = 0; GridStepX < GridCellCountX; ++GridStepX)
	{
		for (int32 GridStepY = 0; GridStepY < GridCellCountY; ++GridStepY)
		{
			FVector CellOrigin = Origin + FVector(GridDx.X * GridStepX, GridDx.X * GridStepY, 0);
			FVector CellOffsetX = { GridDx.X, 0, 0 };
			FVector CellOffsetY = { 0, GridDx.Y, 0 };
			
			FRenderableTriangleVertex A(CellOrigin, { -1,0 }, Normal, BackgroundColor);
			FRenderableTriangleVertex B(CellOrigin + CellOffsetX, { -1 , -1 }, Normal, BackgroundColor);
			FRenderableTriangleVertex C(CellOrigin + CellOffsetY, { 0, 0 }, Normal, BackgroundColor);
			FRenderableTriangleVertex D(CellOrigin + CellOffsetX + CellOffsetY, { 0,-1 }, Normal, BackgroundColor);

			FRenderableTriangle Lower(BackgroundMaterial, A, D, B);
			FRenderableTriangle Upper(BackgroundMaterial, A, C, D);

			BackgroundComponent->AddTriangle(Lower);
			BackgroundComponent->AddTriangle(Upper);		
		}
	}

	
}
