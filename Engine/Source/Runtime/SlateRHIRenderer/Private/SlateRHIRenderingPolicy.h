// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/ShaderResourceManager.h"
#include "Rendering/DrawElements.h"
#include "Rendering/RenderingPolicy.h"
#include "Layout/Clipping.h"
#include "SlateElementIndexBuffer.h"
#include "SlateElementVertexBuffer.h"
#include "SlateRHIResourceManager.h"
#include "Shader.h"
#include "Engine/TextureLODSettings.h"

class FSlateFontServices;
class FSlateRHIResourceManager;
class FSlatePostProcessor;
class UDeviceProfile;

struct FSlateRenderingParams
{
	FMatrix ViewProjectionMatrix;
	FVector2D ViewOffset;
	float CurrentWorldTime;
	float DeltaTimeSeconds;
	float CurrentRealTime;
	bool bAllowSwitchVerticalAxis;
	bool bWireFrame;
	bool bIsHDR;

	FSlateRenderingParams(const FMatrix& InViewProjectionMatrix, float InCurrentWorldTime, float InDeltaTimeSeconds, float InCurrentRealTime)
		: ViewProjectionMatrix(InViewProjectionMatrix)
		, ViewOffset(0, 0)
		, CurrentWorldTime(InCurrentWorldTime)
		, DeltaTimeSeconds(InDeltaTimeSeconds)
		, CurrentRealTime(InCurrentRealTime)
		, bAllowSwitchVerticalAxis(true)
		, bWireFrame(false)
		, bIsHDR(false)
	{
	}
};

class FSlateRHIRenderingPolicy : public FSlateRenderingPolicy
{
public:
	FSlateRHIRenderingPolicy(TSharedRef<FSlateFontServices> InSlateFontServices, TSharedRef<FSlateRHIResourceManager> InResourceManager, TOptional<int32> InitialBufferSize = TOptional<int32>());

	void BuildRenderingBuffers(FRHICommandListImmediate& RHICmdList, FSlateBatchData& InBatchData);

	void DrawElements(FRHICommandListImmediate& RHICmdList, class FSlateBackBuffer& BackBuffer, FTexture2DRHIRef& ColorTarget, FTexture2DRHIRef& DepthStencilTarget, int32 FirstBatchIndex, const TArray<FSlateRenderBatch>& RenderBatches, const FSlateRenderingParams& Params);

	virtual TSharedRef<FSlateShaderResourceManager> GetResourceManager() const override { return ResourceManager; }
	virtual bool IsVertexColorInLinearSpace() const override { return false; }
	
	void InitResources();
	void ReleaseResources();

	void BeginDrawingWindows();
	void EndDrawingWindows();

	void SetUseGammaCorrection( bool bInUseGammaCorrection ) { bGammaCorrect = bInUseGammaCorrection; }
	void SetApplyColorDeficiencyCorrection(bool bInApplyColorCorrection) { bApplyColorDeficiencyCorrection = bInApplyColorCorrection; }

	virtual void AddSceneAt(FSceneInterface* Scene, int32 Index) override;
	virtual void ClearScenes() override;

	virtual void FlushGeneratedResources();

private:
	ETextureSamplerFilter GetSamplerFilter(const UTexture* Texture) const;

	/**
	 * Returns the pixel shader that should be used for the specified ShaderType and DrawEffects
	 * 
	 * @param ShaderType	The shader type being used
	 * @param DrawEffects	Draw effects being used
	 * @return The pixel shader for use with the shader type and draw effects
	 */
	class FSlateElementPS* GetTexturePixelShader( TShaderMap<FGlobalShaderType>* ShaderMap, ESlateShader ShaderType, ESlateDrawEffect DrawEffects );
	class FSlateMaterialShaderPS* GetMaterialPixelShader( const class FMaterial* Material, ESlateShader ShaderType);
	class FSlateMaterialShaderVS* GetMaterialVertexShader( const class FMaterial* Material, bool bUseInstancing );

	/** @return The RHI primitive type from the Slate primitive type */
	EPrimitiveType GetRHIPrimitiveType(ESlateDrawPrimitive SlateType);

private:
	/** Buffers used for rendering */
	TSlateElementVertexBuffer<FSlateVertex> MasterVertexBuffer;
	FSlateElementIndexBuffer MasterIndexBuffer;

	FSlateStencilClipVertexBuffer StencilVertexBuffer;

	/** Handles post process effects for slate */
	TSharedRef<FSlatePostProcessor> PostProcessor;

	TSharedRef<FSlateRHIResourceManager> ResourceManager;

	bool bGammaCorrect;
	bool bApplyColorDeficiencyCorrection;

	TOptional<int32> InitialBufferSizeOverride;

	TArray<FTextureLODGroup> TextureLODGroups;

	UDeviceProfile* LastDeviceProfile;
};
