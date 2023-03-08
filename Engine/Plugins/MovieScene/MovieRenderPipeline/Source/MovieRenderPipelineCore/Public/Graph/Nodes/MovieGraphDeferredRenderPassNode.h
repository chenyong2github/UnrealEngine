// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Graph/Nodes/MovieGraphRenderPassNode.h"
#include "Graph/MovieGraphDataTypes.h"
#include "SceneTypes.h"
#include "Camera/CameraTypes.h"
#include "MovieRenderPipelineDataTypes.h"
#include "Engine/EngineTypes.h"
#include "MovieGraphDeferredRenderPassNode.generated.h"

// Forward Declares
class UMovieGraphDefaultRenderer;
struct FMovieGraphRenderPassSetupData;
struct FMovieGraphRenderPassLayerData;

// For FViewFamilyContextInitData
class FRenderTarget;
class UWorld;
class FSceneView;
class AActor;
class FSceneViewFamilyContext;


UCLASS()
class UMovieGraphDeferredRenderPassNode : public UMovieGraphRenderPassNode
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual FText GetMenuDescription() const override
	{
		return NSLOCTEXT("MovieGraphNodes", "DeferredRenderPassGraphNode_Description", "Deferred Renderer");
	}
#endif
protected:
	// UMovieGraphRenderPassNode Interface
	virtual FString GetRendererNameImpl() const override { return TEXT("Deferred"); }
	virtual void SetupImpl(const FMovieGraphRenderPassSetupData& InSetupData) override;
	virtual void TeardownImpl() override;
	virtual void RenderImpl() override;
	// ~UMovieGraphRenderPassNode Interface

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	struct FViewFamilyContextInitData
	{
		class FRenderTarget* RenderTarget;
		class UWorld* World;
		FMoviePipelineFrameOutputState::FTimeData TimeData;
		ESceneCaptureSource SceneCaptureSource;
		bool bWorldIsPaused;
		float GlobalScreenPercentageFraction;
		float OverscanFraction;
		int32 FrameIndex;
		bool bCameraCut;
		EAntiAliasingMethod AntiAliasingMethod;

		// Ownership of this will be passed to the FSceneViewFamilyContext
		FSceneView* View;

		FSceneViewStateInterface* SceneViewStateReference;

		// Camera Setup
		FMinimalViewInfo MinimalViewInfo;
		class AActor* ViewActor;
	};

	struct FMovieGraphDeferredRenderPass
	{
	public:
		void Setup(TWeakObjectPtr<UMovieGraphDefaultRenderer> InRenderer, const FMovieGraphRenderPassLayerData& InLayer);
		void Teardown();
		void Render();
		void AddReferencedObjects(FReferenceCollector& Collector);

	protected:
		TSharedRef<FSceneViewFamilyContext> AllocateSceneViewFamilyContext(const FViewFamilyContextInitData& InInitData);
		FSceneView* AllocateSceneView(TSharedPtr<FSceneViewFamilyContext> InViewFamilyContext, FViewFamilyContextInitData& InInitData) const;
		void ApplyMoviePipelineOverridesToViewFamily(TSharedRef<FSceneViewFamilyContext> InOutFamily, const FViewFamilyContextInitData& InInitData);



	protected:
		FMovieGraphRenderPassLayerData LayerData;

		// Scene View history used by the renderer 
		FSceneViewStateReference SceneViewState;

		TWeakObjectPtr<class UMovieGraphDefaultRenderer> Renderer;
	};

	TArray<TUniquePtr<FMovieGraphDeferredRenderPass>> CurrentInstances;
};