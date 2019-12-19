// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "SEditorViewportViewMenu.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"
#include "EditorViewportCommands.h"
#include "RayTracingDebugVisualizationMenuCommands.h"
#include "RenderResource.h"

#define LOCTEXT_NAMESPACE "EditorViewportViewMenu"

void SEditorViewportViewMenu::Construct( const FArguments& InArgs, TSharedRef<SEditorViewport> InViewport, TSharedRef<class SViewportToolBar> InParentToolBar )
{
	Viewport = InViewport;
	MenuExtenders = InArgs._MenuExtenders;

	SEditorViewportToolbarMenu::Construct
	(
		SEditorViewportToolbarMenu::FArguments()
			.ParentToolBar( InParentToolBar)
			.Cursor( EMouseCursor::Default )
			.Label(this, &SEditorViewportViewMenu::GetViewMenuLabel)
			.LabelIcon(this, &SEditorViewportViewMenu::GetViewMenuLabelIcon)
			.OnGetMenuContent( this, &SEditorViewportViewMenu::GenerateViewMenuContent )
	);
}

FText SEditorViewportViewMenu::GetViewMenuLabel() const
{
	FText Label = LOCTEXT("ViewMenuTitle_Default", "View");
	TSharedPtr< SEditorViewport > PinnedViewport = Viewport.Pin();
	if( PinnedViewport.IsValid() )
	{
		const TSharedPtr<FEditorViewportClient> ViewportClient = PinnedViewport->GetViewportClient();
		check(ViewportClient.IsValid());
		const EViewModeIndex ViewMode = ViewportClient->GetViewMode();
		// If VMI_VisualizeBuffer, return its subcategory name
		if (ViewMode == VMI_VisualizeBuffer)
		{
			Label = ViewportClient->GetCurrentBufferVisualizationModeDisplayName();
		}
		// For any other category, return its own name
		else
		{
			Label = UViewModeUtils::GetViewModeDisplayName(ViewMode);
		}
	}

	return Label;
}

const FSlateBrush* SEditorViewportViewMenu::GetViewMenuLabelIcon() const
{
	FName Icon = NAME_None;
	TSharedPtr< SEditorViewport > PinnedViewport = Viewport.Pin();
	if( PinnedViewport.IsValid() )
	{
		static FName WireframeIcon( "EditorViewport.WireframeMode" );
		static FName UnlitIcon( "EditorViewport.UnlitMode" );
		static FName LitIcon( "EditorViewport.LitMode" );
		static FName DetailLightingIcon("EditorViewport.DetailLightingMode");
		static FName LightingOnlyIcon("EditorViewport.LightingOnlyMode");
		static FName LightComplexityIcon("EditorViewport.LightComplexityMode");
		static FName ShaderComplexityIcon("EditorViewport.ShaderComplexityMode");
		static FName QuadOverdrawIcon("EditorViewport.QuadOverdrawMode");
		static FName ShaderComplexityWithQuadOverdrawIcon("EditorViewport.ShaderCOmplexityWithQuadOverdrawMode");
		static FName PrimitiveDistanceAccuracyIcon("EditorViewport.TexStreamAccPrimitiveDistanceMode");
		static FName MeshUVDensityAccuracyIcon("EditorViewport.TexStreamAccMeshUVDensityMode");
		static FName MaterialTextureScaleAccuracyIcon("EditorViewport.TexStreamAccMaterialTextureScaleMode");
		static FName RequiredTextureResolutionIcon("EditorViewport.RequiredTextureResolutionMode");
		static FName LightOverlapIcon("EditorViewport.StationaryLightOverlapMode");
		static FName LightmapDensityIcon("EditorViewport.LightmapDensityMode");
		static FName ReflectionModeIcon("EditorViewport.ReflectionOverrideMode");
		static FName LODColorationIcon("EditorViewport.LODColorationMode");
		static FName VisualizeBufferIcon("EditorViewport.VisualizeBufferMode");
		static FName CollisionPawnIcon("EditorViewport.CollisionPawn");
		static FName CollisionVisibilityIcon("EditorViewport.CollisionVisibility");

		switch( PinnedViewport->GetViewportClient()->GetViewMode() )
		{
			case VMI_BrushWireframe:
				Icon = WireframeIcon;
				break;

			case VMI_Wireframe:
				Icon = WireframeIcon;
				break;

			case VMI_Unlit:
				Icon = UnlitIcon;
				break;

			case VMI_Lit:
				Icon = LitIcon;
				break;

#if RHI_RAYTRACING
			// #dxr_todo: UE-72550 use special icons for ray tracing view modes
			case VMI_RayTracingDebug:
				Icon = LitIcon; 
				break;

			case VMI_PathTracing:
				Icon = LitIcon; 
				break;
#endif
			case VMI_Lit_DetailLighting:
				Icon = DetailLightingIcon;
				break;

			case VMI_LightingOnly:
				Icon = LightingOnlyIcon;
				break;

			case VMI_LightComplexity:
				Icon = LightComplexityIcon;
				break;

			case VMI_ShaderComplexity:
				Icon = ShaderComplexityIcon;
				break;

			case VMI_QuadOverdraw:
				Icon = QuadOverdrawIcon;
				break;

			case VMI_ShaderComplexityWithQuadOverdraw:
				Icon = ShaderComplexityWithQuadOverdrawIcon;
				break;

			case VMI_PrimitiveDistanceAccuracy:
				Icon = PrimitiveDistanceAccuracyIcon;
				break;

			case VMI_MeshUVDensityAccuracy:
				Icon = MeshUVDensityAccuracyIcon;
				break;

			case VMI_MaterialTextureScaleAccuracy:
				Icon = MaterialTextureScaleAccuracyIcon;
				break;

			case VMI_RequiredTextureResolution:
				Icon = RequiredTextureResolutionIcon;
				break;

			case VMI_StationaryLightOverlap:
				Icon = LightOverlapIcon;
				break;

			case VMI_LightmapDensity:
				Icon = LightmapDensityIcon;
				break;

			case VMI_ReflectionOverride:
				Icon = ReflectionModeIcon;
				break;

			case VMI_VisualizeBuffer:
				Icon = VisualizeBufferIcon;
				break;

			case VMI_CollisionPawn:
				Icon = CollisionPawnIcon;
				break;

			case VMI_CollisionVisibility:
				Icon = CollisionVisibilityIcon;
				break;

			case VMI_LitLightmapDensity:
				break;

			case VMI_LODColoration:
				Icon = LODColorationIcon;
				break;

			case VMI_HLODColoration:
				Icon = LODColorationIcon;
				break;

			case VMI_GroupLODColoration:
				Icon = LODColorationIcon;
				break;
		}
	}

	return FEditorStyle::GetBrush(Icon);
}

