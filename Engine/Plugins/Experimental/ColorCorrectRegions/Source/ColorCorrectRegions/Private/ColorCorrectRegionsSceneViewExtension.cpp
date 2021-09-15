// Copyright Epic Games, Inc. All Rights Reserved.

#include "ColorCorrectRegionsSceneViewExtension.h"
#include "RHI.h"
#include "SceneView.h"
#include "PostProcess/PostProcessing.h"
#include "ColorCorrectRegionDatabase.h"
#include "ColorCorrectRegionsSubsystem.h"
#include "ColorCorrectRegionsPostProcessMaterial.h"
#include "ScreenPass.h"
#include "CommonRenderResources.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "SceneRendering.h"
// Set this to 1 to clip pixels outside of bounding box.
#define CLIP_PIXELS_OUTSIDE_AABB 1

//Set this to 1 to see the clipping region.
#define ColorCorrectRegions_SHADER_DISPLAY_BOUNDING_RECT 0


namespace
{
	FRHIDepthStencilState* GetMaterialStencilState(const FMaterial* Material)
	{
		static FRHIDepthStencilState* StencilStates[] =
		{
			TStaticDepthStencilState<false, CF_Always, true, CF_Less>::GetRHI(),
			TStaticDepthStencilState<false, CF_Always, true, CF_LessEqual>::GetRHI(),
			TStaticDepthStencilState<false, CF_Always, true, CF_Greater>::GetRHI(),
			TStaticDepthStencilState<false, CF_Always, true, CF_GreaterEqual>::GetRHI(),
			TStaticDepthStencilState<false, CF_Always, true, CF_Equal>::GetRHI(),
			TStaticDepthStencilState<false, CF_Always, true, CF_NotEqual>::GetRHI(),
			TStaticDepthStencilState<false, CF_Always, true, CF_Never>::GetRHI(),
			TStaticDepthStencilState<false, CF_Always, true, CF_Always>::GetRHI(),
		};
		static_assert(EMaterialStencilCompare::MSC_Count == UE_ARRAY_COUNT(StencilStates), "Ensure that all EMaterialStencilCompare values are accounted for.");

		check(Material);

		return StencilStates[Material->GetStencilCompare()];
	}

	FScreenPassTextureViewportParameters GetTextureViewportParameters(const FScreenPassTextureViewport& InViewport)
	{
		const FVector2D Extent(InViewport.Extent);
		const FVector2D ViewportMin(InViewport.Rect.Min.X, InViewport.Rect.Min.Y);
		const FVector2D ViewportMax(InViewport.Rect.Max.X, InViewport.Rect.Max.Y);
		const FVector2D ViewportSize = ViewportMax - ViewportMin;

		FScreenPassTextureViewportParameters Parameters;

		if (!InViewport.IsEmpty())
		{
			Parameters.Extent = Extent;
			Parameters.ExtentInverse = FVector2D(1.0f / Extent.X, 1.0f / Extent.Y);

			Parameters.ScreenPosToViewportScale = FVector2D(0.5f, -0.5f) * ViewportSize;
			Parameters.ScreenPosToViewportBias = (0.5f * ViewportSize) + ViewportMin;

			Parameters.ViewportMin = InViewport.Rect.Min;
			Parameters.ViewportMax = InViewport.Rect.Max;

			Parameters.ViewportSize = ViewportSize;
			Parameters.ViewportSizeInverse = FVector2D(1.0f / Parameters.ViewportSize.X, 1.0f / Parameters.ViewportSize.Y);

			Parameters.UVViewportMin = ViewportMin * Parameters.ExtentInverse;
			Parameters.UVViewportMax = ViewportMax * Parameters.ExtentInverse;

			Parameters.UVViewportSize = Parameters.UVViewportMax - Parameters.UVViewportMin;
			Parameters.UVViewportSizeInverse = FVector2D(1.0f / Parameters.UVViewportSize.X, 1.0f / Parameters.UVViewportSize.Y);

			Parameters.UVViewportBilinearMin = Parameters.UVViewportMin + 0.5f * Parameters.ExtentInverse;
			Parameters.UVViewportBilinearMax = Parameters.UVViewportMax - 0.5f * Parameters.ExtentInverse;
		}

		return Parameters;
	}


