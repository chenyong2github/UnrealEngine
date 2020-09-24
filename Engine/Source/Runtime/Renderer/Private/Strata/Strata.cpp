// Copyright Epic Games, Inc. All Rights Reserved.

#include "Strata.h"
#include "HAL/IConsoleManager.h"
#include "SceneView.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "RendererInterface.h"



// The project setting for Strata
static TAutoConsoleVariable<int32> CVarStrata(
	TEXT("r.Strata"),
	0,
	TEXT("Enables Strata."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);



namespace Strata
{

bool IsStrataEnabled()
{
	return CVarStrata.GetValueOnRenderThread() > 0;
}

void InitialiseStrataFrameSceneData(FSceneRenderer& SceneRenderer, FRDGBuilder& GraphBuilder)
{
	FStrataData& StrataData = SceneRenderer.Scene->StrataData;

	uint32 ResolutionX = 1;
	uint32 ResolutionY = 1;

	if (IsStrataEnabled())
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
		FIntPoint BufferSizeXY = SceneContext.GetBufferSizeXY();
		
		ResolutionX = BufferSizeXY.X;
		ResolutionY = BufferSizeXY.Y;

		StrataData.MaxBytesPerPixel = 16;

	}
	else
	{
		StrataData.MaxBytesPerPixel = 1;
	}

	FRDGTextureRef MaterialLobesTexture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(FIntPoint(ResolutionX, ResolutionY), PF_R16F, FClearValueBinding::None,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV), TEXT("MaterialLobesTexture"));
	AddClearRenderTargetPass(GraphBuilder, MaterialLobesTexture, FLinearColor::Black);
	ConvertToExternalTexture(GraphBuilder, MaterialLobesTexture, StrataData.MaterialLobesTexture);

	const uint32 DesiredBufferSize = FMath::Max(4u, ResolutionX * ResolutionY * StrataData.MaxBytesPerPixel);
	if (StrataData.MaterialLobesBuffer.NumBytes < DesiredBufferSize)
	{
		if (StrataData.MaterialLobesBuffer.NumBytes > 0)
		{
			StrataData.MaterialLobesBuffer.Release();
		}
		StrataData.MaterialLobesBuffer.Initialize(DesiredBufferSize, BUF_Static, TEXT("MaterialLobesBuffer"));
	}

	// Set reference to the Strata data from each view
	for (int32 ViewIndex = 0; ViewIndex < SceneRenderer.Views.Num(); ViewIndex++)
	{
		FViewInfo& View = SceneRenderer.Views[ViewIndex];
		View.StrataData = &SceneRenderer.Scene->StrataData;
	}
}

void BindStrataBasePassUniformParameters(const FViewInfo& View, FStrataOpaquePassUniformParameters& OutStrataUniformParameters)
{
	if (View.StrataData)
	{
		OutStrataUniformParameters.MaxBytesPerPixel = View.StrataData->MaxBytesPerPixel;
		OutStrataUniformParameters.MaterialLobesTextureUAV = View.StrataData->MaterialLobesTexture->GetRenderTargetItem().UAV;
		OutStrataUniformParameters.MaterialLobesBufferUAV = View.StrataData->MaterialLobesBuffer.UAV;
	}
	else
	{
		OutStrataUniformParameters.MaxBytesPerPixel = 0;
		OutStrataUniformParameters.MaterialLobesTextureUAV = GEmptyVertexBufferWithUAV->UnorderedAccessViewRHI;
		OutStrataUniformParameters.MaterialLobesBufferUAV = GEmptyVertexBufferWithUAV->UnorderedAccessViewRHI;
	}
}

} // namespace Strata


