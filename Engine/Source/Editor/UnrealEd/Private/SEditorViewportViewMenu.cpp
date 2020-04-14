// Copyright Epic Games, Inc. All Rights Reserved.


#include "SEditorViewportViewMenu.h"
#include "SEditorViewportViewMenuContext.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ToolMenus.h"
#include "EditorStyleSet.h"
#include "EditorViewportCommands.h"
#include "RayTracingDebugVisualizationMenuCommands.h"
#include "RenderResource.h"

#define LOCTEXT_NAMESPACE "EditorViewportViewMenu"

const FName SEditorViewportViewMenu::BaseMenuName("UnrealEd.ViewportToolbar.View");

void SEditorViewportViewMenu::Construct( const FArguments& InArgs, TSharedRef<SEditorViewport> InViewport, TSharedRef<class SViewportToolBar> InParentToolBar )
{
	Viewport = InViewport;
	MenuName = BaseMenuName;
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

void SEditorViewportViewMenu::RegisterMenus() const
{
	if (!UToolMenus::Get()->IsMenuRegistered(BaseMenuName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(BaseMenuName);
		Menu->AddDynamicSection("BaseSection", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			if (UEditorViewportViewMenuContext* Context = InMenu->FindContext<UEditorViewportViewMenuContext>())
			{
				Context->EditorViewportViewMenu.Pin()->FillViewMenu(InMenu);
			}
		}));
	}
}

TSharedRef<SWidget> SEditorViewportViewMenu::GenerateViewMenuContent() const
{
	RegisterMenus();

	UEditorViewportViewMenuContext* ContextObject = NewObject<UEditorViewportViewMenuContext>();
	ContextObject->EditorViewportViewMenu = SharedThis(this);

	FToolMenuContext MenuContext(Viewport.Pin()->GetCommandList(), MenuExtenders, ContextObject);
	return UToolMenus::Get()->GenerateWidget(MenuName, MenuContext);
}