	void GetPixelSpaceBoundingRect(const FSceneView& InView, const FVector& InBoxCenter, const FVector& InBoxExtents, FIntRect& OutViewport, float& OutMaxDepth, float& OutMinDepth)
	{
		OutViewport = FIntRect(INT32_MAX, INT32_MAX, -INT32_MAX, -INT32_MAX);
		// 8 corners of the bounding box. To be multiplied by box extent and offset by the center.
		const int NumCorners = 8;
		const FVector Verts[NumCorners] = {
			FVector(1, 1, 1),
			FVector(1, 1,-1),
			FVector(1,-1, 1),
			FVector(1,-1,-1),
			FVector(-1, 1, 1),
			FVector(-1, 1,-1),
			FVector(-1,-1, 1),
			FVector(-1,-1,-1) };

		for (int32 Index = 0; Index < NumCorners; Index++)
		{
			// Project bounding box vertecies into screen space.
			const FVector WorldVert = InBoxCenter + (Verts[Index] * InBoxExtents);
			FVector2D PixelVert;
			FVector4 ScreenSpaceCoordinate = InView.WorldToScreen(WorldVert);

			OutMaxDepth = FMath::Max<float>(ScreenSpaceCoordinate.W, OutMaxDepth);
			OutMinDepth = FMath::Min<float>(ScreenSpaceCoordinate.W, OutMinDepth);

			if (InView.ScreenToPixel(ScreenSpaceCoordinate, PixelVert))
			{
				// Update screen-space bounding box with with transformed vert.
				OutViewport.Min.X = FMath::Min<int32>(OutViewport.Min.X, PixelVert.X);
				OutViewport.Min.Y = FMath::Min<int32>(OutViewport.Min.Y, PixelVert.Y);

				OutViewport.Max.X = FMath::Max<int32>(OutViewport.Max.X, PixelVert.X);
				OutViewport.Max.Y = FMath::Max<int32>(OutViewport.Max.Y, PixelVert.Y);
			}
		}
	}

	// Function that calculates all points of intersection between plane and bounding box. Resulting points are unsorted.
	void CalculatePlaneAABBIntersectionPoints(const FPlane& Plane, const FVector& BoxCenter, const FVector& BoxExtents, TArray<FVector>& OutPoints)
	{
		const FVector MaxCorner = BoxCenter + BoxExtents;

		const FVector Verts[3][4] = {
			{
				// X Direction
				FVector(-1, -1, -1),
				FVector(-1,  1, -1),
				FVector(-1, -1,  1),
				FVector(-1,  1,  1),
			},
			{
				// Y Direction
				FVector(-1, -1, -1),
				FVector( 1, -1, -1),
				FVector( 1, -1,  1),
				FVector(-1, -1,  1),
			},
			{
				// Z Direction
				FVector(-1, -1, -1),
				FVector( 1, -1, -1),
				FVector( 1,  1, -1),
				FVector(-1,  1, -1),
			}
		};

		FVector Intersection;
		FVector Start;
		FVector End;

		for (int RunningAxis_Dir = 0; RunningAxis_Dir < 3; RunningAxis_Dir++)
		{
			const FVector *CornerLocations = Verts[RunningAxis_Dir];
			for (int RunningCorner = 0; RunningCorner < 4; RunningCorner++)
			{
				Start = BoxCenter + BoxExtents * CornerLocations[RunningCorner];
				End = FVector(Start.X, Start.Y, Start.Z);
				End[RunningAxis_Dir] = MaxCorner[RunningAxis_Dir];
				if (FMath::SegmentPlaneIntersection(Start, End, Plane, Intersection))
				{
					OutPoints.Add(Intersection);
				}
			}
		}
	}