TSharedRef<SWidget> SEditorViewportViewMenu::GenerateViewMenuContent() const
{
	const FEditorViewportCommands& BaseViewportActions = FEditorViewportCommands::Get();

	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder ViewMenuBuilder(bInShouldCloseWindowAfterMenuSelection, Viewport.Pin()->GetCommandList(), MenuExtenders);

	{
		// View modes
		{
			ViewMenuBuilder.BeginSection("ViewMode", LOCTEXT("ViewModeHeader", "View Mode") );
			{
				ViewMenuBuilder.AddMenuEntry(BaseViewportActions.LitMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_Lit));
				ViewMenuBuilder.AddMenuEntry(BaseViewportActions.UnlitMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_Unlit));
				ViewMenuBuilder.AddMenuEntry(BaseViewportActions.WireframeMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_BrushWireframe));
				ViewMenuBuilder.AddMenuEntry(BaseViewportActions.DetailLightingMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_Lit_DetailLighting));
				ViewMenuBuilder.AddMenuEntry(BaseViewportActions.LightingOnlyMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_LightingOnly));
				ViewMenuBuilder.AddMenuEntry(BaseViewportActions.ReflectionOverrideMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_ReflectionOverride));
				ViewMenuBuilder.AddMenuEntry(BaseViewportActions.CollisionPawn, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_CollisionPawn));
				ViewMenuBuilder.AddMenuEntry(BaseViewportActions.CollisionVisibility, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_CollisionVisibility));
			}

#if RHI_RAYTRACING
			if (IsRayTracingEnabled())
			{
				const FEditorViewportCommands& BaseViewportCommands = FEditorViewportCommands::Get();
				ViewMenuBuilder.AddMenuEntry(BaseViewportCommands.PathTracingMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_PathTracing));
			}
