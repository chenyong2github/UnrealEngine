// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieGraphDefaultRenderer.h"
#include "Graph/MovieGraphPipeline.h"
#include "Graph/MovieGraphConfig.h"
#include "Graph/Nodes/MovieGraphRenderLayerNode.h"
#include "Graph/Nodes/MovieGraphRenderPassNode.h"
#include "MovieRenderPipelineCoreModule.h"
#include "RenderingThread.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/Package.h"

void UMovieGraphDefaultRenderer::SetupRenderingPipelineForShot(UMoviePipelineExecutorShot* InShot)
{
	// Iterate through the graph config and look for Render Layers.
	UMovieGraphConfig* RootGraph = GetOwningGraph()->GetRootGraphForShot(InShot);

	struct FMovieGraphPass
	{
		TSubclassOf<UMovieGraphRenderPassNode> ClassType;
		TArray<FString> LayerNames;
	};

	TArray<FMovieGraphPass> OutputPasses;

	// Start by getting our root set of branches we should follow
	TArray<FMovieGraphBranch> GraphBranches = RootGraph->GetOutputBranches();
	for (const FMovieGraphBranch& Branch : GraphBranches)
	{
		// We follow each branch looking for Render Layer nodes to figure out what render layer this should be.
		FMovieGraphTraversalContext TraversalContext = GetOwningGraph()->GetTraversalContextForShot(InShot);
		TraversalContext.RootBranch = Branch;

		FString RenderLayerName = TEXT("Unnamed_Layer");
		UMovieGraphRenderLayerNode* FirstRenderLayerNode = RootGraph->IterateGraphForClass<UMovieGraphRenderLayerNode>(TraversalContext);
		if (FirstRenderLayerNode)
		{
			RenderLayerName = FirstRenderLayerNode->GetRenderLayerName();;
		}

		// Now we need to figure out which renderers are on this branch.
		TArray<UMovieGraphRenderPassNode*> Renderers = RootGraph->IterateGraphForClassAll<UMovieGraphRenderPassNode>(TraversalContext);
		if (FirstRenderLayerNode && Renderers.Num() == 0)
		{
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Found RenderLayer: \"%s\" but no Renderers defined."), *RenderLayerName);
		}

		for (UMovieGraphRenderPassNode* RenderPassNode : Renderers)
		{
			FString RendererName = RenderPassNode->GetRendererName();

			FMovieGraphPass* ExistingPass = OutputPasses.FindByPredicate([RenderPassNode](const FMovieGraphPass& Pass)
				{ return Pass.ClassType == RenderPassNode->GetClass(); });
			
			if (!ExistingPass)
			{
				ExistingPass = &OutputPasses.AddDefaulted_GetRef();
				ExistingPass->ClassType = RenderPassNode->GetClass();
			}
			ExistingPass->LayerNames.AddUnique(RenderLayerName);
		}
	}

	UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Found: %d Render Passes:"), OutputPasses.Num());
	int32 TotalLayerCount = 0;
	for (const FMovieGraphPass& Pass : OutputPasses)
	{
		// ToDo: This should probably come from the Renderers themselves, as they can internally produce multiple
		// renders (such as ObjectID passes).
		TotalLayerCount += Pass.LayerNames.Num();
		
		FMovieGraphRenderPassSetupData SetupData;
		SetupData.Renderer = this;
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("\tRenderer Class: %s"), *Pass.ClassType->GetName());
		for (const FString& LayerName : Pass.LayerNames)
		{
			FMovieGraphRenderPassLayerData& LayerData = SetupData.Layers.AddDefaulted_GetRef();
			LayerData.LayerName = LayerName;

			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("\t\tLayer Name: %s"), *LayerName);
		}

		UMovieGraphRenderPassNode* RenderPassCDO = Pass.ClassType->GetDefaultObject<UMovieGraphRenderPassNode>();
		RenderPassCDO->Setup(SetupData);

		RenderPassesInUse.Add(RenderPassCDO);
	}

	UE_LOG(LogMovieRenderPipeline, Log, TEXT("Finished initializing %d Render Passes (with %d total layers)."), OutputPasses.Num(), TotalLayerCount);
}

void UMovieGraphDefaultRenderer::TeardownRenderingPipelineForShot(UMoviePipelineExecutorShot* InShot)
{
	FlushRenderingCommands();

	for (TObjectPtr< UMovieGraphRenderPassNode> RenderPass : RenderPassesInUse)
	{
		RenderPass->Teardown();
	}

	RenderPassesInUse.Reset();

	// ToDo: This could probably be preserved across shots to avoid allocations
	PooledViewRenderTargets.Reset();
}


void UMovieGraphDefaultRenderer::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	UMovieGraphDefaultRenderer* This = CastChecked<UMovieGraphDefaultRenderer>(InThis);

	// Can't be a const& due to AddStableReference API
	for (TPair<FRenderTargetInitParams, TObjectPtr<UTextureRenderTarget2D>>& KVP : This->PooledViewRenderTargets)
	{
		Collector.AddStableReference(&KVP.Value);
	}
}


void UMovieGraphDefaultRenderer::Render(const FMovieGraphTimeStepData& InTimeStepData)
{
	for (TObjectPtr<UMovieGraphRenderPassNode> RenderPass : RenderPassesInUse)
	{
		RenderPass->Render();
	}
}


UTextureRenderTarget2D* UMovieGraphDefaultRenderer::GetOrCreateViewRenderTarget(const FRenderTargetInitParams& InInitParams)
{
	if (const TObjectPtr<UTextureRenderTarget2D>* ExistViewRenderTarget = PooledViewRenderTargets.Find(InInitParams))
	{
		return *ExistViewRenderTarget;
	}

	const TObjectPtr<UTextureRenderTarget2D> NewViewRenderTarget = CreateViewRenderTarget(InInitParams);
	PooledViewRenderTargets.Emplace(InInitParams, NewViewRenderTarget);

	return NewViewRenderTarget.Get();
}

TObjectPtr<UTextureRenderTarget2D> UMovieGraphDefaultRenderer::CreateViewRenderTarget(const FRenderTargetInitParams& InInitParams) const
{
	TObjectPtr<UTextureRenderTarget2D> NewTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	NewTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	NewTarget->TargetGamma = InInitParams.TargetGamma;
	NewTarget->InitCustomFormat(InInitParams.Size.X, InInitParams.Size.Y, InInitParams.PixelFormat, false);
	int32 ResourceSizeBytes = NewTarget->GetResourceSizeBytes(EResourceSizeMode::Type::EstimatedTotal);
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("Allocated a View Render Target sized: (%d, %d), Bytes: %d"), InInitParams.Size.X, InInitParams.Size.Y, ResourceSizeBytes);

	return NewTarget;
}