	// Takes in an existing viewport and updates it with an intersection bounding rectangle.
	void UpdateMinMaxWithFrustrumAABBIntersection(const FSceneView& InView, const FVector& InBoxCenter, const FVector& InBoxExtents, FIntRect& OutViewportToUpdate, float& OutMaxDepthToUpdate)
	{
		TArray<FVector> Points;
		Points.Reserve(6);
		CalculatePlaneAABBIntersectionPoints(InView.ViewFrustum.Planes[4], InBoxCenter, InBoxExtents, Points);
		if (Points.Num() == 0)
		{
			return;
		}

		for (FVector Point : Points)
		{
			// Project bounding box vertecies into screen space.
			FVector4 ScreenSpaceCoordinate = InView.WorldToScreen(Point);
			FVector4 ScreenSpaceCoordinateScaled = ScreenSpaceCoordinate * 1.0 / ScreenSpaceCoordinate.W;

			OutMaxDepthToUpdate = FMath::Max<float>(ScreenSpaceCoordinate.W, OutMaxDepthToUpdate);
			FVector2D PixelVert;

			if (InView.ScreenToPixel(ScreenSpaceCoordinate, PixelVert))
			{
				// Update screen-space bounding box with with transformed vert.
				OutViewportToUpdate.Min.X = FMath::Min<int32>(OutViewportToUpdate.Min.X, PixelVert.X);
				OutViewportToUpdate.Min.Y = FMath::Min<int32>(OutViewportToUpdate.Min.Y, PixelVert.Y);

				OutViewportToUpdate.Max.X = FMath::Max<int32>(OutViewportToUpdate.Max.X, PixelVert.X);
				OutViewportToUpdate.Max.Y = FMath::Max<int32>(OutViewportToUpdate.Max.Y, PixelVert.Y);
			}
		}
	}

	bool ViewSupportsRegions(const FSceneView& View)
	{
		return View.Family->EngineShowFlags.PostProcessing &&
				View.Family->EngineShowFlags.PostProcessMaterial;
	}

	template<typename TSetupFunction>
	void DrawScreenPass(
		FRHICommandListImmediate& RHICmdList,
		const FSceneView& View,
		const FScreenPassTextureViewport& OutputViewport,
		const FScreenPassTextureViewport& InputViewport,
		const FScreenPassPipelineState& PipelineState,
		TSetupFunction SetupFunction)
	{
		PipelineState.Validate();

		const FIntRect InputRect = InputViewport.Rect;
		const FIntPoint InputSize = InputViewport.Extent;
		const FIntRect OutputRect = OutputViewport.Rect;
		const FIntPoint OutputSize = OutputRect.Size();

		RHICmdList.SetViewport(OutputRect.Min.X, OutputRect.Min.Y, 0.0f, OutputRect.Max.X, OutputRect.Max.Y, 1.0f);

		SetScreenPassPipelineState(RHICmdList, PipelineState);

		// Setting up buffers.
		SetupFunction(RHICmdList);

		FIntPoint LocalOutputPos(FIntPoint::ZeroValue);
		FIntPoint LocalOutputSize(OutputSize);
		EDrawRectangleFlags DrawRectangleFlags = EDRF_UseTriangleOptimization;

		DrawPostProcessPass(
			RHICmdList,
			LocalOutputPos.X, LocalOutputPos.Y, LocalOutputSize.X, LocalOutputSize.Y,
			InputRect.Min.X, InputRect.Min.Y, InputRect.Width(), InputRect.Height(),
			OutputSize,
			InputSize,
			PipelineState.VertexShader,
			View.StereoPass,
			false,
			DrawRectangleFlags);
	}