#endif

			// Optimization
			{
				struct Local
				{
					static void BuildOptimizationMenu( FMenuBuilder& Menu, TWeakPtr< SViewportToolBar > InParentToolBar )
					{
						const FEditorViewportCommands& BaseViewportCommands = FEditorViewportCommands::Get();

						UWorld* World = GWorld;
						const ERHIFeatureLevel::Type FeatureLevel = (IsInGameThread() && World) ? (ERHIFeatureLevel::Type)World->FeatureLevel : GMaxRHIFeatureLevel;

						Menu.BeginSection("OptimizationViewmodes", LOCTEXT("OptimizationSubMenuHeader", "Optimization Viewmodes"));
						{
							if (FeatureLevel == ERHIFeatureLevel::SM5)
							{
								Menu.AddMenuEntry(BaseViewportCommands.LightComplexityMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_LightComplexity));
								Menu.AddMenuEntry(BaseViewportCommands.LightmapDensityMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_LightmapDensity));
								Menu.AddMenuEntry(BaseViewportCommands.StationaryLightOverlapMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_StationaryLightOverlap));
							}

							Menu.AddMenuEntry(BaseViewportCommands.ShaderComplexityMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_ShaderComplexity));

							if (AllowDebugViewShaderMode(DVSM_ShaderComplexityContainedQuadOverhead, GMaxRHIShaderPlatform, FeatureLevel))
							{
								Menu.AddMenuEntry(BaseViewportCommands.ShaderComplexityWithQuadOverdrawMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_ShaderComplexityWithQuadOverdraw));
							}
							if (AllowDebugViewShaderMode(DVSM_QuadComplexity, GMaxRHIShaderPlatform, FeatureLevel))
							{
								Menu.AddMenuEntry(BaseViewportCommands.QuadOverdrawMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_QuadOverdraw));
							}
						}
						Menu.EndSection();

						Menu.BeginSection("TextureStreaming", LOCTEXT("TextureStreamingHeader", "Texture Streaming Accuracy") );
						if ( AllowDebugViewShaderMode(DVSM_PrimitiveDistanceAccuracy, GMaxRHIShaderPlatform, FeatureLevel) && (!InParentToolBar.IsValid() || InParentToolBar.Pin()->IsViewModeSupported(VMI_PrimitiveDistanceAccuracy)) )
						{
							Menu.AddMenuEntry(BaseViewportCommands.TexStreamAccPrimitiveDistanceMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_PrimitiveDistanceAccuracy));
						}
						if ( AllowDebugViewShaderMode(DVSM_MeshUVDensityAccuracy, GMaxRHIShaderPlatform, FeatureLevel) && (!InParentToolBar.IsValid() || InParentToolBar.Pin()->IsViewModeSupported(VMI_MeshUVDensityAccuracy)) )
						{
							Menu.AddMenuEntry(BaseViewportCommands.TexStreamAccMeshUVDensityMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_MeshUVDensityAccuracy));
						}
						// TexCoordScale accuracy viewmode requires shaders that are only built in the TextureStreamingBuild, which requires the new metrics to be enabled.
						if ( AllowDebugViewShaderMode(DVSM_MaterialTextureScaleAccuracy, GMaxRHIShaderPlatform, FeatureLevel) && CVarStreamingUseNewMetrics.GetValueOnAnyThread() != 0 && (!InParentToolBar.IsValid() || InParentToolBar.Pin()->IsViewModeSupported(VMI_MaterialTextureScaleAccuracy)) )
						{
							Menu.AddMenuEntry(BaseViewportCommands.TexStreamAccMaterialTextureScaleMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_MaterialTextureScaleAccuracy));
						}
						if ( AllowDebugViewShaderMode(DVSM_RequiredTextureResolution, GMaxRHIShaderPlatform, FeatureLevel) && (!InParentToolBar.IsValid() || InParentToolBar.Pin()->IsViewModeSupported(VMI_MaterialTextureScaleAccuracy)) )
						{
							Menu.AddMenuEntry(BaseViewportCommands.RequiredTextureResolutionMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_RequiredTextureResolution));
						}
						Menu.EndSection();
					}
				};

				ViewMenuBuilder.AddSubMenu(
					LOCTEXT("OptimizationSubMenu", "Optimization Viewmodes"), LOCTEXT("Optimization_ToolTip", "Select optimization visualizer"),
					FNewMenuDelegate::CreateStatic(&Local::BuildOptimizationMenu, ParentToolBar),
					FUIAction(FExecuteAction(), FCanExecuteAction(),
						FIsActionChecked::CreateLambda([this]()
						{
								const TSharedRef<SEditorViewport> ViewportRef = Viewport.Pin().ToSharedRef();
								const TSharedPtr<FEditorViewportClient> ViewportClient = ViewportRef->GetViewportClient();
								check(ViewportClient.IsValid());
								const EViewModeIndex ViewMode = ViewportClient->GetViewMode();
								return (
									// Texture Streaming Accuracy
									ViewMode == VMI_LightComplexity || ViewMode == VMI_LightmapDensity || ViewMode == VMI_StationaryLightOverlap
									|| ViewMode == VMI_ShaderComplexity || ViewMode == VMI_ShaderComplexityWithQuadOverdraw || ViewMode == VMI_QuadOverdraw
									// Texture Streaming Accuracy
									|| ViewMode == VMI_PrimitiveDistanceAccuracy || ViewMode == VMI_MeshUVDensityAccuracy || ViewMode == VMI_MaterialTextureScaleAccuracy || ViewMode == VMI_RequiredTextureResolution
								);
						})),
					/* InExtensionHook = */ NAME_None, EUserInterfaceActionType::RadioButton,
					/* bInOpenSubMenuOnClick = */ false, FSlateIcon(FEditorStyle::GetStyleSetName(), "EditorViewport.QuadOverdrawMode"));
			}