void SEditorViewportViewMenu::FillViewMenu(UToolMenu* Menu) const
{
	const FEditorViewportCommands& BaseViewportActions = FEditorViewportCommands::Get();

	{
		// View modes
		{
			FToolMenuSection& Section = Menu->AddSection("ViewMode", LOCTEXT("ViewModeHeader", "View Mode"));
			{
				Section.AddMenuEntry(BaseViewportActions.LitMode, UViewModeUtils::GetViewModeDisplayName(VMI_Lit));
				Section.AddMenuEntry(BaseViewportActions.UnlitMode, UViewModeUtils::GetViewModeDisplayName(VMI_Unlit));
				Section.AddMenuEntry(BaseViewportActions.WireframeMode, UViewModeUtils::GetViewModeDisplayName(VMI_BrushWireframe));
				Section.AddMenuEntry(BaseViewportActions.DetailLightingMode, UViewModeUtils::GetViewModeDisplayName(VMI_Lit_DetailLighting));
				Section.AddMenuEntry(BaseViewportActions.LightingOnlyMode, UViewModeUtils::GetViewModeDisplayName(VMI_LightingOnly));
				Section.AddMenuEntry(BaseViewportActions.ReflectionOverrideMode, UViewModeUtils::GetViewModeDisplayName(VMI_ReflectionOverride));
				Section.AddMenuEntry(BaseViewportActions.CollisionPawn, UViewModeUtils::GetViewModeDisplayName(VMI_CollisionPawn));
				Section.AddMenuEntry(BaseViewportActions.CollisionVisibility, UViewModeUtils::GetViewModeDisplayName(VMI_CollisionVisibility));
			}

#if RHI_RAYTRACING
			if (IsRayTracingEnabled())
			{
				Section.AddMenuEntry(BaseViewportActions.PathTracingMode, UViewModeUtils::GetViewModeDisplayName(VMI_PathTracing));
			}
#endif

			// Optimization
			{
				struct Local
				{
					static void BuildOptimizationMenu( UToolMenu* Menu, TWeakPtr< SViewportToolBar > InParentToolBar )
					{
						const FEditorViewportCommands& BaseViewportCommands = FEditorViewportCommands::Get();

						UWorld* World = GWorld;
						const ERHIFeatureLevel::Type FeatureLevel = (IsInGameThread() && World) ? (ERHIFeatureLevel::Type)World->FeatureLevel : GMaxRHIFeatureLevel;

						{
							FToolMenuSection& Section = Menu->AddSection("OptimizationViewmodes", LOCTEXT("OptimizationSubMenuHeader", "Optimization Viewmodes"));
							if (FeatureLevel == ERHIFeatureLevel::SM5)
							{
								Section.AddMenuEntry(BaseViewportCommands.LightComplexityMode, UViewModeUtils::GetViewModeDisplayName(VMI_LightComplexity));
								if (IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"))->GetValueOnAnyThread() != 0)
								{
									Section.AddMenuEntry(BaseViewportCommands.LightmapDensityMode, UViewModeUtils::GetViewModeDisplayName(VMI_LightmapDensity));
								}
								Section.AddMenuEntry(BaseViewportCommands.StationaryLightOverlapMode, UViewModeUtils::GetViewModeDisplayName(VMI_StationaryLightOverlap));
							}

							Section.AddMenuEntry(BaseViewportCommands.ShaderComplexityMode, UViewModeUtils::GetViewModeDisplayName(VMI_ShaderComplexity));

							if (AllowDebugViewShaderMode(DVSM_ShaderComplexityContainedQuadOverhead, GMaxRHIShaderPlatform, FeatureLevel))
							{
								Section.AddMenuEntry(BaseViewportCommands.ShaderComplexityWithQuadOverdrawMode, UViewModeUtils::GetViewModeDisplayName(VMI_ShaderComplexityWithQuadOverdraw));
							}
							if (AllowDebugViewShaderMode(DVSM_QuadComplexity, GMaxRHIShaderPlatform, FeatureLevel))
							{
								Section.AddMenuEntry(BaseViewportCommands.QuadOverdrawMode, UViewModeUtils::GetViewModeDisplayName(VMI_QuadOverdraw));
							}
						}

						{
							FToolMenuSection& Section = Menu->AddSection("TextureStreaming", LOCTEXT("TextureStreamingHeader", "Texture Streaming Accuracy"));
							if (AllowDebugViewShaderMode(DVSM_PrimitiveDistanceAccuracy, GMaxRHIShaderPlatform, FeatureLevel) && (!InParentToolBar.IsValid() || InParentToolBar.Pin()->IsViewModeSupported(VMI_PrimitiveDistanceAccuracy)))
							{
								Section.AddMenuEntry(BaseViewportCommands.TexStreamAccPrimitiveDistanceMode, UViewModeUtils::GetViewModeDisplayName(VMI_PrimitiveDistanceAccuracy));
							}
							if (AllowDebugViewShaderMode(DVSM_MeshUVDensityAccuracy, GMaxRHIShaderPlatform, FeatureLevel) && (!InParentToolBar.IsValid() || InParentToolBar.Pin()->IsViewModeSupported(VMI_MeshUVDensityAccuracy)))
							{
								Section.AddMenuEntry(BaseViewportCommands.TexStreamAccMeshUVDensityMode, UViewModeUtils::GetViewModeDisplayName(VMI_MeshUVDensityAccuracy));
							}
							// TexCoordScale accuracy viewmode requires shaders that are only built in the TextureStreamingBuild, which requires the new metrics to be enabled.
							if (AllowDebugViewShaderMode(DVSM_MaterialTextureScaleAccuracy, GMaxRHIShaderPlatform, FeatureLevel) && CVarStreamingUseNewMetrics.GetValueOnAnyThread() != 0 && (!InParentToolBar.IsValid() || InParentToolBar.Pin()->IsViewModeSupported(VMI_MaterialTextureScaleAccuracy)))
							{
								Section.AddMenuEntry(BaseViewportCommands.TexStreamAccMaterialTextureScaleMode, UViewModeUtils::GetViewModeDisplayName(VMI_MaterialTextureScaleAccuracy));
							}
							if (AllowDebugViewShaderMode(DVSM_RequiredTextureResolution, GMaxRHIShaderPlatform, FeatureLevel) && (!InParentToolBar.IsValid() || InParentToolBar.Pin()->IsViewModeSupported(VMI_MaterialTextureScaleAccuracy)))
							{
								Section.AddMenuEntry(BaseViewportCommands.RequiredTextureResolutionMode, UViewModeUtils::GetViewModeDisplayName(VMI_RequiredTextureResolution));
							}
						}
					}
				};

				Section.AddSubMenu(
					"OptimizationSubMenu",
					LOCTEXT("OptimizationSubMenu", "Optimization Viewmodes"), LOCTEXT("Optimization_ToolTip", "Select optimization visualizer"),
					FNewToolMenuDelegate::CreateStatic(&Local::BuildOptimizationMenu, ParentToolBar),
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
					EUserInterfaceActionType::RadioButton,
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

				Section.AddSubMenu("RayTracingDebugSubMenu", LOCTEXT("RayTracingDebugSubMenu", "Ray Tracing Debug"), LOCTEXT("RayTracing_ToolTip", "Select ray tracing buffer visualization view modes"), FNewMenuDelegate::CreateStatic(&Local::BuildRayTracingDebugMenu, ParentToolBar));
			}
#endif

			{
				struct Local
				{
					static void BuildLODMenu(UToolMenu* Menu)
					{
						{
							FToolMenuSection& Section = Menu->AddSection("LevelViewportLODColoration", LOCTEXT("LODModesHeader", "Level of Detail Coloration"));
							Section.AddMenuEntry(FEditorViewportCommands::Get().LODColorationMode, UViewModeUtils::GetViewModeDisplayName(VMI_LODColoration));
							Section.AddMenuEntry(FEditorViewportCommands::Get().HLODColorationMode, UViewModeUtils::GetViewModeDisplayName(VMI_HLODColoration));
						}
					}
				};

				Section.AddSubMenu(
					"VisualizeGroupedLOD",
					LOCTEXT("VisualizeGroupedLODDisplayName", "Level of Detail Coloration"), LOCTEXT("GroupedLODMenu_ToolTip", "Select a mode for LOD Coloration"),
					FNewToolMenuDelegate::CreateStatic(&Local::BuildLODMenu),
					FUIAction(FExecuteAction(), FCanExecuteAction(),
						FIsActionChecked::CreateLambda([this]()
						{
								const TSharedRef<SEditorViewport> ViewportRef = Viewport.Pin().ToSharedRef();
								const TSharedPtr<FEditorViewportClient> ViewportClient = ViewportRef->GetViewportClient();
								check(ViewportClient.IsValid());
								const EViewModeIndex ViewMode = ViewportClient->GetViewMode();
								return (ViewMode == VMI_LODColoration || ViewMode == VMI_HLODColoration);
						})),
					EUserInterfaceActionType::RadioButton,
					/* bInOpenSubMenuOnClick = */ false, FSlateIcon(FEditorStyle::GetStyleSetName(), "EditorViewport.GroupLODColorationMode"));
			}
		}

		// Auto Exposure
		{
			const FEditorViewportCommands& BaseViewportCommands = FEditorViewportCommands::Get();

			TSharedRef<SWidget> FixedEV100Menu = Viewport.Pin()->BuildFixedEV100Menu();
			TSharedPtr<FEditorViewportClient> EditorViewPostClient = Viewport.Pin()->GetViewportClient();
			const bool bIsLevelEditor = EditorViewPostClient.IsValid() && EditorViewPostClient->IsLevelEditorClient();

			FToolMenuSection& Section = Menu->AddSection("Exposure", LOCTEXT("ExposureHeader", "Exposure"));
			Section.AddMenuEntry(bIsLevelEditor ? BaseViewportCommands.ToggleInGameExposure : BaseViewportCommands.ToggleAutoExposure);
			Section.AddEntry(FToolMenuEntry::InitWidget("FixedEV100", FixedEV100Menu, LOCTEXT("FixedEV100", "EV100")));
		}
	}
}

#undef LOCTEXT_NAMESPACE