	// A helper function for getting the right shader.
	TShaderMapRef<FColorCorrectRegionMaterialPS> GetRegionShader(const FGlobalShaderMap* GlobalShaderMap, EColorCorrectRegionsType RegionType, FColorCorrectRegionMaterialPS::ETemperatureType TemperatureType, bool bIsAdvanced, bool bSampleOpacityFromGbuffer)
	{
		FColorCorrectRegionMaterialPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FColorCorrectRegionMaterialPS::FAdvancedShader>(bIsAdvanced);
		PermutationVector.Set<FColorCorrectRegionMaterialPS::FShaderType>(RegionType);
		PermutationVector.Set<FColorCorrectRegionMaterialPS::FTemperatureType>(TemperatureType);

#if ColorCorrectRegions_SHADER_DISPLAY_BOUNDING_RECT
		PermutationVector.Set<FColorCorrectRegionMaterialPS::FDisplayBoundingRect>(true);
#endif
#if CLIP_PIXELS_OUTSIDE_AABB
		PermutationVector.Set<FColorCorrectRegionMaterialPS::FClipPixelsOutsideAABB>(true);
#endif
		PermutationVector.Set<FColorCorrectRegionMaterialPS::FSampleOpacityFromGbuffer>(bSampleOpacityFromGbuffer);
		
		return TShaderMapRef<FColorCorrectRegionMaterialPS>(GlobalShaderMap, PermutationVector);
		;
	}

	FVector4 Clamp(const FVector4 & VectorToClamp, float Min, float Max)
	{
		return FVector4(FMath::Clamp(VectorToClamp.X, Min, Max),
						FMath::Clamp(VectorToClamp.Y, Min, Max),
						FMath::Clamp(VectorToClamp.Z, Min, Max),
						FMath::Clamp(VectorToClamp.W, Min, Max));
	}
}

FColorCorrectRegionsSceneViewExtension::FColorCorrectRegionsSceneViewExtension(const FAutoRegister& AutoRegister, UColorCorrectRegionsSubsystem* InWorldSubsystem) :
	FSceneViewExtensionBase(AutoRegister), WorldSubsystem(InWorldSubsystem)
{
}

void FColorCorrectRegionsSceneViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	// Necessary for when an actor is added or removed from the scene. Also when priority is changed.
	FScopeLock RegionScopeLock(&WorldSubsystem->RegionAccessCriticalSection);

	if (WorldSubsystem->Regions.Num() == 0 || !ViewSupportsRegions(View))
	{
		return;
	}

	Inputs.Validate();

	const FSceneViewFamily& ViewFamily = *View.Family;

	const auto FeatureLevel = View.GetFeatureLevel();
	const float ScreenPercentage = ViewFamily.GetPrimaryResolutionFractionUpperBound() * ViewFamily.SecondaryViewFraction;
	
	// We need to make sure to take Windows and Scene scale into account.

	checkSlow(View.bIsViewInfo); // can't do dynamic_cast because FViewInfo doesn't have any virtual functions.
	const FIntRect PrimaryViewRect = static_cast<const FViewInfo&>(View).ViewRect;

	FScreenPassTexture SceneColor((*Inputs.SceneTextures)->SceneColorTexture, PrimaryViewRect);


	if (!SceneColor.IsValid())
	{
		return;
	}

	{
		// Getting material data for the current view.
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		// Reusing the same output description for our back buffer as SceneColor
		FRDGTextureDesc ColorCorrectRegionsOutputDesc = SceneColor.Texture->Desc;
		bool bSampleOpacityFromGbuffer = false;
		if (ColorCorrectRegionsOutputDesc.Format != PF_FloatRGBA)
		{
			bSampleOpacityFromGbuffer = true;
		}
		ColorCorrectRegionsOutputDesc.Format = PF_FloatRGBA;
		FLinearColor ClearColor(0., 0., 0., 0.);
		ColorCorrectRegionsOutputDesc.ClearValue = FClearValueBinding(ClearColor);

		FRDGTexture* BackBufferRenderTargetTexture = GraphBuilder.CreateTexture(ColorCorrectRegionsOutputDesc, TEXT("BackBufferRenderTargetTexture"));
		FScreenPassRenderTarget BackBufferRenderTarget = FScreenPassRenderTarget(BackBufferRenderTargetTexture, SceneColor.ViewRect, ERenderTargetLoadAction::EClear);
		FScreenPassRenderTarget SceneColorRenderTarget(SceneColor, ERenderTargetLoadAction::ELoad);
		const FScreenPassTextureViewport SceneColorTextureViewport(SceneColor);

		FRHIBlendState* DefaultBlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();
		FRHIDepthStencilState* DepthStencilState = FScreenPassPipelineState::FDefaultDepthStencilState::GetRHI();

		RDG_EVENT_SCOPE(GraphBuilder, "Color Correct Regions %dx%d", SceneColorTextureViewport.Rect.Width(), SceneColorTextureViewport.Rect.Height());

		FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		const FScreenPassTextureViewportParameters SceneTextureViewportParams = GetTextureViewportParameters(SceneColorTextureViewport);
		FScreenPassTextureInput SceneTextureInput;
		{
			SceneTextureInput.Viewport = SceneTextureViewportParams;
			SceneTextureInput.Texture = SceneColorRenderTarget.Texture;
			SceneTextureInput.Sampler = PointClampSampler;
		}

		// Because we are not using proxy material, but plain global shader, we need to setup Scene textures ourselves.
		// We don't need to do this per region.
		FSceneTextureShaderParameters SceneTextures = CreateSceneTextureShaderParameters(GraphBuilder, View.GetFeatureLevel(), ESceneTextureSetupMode::All);

		for (auto It = WorldSubsystem->Regions.CreateConstIterator(); It; ++It)
		{
			AColorCorrectRegion* Region = *It;

			// We use primitive componentIds to check if this region is hidden from camera in cases such as Display cluster.
			FPrimitiveComponentId FirstComponentId = FColorCorrectRegionDatabase::GetFirstComponentId(Region);

			/* If Region is pending for kill, invisible or disabled we don't need to render it.
			*	If Region's Primitive is not visible in the current view's scene then we don't need to render it either.
			*	We are checking if the region belongs to the same world as the view. 
			* Alternative is to get all component ids from ViewFamily.Scene and compare with actor's 
			*	Region->ActiveMeshComponent->ComponentId ComponentIdSearchTable.Contains(RegionPrimitiveComponentId)
			*/
			if (Region->IsPendingKill() || 
				Region->IsActorBeingDestroyed() ||
				!Region->Enabled || 
				Region->IsHidden() ||
#if WITH_EDITOR
				Region->IsHiddenEd() ||
#endif
				Region->GetWorld() != ViewFamily.Scene->GetWorld() ||
				View.HiddenPrimitives.Contains(FirstComponentId))
			{
				continue;
			}

			FVector BoxCenter, BoxExtents;
			Region->GetBounds(BoxCenter, BoxExtents);

			// If bounding box is zero, then we don't need to do anything.
			if (BoxExtents.IsNearlyZero())
			{
				continue;
			}

			FIntRect Viewport;

			float MaxDepth = -BIG_NUMBER;
			float MinDepth = BIG_NUMBER;

			if (Region->Invert)
			{
				// In case of Region inversion we would to render the entire screen
				Viewport = PrimaryViewRect;
			}
			else
			{
				GetPixelSpaceBoundingRect(View, BoxCenter, BoxExtents, Viewport, MaxDepth, MinDepth);

				// Check if CCR is too small to be rendered (less than one pixel on the screen).
				if (Viewport.Width() == 0 || Viewport.Height() == 0)
				{
					continue;
				}

				// This is to handle corner cases when user has a very long disproportionate region and gets either
				// within bounds or close to the center.
				float MaxBoxExtent = FMath::Abs(BoxExtents.GetMax());
				if (MaxDepth >= 0 && MinDepth < 0)
				{
					UpdateMinMaxWithFrustrumAABBIntersection(View, BoxCenter, BoxExtents, Viewport, MaxDepth);
				}

				FIntRect ConstrainedViewRect = View.UnscaledViewRect;

				// We need to make sure that Bounding Rectangle is offset by the position of the View's Viewport.
				Viewport.Min -= ConstrainedViewRect.Min;

				Viewport = Viewport.Scale(ScreenPercentage);

				// Culling all regions that are not within the screen bounds.
				if ((Viewport.Min.X >= PrimaryViewRect.Width() ||
					Viewport.Min.Y >= PrimaryViewRect.Height() ||
					Viewport.Max.X <= 0.0f ||
					Viewport.Max.Y <= 0.0f ||
					MaxDepth < 0.0f))
				{
					continue;
				}
				// Clipping is required because as we get closer to the bounding box the bounds
				// May extend beyond Allowed render target size.
				Viewport.Clip(PrimaryViewRect);
			}
			

			bool bIsAdvanced = false;

			const FVector4 One(1., 1., 1., 1.);
			const FVector4 Zero(0., 0., 0., 0.);
			TArray<FColorGradePerRangeSettings*> AdvancedSettings{ &Region->ColorGradingSettings.Shadows,
																	&Region->ColorGradingSettings.Midtones,
																	&Region->ColorGradingSettings.Highlights };

			// Check if any of the regions are advanced.
			for (auto SettingsIt = AdvancedSettings.CreateConstIterator(); SettingsIt; ++SettingsIt)
			{
				const FColorGradePerRangeSettings* ColorGradingSettings = *SettingsIt;
				if (!ColorGradingSettings->Saturation.Equals(One, SMALL_NUMBER) ||
					!ColorGradingSettings->Contrast.Equals(One, SMALL_NUMBER) ||
					!ColorGradingSettings->Gamma.Equals(One, SMALL_NUMBER) ||
					!ColorGradingSettings->Gain.Equals(One, SMALL_NUMBER) ||
					!ColorGradingSettings->Offset.Equals(Zero, SMALL_NUMBER))
				{
					bIsAdvanced = true;
					break;
				}
			}

			const FScreenPassTextureViewport RegionViewport(SceneColorRenderTarget.Texture, Viewport);

			FCCRShaderInputParameters* PostProcessMaterialParameters = GraphBuilder.AllocParameters<FCCRShaderInputParameters>();
			PostProcessMaterialParameters->RenderTargets[0] = BackBufferRenderTarget.GetRenderTargetBinding();

			PostProcessMaterialParameters->PostProcessOutput = SceneTextureViewportParams;
			PostProcessMaterialParameters->PostProcessInput[0] = SceneTextureInput;
			PostProcessMaterialParameters->SceneTextures = SceneTextures;
			PostProcessMaterialParameters->View = View.ViewUniformBuffer;

			TShaderMapRef<FColorCorrectRegionMaterialVS> VertexShader(GlobalShaderMap);
			const float DefaultTemperature = 6500;

			// If temperature is default we don't want to do the calculations.
			FColorCorrectRegionMaterialPS::ETemperatureType TemperatureType = FMath::IsNearlyEqual(Region->Temperature, DefaultTemperature) 
				? FColorCorrectRegionMaterialPS::ETemperatureType::Disabled 
				: static_cast<FColorCorrectRegionMaterialPS::ETemperatureType>(Region->TemperatureType);
			TShaderMapRef<FColorCorrectRegionMaterialPS> PixelShader = GetRegionShader(GlobalShaderMap, Region->Type, TemperatureType, bIsAdvanced, bSampleOpacityFromGbuffer);

			ClearUnusedGraphResources(VertexShader, PixelShader, PostProcessMaterialParameters);

			FCCRRegionDataInputParameter RegionData;
			FCCRColorCorrectParameter CCBase;
			FCCRColorCorrectShadowsParameter CCShadows;
			FCCRColorCorrectMidtonesParameter CCMidtones;
			FCCRColorCorrectHighlightsParameter CCHighlights;


			// Setting constant buffer data to be passed to the shader.
			{
				RegionData.Rotate = FMath::DegreesToRadians<FVector>(Region->GetActorRotation().Euler());
				RegionData.Translate = Region->GetActorLocation();

				const float ScaleMultiplier = 50.;
				// Pre multiplied scale. 
				RegionData.Scale = Region->GetActorScale() * ScaleMultiplier;

				RegionData.WhiteTemp = Region->Temperature;
				// Inner could be larger than outer, in which case we need to make sure these are swapped.
				RegionData.Inner = FMath::Min<float>(Region->Outer, Region->Inner);
				RegionData.Outer = FMath::Max<float>(Region->Outer, Region->Inner);
				RegionData.Falloff = Region->Falloff;
				RegionData.Intensity = Region->Intensity;
				RegionData.ExcludeStencil = Region->ExcludeStencil;
				RegionData.Invert = Region->Invert;

				CCBase.ColorSaturation = Region->ColorGradingSettings.Global.Saturation;
				CCBase.ColorContrast = Region->ColorGradingSettings.Global.Contrast;
				CCBase.ColorGamma = Region->ColorGradingSettings.Global.Gamma;
				CCBase.ColorGain = Region->ColorGradingSettings.Global.Gain;
				CCBase.ColorOffset = Region->ColorGradingSettings.Global.Offset;

				// Set advanced 
				if (bIsAdvanced)
				{
					const float GammaMin = 0.02;
					const float GammaMax = 10.;
					//clamp(ExternalExpressions.ColorGammaHighlights, 0.02, 10.)
					CCShadows.ColorSaturation = Region->ColorGradingSettings.Shadows.Saturation;
					CCShadows.ColorContrast = Region->ColorGradingSettings.Shadows.Contrast;
					CCShadows.ColorGamma = Clamp(Region->ColorGradingSettings.Shadows.Gamma, GammaMin, GammaMax);
					CCShadows.ColorGain = Region->ColorGradingSettings.Shadows.Gain;
					CCShadows.ColorOffset = Region->ColorGradingSettings.Shadows.Offset;
					CCShadows.ShadowMax = Region->ColorGradingSettings.ShadowsMax;

					CCMidtones.ColorSaturation = Region->ColorGradingSettings.Midtones.Saturation;
					CCMidtones.ColorContrast = Region->ColorGradingSettings.Midtones.Contrast;
					CCMidtones.ColorGamma = Clamp(Region->ColorGradingSettings.Midtones.Gamma, GammaMin, GammaMax);
					CCMidtones.ColorGain = Region->ColorGradingSettings.Midtones.Gain;
					CCMidtones.ColorOffset = Region->ColorGradingSettings.Midtones.Offset;

					CCHighlights.ColorSaturation = Region->ColorGradingSettings.Highlights.Saturation;
					CCHighlights.ColorContrast = Region->ColorGradingSettings.Highlights.Contrast;
					CCHighlights.ColorGamma = Clamp(Region->ColorGradingSettings.Highlights.Gamma, GammaMin, GammaMax);
					CCHighlights.ColorGain = Region->ColorGradingSettings.Highlights.Gain;
					CCHighlights.ColorOffset = Region->ColorGradingSettings.Highlights.Offset;
					CCHighlights.HighlightsMin = Region->ColorGradingSettings.HighlightsMin;
				}
			}

#if CLIP_PIXELS_OUTSIDE_AABB
			// In case this is a second pass we need to clear the viewport in the backbuffer texture.
			// We don't need to clear the entire texture, just the render viewport.
			if (BackBufferRenderTarget.LoadAction == ERenderTargetLoadAction::ELoad)
			{
				FClearRectPS::FParameters* Parameters = GraphBuilder.AllocParameters<FClearRectPS::FParameters>();
				TShaderMapRef<FClearRectPS> CopyPixelShader(GlobalShaderMap);
				TShaderMapRef<FColorCorrectScreenPassVS> ScreenPassVS(GlobalShaderMap);
				Parameters->RenderTargets[0] = BackBufferRenderTarget.GetRenderTargetBinding();

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("ColorCorrectRegions_ClearViewport"),
					Parameters,
					ERDGPassFlags::Raster,
					[&View, ScreenPassVS, CopyPixelShader, RegionViewport, Parameters, DefaultBlendState](FRHICommandListImmediate& RHICmdList)
				{
					DrawScreenPass(
						RHICmdList,
						View,
						RegionViewport,
						RegionViewport,
						FScreenPassPipelineState(ScreenPassVS, CopyPixelShader, DefaultBlendState),
						[&](FRHICommandListImmediate&)
					{
						SetShaderParameters(RHICmdList, CopyPixelShader, CopyPixelShader.GetPixelShader(), *Parameters);
					});
				});
			}
#endif
			// Main region rendering.
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ColorCorrectRegions"),
				PostProcessMaterialParameters,
				ERDGPassFlags::Raster,
				[&View,
				RegionViewport,
				VertexShader,
				PixelShader,
				DefaultBlendState,
				DepthStencilState,
				PostProcessMaterialParameters,
				RegionData,
				CCBase,
				CCShadows,
				CCMidtones,
				CCHighlights,
				bIsAdvanced](FRHICommandListImmediate& RHICmdList)
			{
				
				DrawScreenPass(
					RHICmdList,
					View,
					RegionViewport, // Output Viewport
					RegionViewport, // Input Viewport
					FScreenPassPipelineState(VertexShader, PixelShader, DefaultBlendState, DepthStencilState),
					[&](FRHICommandListImmediate& RHICmdList)
				{
					SetUniformBufferParameterImmediate(RHICmdList, PixelShader.GetPixelShader(), PixelShader->GetUniformBufferParameter<FCCRRegionDataInputParameter>(), RegionData);
					SetUniformBufferParameterImmediate(RHICmdList, PixelShader.GetPixelShader(), PixelShader->GetUniformBufferParameter<FCCRColorCorrectParameter>(), CCBase);
					if (bIsAdvanced)
					{
						SetUniformBufferParameterImmediate(RHICmdList, PixelShader.GetPixelShader(), PixelShader->GetUniformBufferParameter<FCCRColorCorrectShadowsParameter>(), CCShadows);
						SetUniformBufferParameterImmediate(RHICmdList, PixelShader.GetPixelShader(), PixelShader->GetUniformBufferParameter<FCCRColorCorrectMidtonesParameter>(), CCMidtones);
						SetUniformBufferParameterImmediate(RHICmdList, PixelShader.GetPixelShader(), PixelShader->GetUniformBufferParameter<FCCRColorCorrectHighlightsParameter>(), CCHighlights);
					}
					VertexShader->SetParameters(RHICmdList,  View);
					SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *PostProcessMaterialParameters);

					PixelShader->SetParameters(RHICmdList, View);
					SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PostProcessMaterialParameters);
				});

			});

			// Since we've rendered into the backbuffer already we have to use load flag instead.
			BackBufferRenderTarget.LoadAction = ERenderTargetLoadAction::ELoad;

			FCopyRectPS::FParameters* Parameters = GraphBuilder.AllocParameters<FCopyRectPS::FParameters>();
			Parameters->InputTexture = BackBufferRenderTarget.Texture;
			Parameters->InputSampler = TStaticSamplerState<>::GetRHI();
			Parameters->RenderTargets[0] = SceneColorRenderTarget.GetRenderTargetBinding();

			TShaderMapRef<FCopyRectPS> CopyPixelShader(GlobalShaderMap);
			TShaderMapRef<FColorCorrectScreenPassVS> ScreenPassVS(GlobalShaderMap);

#if CLIP_PIXELS_OUTSIDE_AABB
			// Blending the output from the main step with scene color.
			// src.rgb*src.a + dest.rgb*(1.-src.a); alpha = src.a*0. + dst.a*1.0
			FRHIBlendState* CopyBlendState = TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
#else	
			FRHIBlendState* CopyBlendState = DefaultBlendState;
#endif
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ColorCorrectRegions_CopyViewport"),
				Parameters,
				ERDGPassFlags::Raster,
				[&View, ScreenPassVS, CopyPixelShader, RegionViewport, Parameters, CopyBlendState](FRHICommandListImmediate& RHICmdList)
			{
				DrawScreenPass(
					RHICmdList,
					View,
					RegionViewport,
					RegionViewport,
					FScreenPassPipelineState(ScreenPassVS, CopyPixelShader, CopyBlendState),
					[&](FRHICommandListImmediate&)
				{
					SetShaderParameters(RHICmdList, CopyPixelShader, CopyPixelShader.GetPixelShader(), *Parameters);
				});
			});

		}

	}
}