#if RHI_RAYTRACING
			if (IsRayTracingEnabled())
			{
				struct Local
				{
					static void BuildRayTracingDebugMenu(FMenuBuilder& Menu, TWeakPtr< SViewportToolBar > InParentToolBar)
					{
						const FRayTracingDebugVisualizationMenuCommands& RtDebugCommands = FRayTracingDebugVisualizationMenuCommands::Get();
						RtDebugCommands.BuildVisualisationSubMenu(Menu);
					}
				};

				ViewMenuBuilder.AddSubMenu(LOCTEXT("RayTracingDebugSubMenu", "Ray Tracing Debug"), LOCTEXT("RayTracing_ToolTip", "Select ray tracing buffer visualization view modes"), FNewMenuDelegate::CreateStatic(&Local::BuildRayTracingDebugMenu, ParentToolBar));
			}
#endif

			{
				struct Local
				{
					static void BuildLODMenu(FMenuBuilder& Menu)
					{
						Menu.BeginSection("LevelViewportLODColoration", LOCTEXT("LODModesHeader", "Level of Detail Coloration"));
						{
							Menu.AddMenuEntry(FEditorViewportCommands::Get().LODColorationMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_LODColoration));
							Menu.AddMenuEntry(FEditorViewportCommands::Get().HLODColorationMode, NAME_None, UViewModeUtils::GetViewModeDisplayName(VMI_HLODColoration));
						}
						Menu.EndSection();
					}
				};

				ViewMenuBuilder.AddSubMenu(
					LOCTEXT("VisualizeGroupedLODDisplayName", "Level of Detail Coloration"), LOCTEXT("GroupedLODMenu_ToolTip", "Select a mode for LOD Coloration"),
					FNewMenuDelegate::CreateStatic(&Local::BuildLODMenu),
					FUIAction(FExecuteAction(), FCanExecuteAction(),
						FIsActionChecked::CreateLambda([this]()
						{
								const TSharedRef<SEditorViewport> ViewportRef = Viewport.Pin().ToSharedRef();
								const TSharedPtr<FEditorViewportClient> ViewportClient = ViewportRef->GetViewportClient();
								check(ViewportClient.IsValid());
								const EViewModeIndex ViewMode = ViewportClient->GetViewMode();
								return (ViewMode == VMI_LODColoration || ViewMode == VMI_HLODColoration);
						})),
					/* InExtensionHook = */ NAME_None, EUserInterfaceActionType::RadioButton,
					/* bInOpenSubMenuOnClick = */ false, FSlateIcon(FEditorStyle::GetStyleSetName(), "EditorViewport.GroupLODColorationMode"));
			}

			ViewMenuBuilder.EndSection();
		}

		// Auto Exposure
		{
			const FEditorViewportCommands& BaseViewportCommands = FEditorViewportCommands::Get();

			TSharedRef<SWidget> FixedEV100Menu = Viewport.Pin()->BuildFixedEV100Menu();
			TSharedPtr<FEditorViewportClient> EditorViewPostClient = Viewport.Pin()->GetViewportClient();
			const bool bIsLevelEditor = EditorViewPostClient.IsValid() && EditorViewPostClient->IsLevelEditorClient();

			ViewMenuBuilder.BeginSection("Exposure", LOCTEXT("ExposureHeader", "Exposure"));
			ViewMenuBuilder.AddMenuEntry( bIsLevelEditor ? BaseViewportCommands.ToggleInGameExposure : BaseViewportCommands.ToggleAutoExposure, NAME_None );
			ViewMenuBuilder.AddWidget( FixedEV100Menu, LOCTEXT("FixedEV100", "EV100") );
			ViewMenuBuilder.EndSection();
		}
	}
	return ViewMenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
