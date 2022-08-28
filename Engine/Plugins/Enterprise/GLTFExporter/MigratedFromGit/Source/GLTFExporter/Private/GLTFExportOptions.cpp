// Copyriyright Epic Games, Inc. All Rights Reserved.

#include "GLTFExportOptions.h"
#include "GLTFExportOptionsWindow.h"

UGLTFExportOptions::UGLTFExportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bEmbedVertexData = true;
	bExportVertexColor = true;
	bExportPreviewMesh = true;
	bExportUnlitMaterial = true;
	bExportClearCoatMaterial = true;
	bBakeMaterialInputs = true;
	BakedMaterialInputSize = EGLTFExporterTextureSize::POT_512;
	bEmbedTextures = true;
	TextureFormat = EGLTFExporterTextureFormat::PNG;
	bExportLightmaps = true;
	bExportAnyActor = true;
	bExportLight = true;
	bExportCamera = true;
	bExportReflectionCapture = true;
	bExportHDRIBackdrop = true;
	bExportVariantSets = true;
	bExportAnimationTrigger = true;
}

void UGLTFExportOptions::ResetToDefault()
{
	ReloadConfig();
}

void UGLTFExportOptions::LoadOptions()
{
}

void UGLTFExportOptions::SaveOptions()
{
}

void UGLTFExportOptions::FillOptions(bool bShowOptionDialog, const FString& FullPath, bool BatchMode, bool& OutOperationCanceled, bool& bOutExportAll)
{
	OutOperationCanceled = false;

	LoadOptions();

	if (!bShowOptionDialog || GIsAutomationTesting || FApp::IsUnattended())
	{
		return;
	}

	bOutExportAll = false;

	SGLTFExportOptionsWindow::ShowDialog(this, FullPath, BatchMode, OutOperationCanceled, bOutExportAll);
	SaveOptions();
}
