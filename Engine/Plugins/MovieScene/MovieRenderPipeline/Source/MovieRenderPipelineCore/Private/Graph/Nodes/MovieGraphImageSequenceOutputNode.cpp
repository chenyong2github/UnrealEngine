// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/Nodes/MovieGraphImageSequenceOutputNode.h"
#include "Graph/Nodes/MovieGraphOutputSettingNode.h"
#include "Graph/Nodes/MovieGraphRenderLayerNode.h"
#include "Graph/MovieGraphDataTypes.h"
#include "Graph/MovieGraphPipeline.h"
#include "Graph/MovieGraphConfig.h"
#include "Modules/ModuleManager.h"
#include "ImageWriteQueue.h"
#include "Misc/Paths.h"
#include "Async/TaskGraphInterfaces.h"

UMovieGraphImageSequenceOutputNode::UMovieGraphImageSequenceOutputNode()
{
	ImageWriteQueue = &FModuleManager::Get().LoadModuleChecked<IImageWriteQueueModule>("ImageWriteQueue").GetWriteQueue();
}

void UMovieGraphImageSequenceOutputNode::OnAllFramesSubmittedImpl()
{
	FinalizeFence = ImageWriteQueue->CreateFence();
}

bool UMovieGraphImageSequenceOutputNode::IsFinishedWritingToDiskImpl() const
{
	// Wait until the finalization fence is reached meaning we've written everything to disk.
	return Super::IsFinishedWritingToDiskImpl() && (!FinalizeFence.IsValid() || FinalizeFence.WaitFor(0));
}

void UMovieGraphImageSequenceOutputNode::OnReceiveImageDataImpl(UMovieGraphPipeline* InPipeline, UE::MovieGraph::FMovieGraphOutputMergerFrame* InRawFrameData, const TSet<FMovieGraphRenderDataIdentifier>& InMask)
{
	check(InRawFrameData);

	// ToDo:
	// The ImageWriteQueue is set up in a fire-and-forget manner. This means that the data needs to be placed in the WriteQueue
	// as a TUniquePtr (so it can free the data when its done). Unfortunately we can have multiple output formats at once,
	// so we can't MoveTemp the data into it, we need to make a copy (though we could optimize for the common case where there is
	// only one output format).
	// Copying can be expensive (3ms @ 1080p, 12ms at 4k for a single layer image) so ideally we'd like to do it on the task graph
	// but this isn't really compatible with the ImageWriteQueue API as we need the future returned by the ImageWriteQueue to happen
	// in order, so that we push our futures to the main Movie Pipeline in order, otherwise when we encode files to videos they'll
	// end up with frames out of order. A workaround for this would be to chain all of the send-to-imagewritequeue tasks to each
	// other with dependencies, but I'm not sure that's going to scale to the potentialy high data volume going wide MRQ will eventually
	// need.

	// The base ImageSequenceOutputNode doesn't support any multilayer formats, so we write out each render pass separately.
	for (TPair<FMovieGraphRenderDataIdentifier, TUniquePtr<FImagePixelData>>& RenderData : InRawFrameData->ImageOutputData)
	{
		// A layer within this output data may have chosen to not be written to disk by this CDO node
		if (!InMask.Contains(RenderData.Key))
		{
			continue;
		}

		UE::MovieGraph::FMovieGraphSampleState* Payload = RenderData.Value->GetPayload<UE::MovieGraph::FMovieGraphSampleState>();

		const bool bIncludeCDOs = true;
		UMovieGraphOutputSettingNode* OutputSettingNode = InRawFrameData->EvaluatedConfig->GetSettingForBranch<UMovieGraphOutputSettingNode>(RenderData.Key.RootBranchName, bIncludeCDOs);
		if (!ensure(OutputSettingNode))
		{
			continue;
		}

		FString RenderLayerName = RenderData.Key.RootBranchName.ToString();
		UMovieGraphRenderLayerNode* RenderLayerNode = InRawFrameData->EvaluatedConfig->GetSettingForBranch<UMovieGraphRenderLayerNode>(RenderData.Key.RootBranchName, bIncludeCDOs);
		if (RenderLayerNode)
		{
			RenderLayerName = RenderLayerNode->GetRenderLayerName();
		}
		// ToDo: Certain images may require transparency, at which point
		// we write out a .png instead of a .jpeg.
		EImageFormat PreferredOutputFormat = OutputFormat;

		const TCHAR* Extension = TEXT("");
		switch (PreferredOutputFormat)
		{
		case EImageFormat::PNG: Extension = TEXT("png"); break;
		case EImageFormat::JPEG: Extension = TEXT("jpeg"); break;
		case EImageFormat::BMP: Extension = TEXT("bmp"); break;
		case EImageFormat::EXR: Extension = TEXT("exr"); break;
		}

		// Generate one string that puts the directory combined with the filename format.
		FString FileNameFormatString = OutputSettingNode->OutputDirectory.Path / OutputSettingNode->FileNameFormat;

		// ToDo: Validate the string, ie: ensure it has {render_pass} in there somewhere there are multiple render passes
		// in the output data, include {camera_name} if there are multiple cameras for that render pass, etc.
		FileNameFormatString += TEXT(".{ext}");

		// Map the .ext to be specific to our output data.
		TMap<FString, FString> AdditionalFormatArgs;
		AdditionalFormatArgs.Add(TEXT("ext"), Extension);



		const FString ResolvedProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FString FileName = FString::Printf(TEXT("%s/Saved/MovieRenders/%s_%d.jpg"), *ResolvedProjectDir, *RenderLayerName, Payload->TraversalContext.Time.OutputFrameNumber);

		TUniquePtr<FImageWriteTask> TileImageTask = MakeUnique<FImageWriteTask>();
		TileImageTask->Format = OutputFormat;
		TileImageTask->CompressionQuality = 100;
		TileImageTask->Filename = FileName;
		TileImageTask->PixelData = RenderData.Value->CopyImageData();

		UE::MovieGraph::FMovieGraphOutputFutureData OutputData;
		OutputData.Shot = nullptr;
		OutputData.FilePath = FileName;
		OutputData.DataIdentifier = RenderData.Key;

		TFuture<bool> Future = ImageWriteQueue->Enqueue(MoveTemp(TileImageTask));

		InPipeline->AddOutputFuture(MoveTemp(Future), OutputData);
	}

}