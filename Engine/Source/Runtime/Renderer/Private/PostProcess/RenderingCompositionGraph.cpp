// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RenderingCompositionGraph.cpp: Scene pass order and dependency system.
=============================================================================*/

#include "PostProcess/RenderingCompositionGraph.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Async/Async.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "RenderTargetPool.h"
#include "RendererModule.h"
#include "HighResScreenshot.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "PostProcess/SceneRenderTargets.h"
#include "SceneRendering.h"
#include "VisualizeTexture.h"
#include "ScreenPass.h"

#include "IImageWrapper.h"
#include "ImageWriteQueue.h"
#include "ImageWriteStream.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_TYPE_LAYOUT(FPostProcessPassParameters);

void ExecuteCompositionGraphDebug();

static TAutoConsoleVariable<int32> CVarCompositionGraphOrder(
	TEXT("r.CompositionGraphOrder"),
	1,
	TEXT("Defines in which order the nodes in the CompositionGraph are executed (affects postprocess and some lighting).\n")
	TEXT("Option 1 provides more control, which can be useful for preserving ESRAM, avoid GPU sync, cluster up compute shaders for performance and control AsyncCompute.\n")
	TEXT(" 0: tree order starting with the root, first all inputs then dependencies (classic UE4, unconnected nodes are not getting executed)\n")
	TEXT(" 1: RegisterPass() call order, unless the dependencies (input and additional) require a different order (might become new default as it provides more control, executes all registered nodes)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarCompositionForceRenderTargetLoad(
	TEXT("r.CompositionForceRenderTargetLoad"),
	0,
	TEXT("0: default engine behaviour\n")
	TEXT("1: force ERenderTargetLoadAction::ELoad for all render targets"),
	ECVF_RenderThreadSafe);

#if !UE_BUILD_SHIPPING
FAutoConsoleCommand CmdCompositionGraphDebug(
	TEXT("r.CompositionGraphDebug"),
	TEXT("Execute this command to get a single frame dump of the composition graph of one frame (post processing and lighting)."),
	FConsoleCommandDelegate::CreateStatic(ExecuteCompositionGraphDebug)
	);
#endif


// render thread, 0:off, >0 next n frames should be debugged
uint32 GDebugCompositionGraphFrames = 0;

class FGMLFileWriter
{
public:
	// constructor
	FGMLFileWriter()
		: GMLFile(0)
	{		
	}

	void OpenGMLFile(const TCHAR* Name)
	{
#if !UE_BUILD_SHIPPING
		// todo: do we need to create the directory?
		FString FilePath = FPaths::ScreenShotDir() + TEXT("/") + Name + TEXT(".gml");
		GMLFile = IFileManager::Get().CreateDebugFileWriter(*FilePath);
#endif
	}

	void CloseGMLFile()
	{
#if !UE_BUILD_SHIPPING
		if(GMLFile)
		{
			delete GMLFile;
			GMLFile = 0;
		}
#endif
	}

	// .GML file is to visualize the post processing graph as a 2d graph
	void WriteLine(const char* Line)
	{
#if !UE_BUILD_SHIPPING
		if(GMLFile)
		{
			GMLFile->Serialize((void*)Line, FCStringAnsi::Strlen(Line));
			GMLFile->Serialize((void*)"\r\n", 2);
		}
#endif
	}

private:
	FArchive* GMLFile;
};

FGMLFileWriter GGMLFileWriter;


bool ShouldDebugCompositionGraph()
{
#if !UE_BUILD_SHIPPING
	return GDebugCompositionGraphFrames > 0;
#else 
	return false;
#endif
}

void Test()
{
	struct ObjectSize4
	{
		void SetBaseValues(){}
		static FName GetFName()
		{
			static const FName Name(TEXT("ObjectSize4"));
			return Name;
		}
		uint8 Data[4];
	};
 
	MS_ALIGN(16) struct ObjectAligned16
	{
		void SetBaseValues(){}
		static FName GetFName()
		{
			static const FName Name(TEXT("ObjectAligned16"));
			return Name;
		}
		uint8 Data[16];
	} GCC_ALIGN(16);

	// https://udn.unrealengine.com/questions/274066/fblendablemanager-returning-wrong-or-misaligned-da.html
	FBlendableManager Manager;
	Manager.GetSingleFinalData<ObjectSize4>();
	ObjectAligned16& AlignedData = Manager.GetSingleFinalData<ObjectAligned16>();

	check((reinterpret_cast<ptrdiff_t>(&AlignedData) & 16) == 0);
}

void ExecuteCompositionGraphDebug()
{
	ENQUEUE_RENDER_COMMAND(StartDebugCompositionGraph)(
		[](FRHICommandList& RHICmdList)
		{
			GDebugCompositionGraphFrames = 1;
			Test();
		});
}

// main thread
void CompositionGraph_OnStartFrame()
{
#if !UE_BUILD_SHIPPING
	ENQUEUE_RENDER_COMMAND(DebugCompositionGraphDec)(
		[](FRHICommandList& RHICmdList)
		{
			if(GDebugCompositionGraphFrames)
			{
				--GDebugCompositionGraphFrames;
			}
		});		
#endif
}

const TRefCountPtr<IPooledRenderTarget>& GetFallbackTarget(EFallbackColor FallbackColor)
{
	switch (FallbackColor)
	{
	case eFC_0000: return GSystemTextures.BlackDummy;
	case eFC_0001: return GSystemTextures.BlackAlphaOneDummy;
	case eFC_1111: return GSystemTextures.WhiteDummy;
	default:
		ensure(!"Unhandled enum in EFallbackColor");
		static const TRefCountPtr<IPooledRenderTarget> NullTarget;
		return NullTarget;
	}
}

const FTextureRHIRef& GetFallbackTexture(EFallbackColor FallbackColor)
{
	const TRefCountPtr<IPooledRenderTarget>& Target = GetFallbackTarget(FallbackColor);
	if (Target)
	{
		return Target->GetRenderTargetItem().ShaderResourceTexture;
	}
	else
	{
		static const FTextureRHIRef NullTexture;
		return NullTexture;
	}
}

FRenderingCompositePassContext::FRenderingCompositePassContext(FRHICommandListImmediate& InRHICmdList, const FViewInfo& InView)
	: View(InView)
	, SceneColorViewRect(InView.ViewRect)
	, ViewState((FSceneViewState*)InView.State)
	, Pass(0)
	, RHICmdList(InRHICmdList)
	, ViewPortRect(0, 0, 0 ,0)
	, FeatureLevel(View.GetFeatureLevel())
	, ShaderMap(InView.ShaderMap)
	, bWasProcessed(false)
	, bHasHmdMesh(false)
{
	check(!IsViewportValid());

	ReferenceBufferSize = FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY();
}

FRenderingCompositePassContext::~FRenderingCompositePassContext()
{
	Graph.Free();
}


bool FRenderingCompositePassContext::IsViewFamilyRenderTarget(const FSceneRenderTargetItem& DestRenderTarget) const
{
	check(DestRenderTarget.ShaderResourceTexture);
	return DestRenderTarget.ShaderResourceTexture == View.Family->RenderTarget->GetRenderTargetTexture();
}


void FRenderingCompositePassContext::Process(const TArray<FRenderingCompositePass*>& TargetedRoots, const TCHAR *GraphDebugName)
{
	// call this method only once after the graph is finished
	check(!bWasProcessed);

	bWasProcessed = true;
	bHasHmdMesh = IsHMDHiddenAreaMaskActive();

	if (TargetedRoots.Num() == 0)
	{
		return;
	}

	if(ShouldDebugCompositionGraph())
	{
		UE_LOG(LogConsoleResponse,Log, TEXT(""));
		UE_LOG(LogConsoleResponse,Log, TEXT("FRenderingCompositePassContext:Debug '%s' ---------"), GraphDebugName);
		UE_LOG(LogConsoleResponse,Log, TEXT(""));

		GGMLFileWriter.OpenGMLFile(GraphDebugName);
		GGMLFileWriter.WriteLine("Creator \"UnrealEngine4\"");
		GGMLFileWriter.WriteLine("Version \"2.10\"");
		GGMLFileWriter.WriteLine("graph");
		GGMLFileWriter.WriteLine("[");
		GGMLFileWriter.WriteLine("\tcomment\t\"This file can be viewed with yEd from yWorks. Run Layout/Hierarchical after loading.\"");
		GGMLFileWriter.WriteLine("\thierarchic\t1");
		GGMLFileWriter.WriteLine("\tdirected\t1");
	}

	bool bNewOrder = CVarCompositionGraphOrder.GetValueOnRenderThread() != 0;

	for (FRenderingCompositePass* Root : TargetedRoots)
	{
		Graph.RecursivelyGatherDependencies(Root);
	}

	SceneTexturesUniformBuffer = CreateSceneTextureUniformBufferDependentOnShadingPath(RHICmdList, FeatureLevel);

	if(bNewOrder)
	{
		// process in the order the nodes have been created (for more control), unless the dependencies require it differently
		for (FRenderingCompositePass* Node : Graph.Nodes)
		{
			// only if this is true the node is actually needed - no need to compute it when it's not needed
			if(Node->WasComputeOutputDescCalled())
			{
				Graph.RecursivelyProcess(Node, *this);
			}
		}
	}
	else
	{
		// process in the order of the dependencies, starting from the root (without processing unreferenced nodes)
		for (FRenderingCompositePass* Root : TargetedRoots)
		{
			Graph.RecursivelyProcess(Root, *this);
		}
	}

	if(ShouldDebugCompositionGraph())
	{
		UE_LOG(LogConsoleResponse,Log, TEXT(""));

		GGMLFileWriter.WriteLine("]");
		GGMLFileWriter.CloseGMLFile();
	}
}

ERenderTargetLoadAction FRenderingCompositePassContext::GetLoadActionForRenderTarget(const FSceneRenderTargetItem& DestRenderTarget) const
{
	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::ENoAction;

	if (IsViewFamilyRenderTarget(DestRenderTarget))
	{
		const bool bForceLoad = !!CVarCompositionForceRenderTargetLoad.GetValueOnAnyThread();
		if (bForceLoad)
		{
			LoadAction = ERenderTargetLoadAction::ELoad;
		}
		else
		{
			// If rendering the final view family's render target, must clear first view, and load subsequent views.
			LoadAction = (&View != View.Family->Views[0]) ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::EClear;
		}
	}
	else if (HasHmdMesh())
	{
		// Clears render target because going to have unrendered pixels inside view rect.
		LoadAction = ERenderTargetLoadAction::EClear;
	}

	return LoadAction;
}

FIntRect FRenderingCompositePassContext::GetSceneColorDestRect(FRenderingCompositePass* InPass) const
{
	if (const FRenderingCompositeOutput* Output = InPass->GetOutput(ePId_Output0))
	{
		if (const IPooledRenderTarget* Target = Output->PooledRenderTarget)
		{
			return GetSceneColorDestRect(Target->GetRenderTargetItem());
		}
	}
	return SceneColorViewRect;
}

// --------------------------------------------------------------------------

FRenderingCompositionGraph::FRenderingCompositionGraph()
{
}

FRenderingCompositionGraph::~FRenderingCompositionGraph()
{
	Free();
}

void FRenderingCompositionGraph::Free()
{
	for(uint32 i = 0; i < (uint32)Nodes.Num(); ++i)
	{
		FRenderingCompositePass *Element = Nodes[i];
		if (FMemStack::Get().ContainsPointer(Element))
		{
			Element->~FRenderingCompositePass();
		}
		else
		{
			// Call release on non-stack allocated elements
			Element->Release();
		}
	}

	Nodes.Empty();
}

void FRenderingCompositionGraph::RecursivelyGatherDependencies(FRenderingCompositePass *Pass)
{
	checkSlow(Pass);

	if(Pass->bComputeOutputDescWasCalled)
	{
		// already processed
		return;
	}
	Pass->bComputeOutputDescWasCalled = true;

	// iterate through all inputs and additional dependencies of this pass
	uint32 Index = 0;
	while(const FRenderingCompositeOutputRef* OutputRefIt = Pass->GetDependency(Index++))
	{
		FRenderingCompositeOutput* InputOutput = OutputRefIt->GetOutput();
				
		if(InputOutput)
		{
			// add a dependency to this output as we are referencing to it
			InputOutput->AddDependency();
		}
		
		if(FRenderingCompositePass* OutputRefItPass = OutputRefIt->GetPass())
		{
			// recursively process all inputs of this Pass
			RecursivelyGatherDependencies(OutputRefItPass);
		}
	}
}

TUniquePtr<FImagePixelData> FRenderingCompositionGraph::GetDumpOutput(FRenderingCompositePassContext& Context, FIntRect SourceRect, FRenderingCompositeOutput* Output) const
{
	FSceneRenderTargetItem& RenderTargetItem = Output->PooledRenderTarget->GetRenderTargetItem();
	FTextureRHIRef Texture = RenderTargetItem.TargetableTexture ? RenderTargetItem.TargetableTexture : RenderTargetItem.ShaderResourceTexture;
	check(Texture);
	check(Texture->GetTexture2D());

	int32 MSAAXSamples = Texture->GetNumSamples();
	SourceRect.Min.X *= MSAAXSamples;
	SourceRect.Max.X *= MSAAXSamples;

	switch (Texture->GetFormat())
	{
		case PF_FloatRGBA:
		{
			TArray<FFloat16Color> RawPixels;
			RawPixels.SetNum(SourceRect.Width() * SourceRect.Height());
			Context.RHICmdList.ReadSurfaceFloatData(Texture, SourceRect, RawPixels, (ECubeFace)0, 0, 0);
			TUniquePtr<TImagePixelData<FFloat16Color>> PixelData = MakeUnique<TImagePixelData<FFloat16Color>>(SourceRect.Size(), TArray64<FFloat16Color>(MoveTemp(RawPixels)));

			check(PixelData->IsDataWellFormed());

			return PixelData;
		}

		case PF_A32B32G32R32F:
		{
			FReadSurfaceDataFlags ReadDataFlags(RCM_MinMax);
			ReadDataFlags.SetLinearToGamma(false);

			TArray<FLinearColor> RawPixels;
			RawPixels.SetNum(SourceRect.Width() * SourceRect.Height());
			Context.RHICmdList.ReadSurfaceData(Texture, SourceRect, RawPixels, ReadDataFlags);
			TUniquePtr<TImagePixelData<FLinearColor>> PixelData = MakeUnique<TImagePixelData<FLinearColor>>(SourceRect.Size(), TArray64<FLinearColor>(MoveTemp(RawPixels)));

			check(PixelData->IsDataWellFormed());

			return PixelData;
		}

		case PF_R8G8B8A8:
		case PF_B8G8R8A8:
		{
			FReadSurfaceDataFlags ReadDataFlags;
			ReadDataFlags.SetLinearToGamma(false);

			TArray<FColor> RawPixels;
			RawPixels.SetNum(SourceRect.Width() * SourceRect.Height());
			Context.RHICmdList.ReadSurfaceData(Texture, SourceRect, RawPixels, ReadDataFlags);
			TUniquePtr<TImagePixelData<FColor>> PixelData = MakeUnique<TImagePixelData<FColor>>(SourceRect.Size(), TArray64<FColor>(MoveTemp(RawPixels)));

			check(PixelData->IsDataWellFormed());

			return PixelData;
		}
	}

	return nullptr;
}

void FRenderingCompositionGraph::DumpOutputToPipe(FRenderingCompositePassContext& Context, FImagePixelPipe* OutputPipe, FRenderingCompositeOutput* Output) const
{
	TUniquePtr<FImagePixelData> PixelData = GetDumpOutput(Context, Context.View.ViewRect, Output);
	if (PixelData.IsValid())
	{
		OutputPipe->Push(MoveTemp(PixelData));
	}
}

TFuture<bool> FRenderingCompositionGraph::DumpOutputToFile(FRenderingCompositePassContext& Context, const FString& Filename, FRenderingCompositeOutput* Output) const
{
	FHighResScreenshotConfig& HighResScreenshotConfig = GetHighResScreenshotConfig();

	if (!ensureMsgf(HighResScreenshotConfig.ImageWriteQueue, TEXT("Unable to write images unless FHighResScreenshotConfig::Init has been called.")))
	{
		return TFuture<bool>();
	}

	FIntRect SourceRect = Context.View.ViewRect;
	if (GIsHighResScreenshot && HighResScreenshotConfig.CaptureRegion.Area())
	{
		SourceRect = HighResScreenshotConfig.CaptureRegion;
	}

	TUniquePtr<FImagePixelData> PixelData = GetDumpOutput(Context, SourceRect, Output);
	if (!PixelData.IsValid())
	{
		return TFuture<bool>();
	}

	TUniquePtr<FImageWriteTask> ImageTask = MakeUnique<FImageWriteTask>();
	ImageTask->PixelData = MoveTemp(PixelData);

	HighResScreenshotConfig.PopulateImageTaskParams(*ImageTask);
	ImageTask->Filename = Filename;

	if (ImageTask->PixelData->GetType() == EImagePixelType::Color)
	{
		// Always write full alpha
		ImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FColor>(255));

		if (ImageTask->Format == EImageFormat::EXR)
		{
			// Write FColors with a gamma curve. This replicates behaviour that previously existed in ExrImageWrapper.cpp (see following overloads) that assumed
			// any 8 bit output format needed linearizing, but this is not a safe assumption to make at such a low level:
			// void ExtractAndConvertChannel(const uint8*Src, uint32 SrcChannels, uint32 x, uint32 y, float* ChannelOUT)
			// void ExtractAndConvertChannel(const uint8*Src, uint32 SrcChannels, uint32 x, uint32 y, FFloat16* ChannelOUT)
			ImageTask->PixelPreProcessors.Add(TAsyncGammaCorrect<FColor>(2.2f));
		}
	}

	return HighResScreenshotConfig.ImageWriteQueue->Enqueue(MoveTemp(ImageTask));
}

void FRenderingCompositionGraph::RecursivelyProcess(const FRenderingCompositeOutputRef& InOutputRef, FRenderingCompositePassContext& Context) const
{
	FRenderingCompositePass *Pass = InOutputRef.GetPass();
	FRenderingCompositeOutput *Output = InOutputRef.GetOutput();

#if !UE_BUILD_SHIPPING
	if(!Pass || !Output)
	{
		// to track down a crash bug
		if(Context.Pass)
		{
			UE_LOG(LogRenderer,Fatal, TEXT("FRenderingCompositionGraph::RecursivelyProcess %s"), *Context.Pass->ConstructDebugName());
		}
	}
#endif

	check(Pass);
	check(Output);

	if(Pass->bProcessWasCalled)
	{
		// already processed
		return;
	}
	Pass->bProcessWasCalled = true;

	// iterate through all inputs and additional dependencies of this pass
	{
		uint32 Index = 0;

		while(const FRenderingCompositeOutputRef* OutputRefIt = Pass->GetDependency(Index++))
		{
			if(OutputRefIt->GetPass())
			{
				if(!OutputRefIt)
				{
					// Pass doesn't have more inputs
					break;
				}

				FRenderingCompositeOutput* Input = OutputRefIt->GetOutput();
				
				// to track down an issue, should never happen
				check(OutputRefIt->GetPass());

				if(GRenderTargetPool.IsEventRecordingEnabled())
				{
					GRenderTargetPool.AddPhaseEvent(*Pass->ConstructDebugName());
				}

				Context.Pass = Pass;
				RecursivelyProcess(*OutputRefIt, Context);
			}
		}
	}
	
	// Requests the output render target descriptors.
	for(uint32 OutputId = 0; ; ++OutputId)
	{
		EPassOutputId PassOutputId = (EPassOutputId)(OutputId);
		FRenderingCompositeOutput* PassOutput = Pass->GetOutput(PassOutputId);

		if(!PassOutput)
		{
			break;
		}

		PassOutput->RenderTargetDesc = Pass->ComputeOutputDesc(PassOutputId);

		// Allow format overrides for high-precision work
		static const auto CVarPostProcessingColorFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.PostProcessingColorFormat"));

		if (CVarPostProcessingColorFormat->GetValueOnRenderThread() == 1)
		{
			if (PassOutput->RenderTargetDesc.Format == PF_FloatRGBA ||
				PassOutput->RenderTargetDesc.Format == PF_FloatRGB ||
				PassOutput->RenderTargetDesc.Format == PF_FloatR11G11B10)
			{
				PassOutput->RenderTargetDesc.Format = PF_A32B32G32R32F;
			}
		}
	}

	// Execute the pass straight away to have any update on the output decsriptors ExtractRDGTextureForOutput() might do.
	{
		Context.Pass = Pass;
		Context.SetViewportInvalid();

		if (Pass->bBindGlobalUniformBuffers)
		{
			FUniformBufferStaticBindings GlobalUniformBuffers;
			GlobalUniformBuffers.AddUniformBuffer(Context.SceneTexturesUniformBuffer);
			Context.RHICmdList.SetGlobalUniformBuffers(GlobalUniformBuffers);
		}

		// then process the pass itself
		check(!Context.RHICmdList.IsInsideRenderPass());
		Pass->Process(Context);
		check(!Context.RHICmdList.IsInsideRenderPass());

		if (Pass->bBindGlobalUniformBuffers)
		{
			Context.RHICmdList.SetGlobalUniformBuffers({});
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if(ShouldDebugCompositionGraph())
	{
		GGMLFileWriter.WriteLine("\tnode");
		GGMLFileWriter.WriteLine("\t[");

		int32 PassId = ComputeUniquePassId(Pass);
		FString PassDebugName = Pass->ConstructDebugName();

		ANSICHAR Line[MAX_SPRINTF];

		{
			GGMLFileWriter.WriteLine("\t\tgraphics");
			GGMLFileWriter.WriteLine("\t\t[");
			FCStringAnsi::Sprintf(Line, "\t\t\tw\t%d", 200);
			GGMLFileWriter.WriteLine(Line);
			FCStringAnsi::Sprintf(Line, "\t\t\th\t%d", 80);
			GGMLFileWriter.WriteLine(Line);
			GGMLFileWriter.WriteLine("\t\t\tfill\t\"#FFCCCC\"");
			GGMLFileWriter.WriteLine("\t\t]");
		}

		{
			FCStringAnsi::Sprintf(Line, "\t\tid\t%d", PassId);
			GGMLFileWriter.WriteLine(Line);
			GGMLFileWriter.WriteLine("\t\tLabelGraphics");
			GGMLFileWriter.WriteLine("\t\t[");
			FCStringAnsi::Sprintf(Line, "\t\t\ttext\t\"#%d\r%s\"", PassId, (const char *)TCHAR_TO_ANSI(*PassDebugName));
			GGMLFileWriter.WriteLine(Line);
			GGMLFileWriter.WriteLine("\t\t\tanchor\t\"t\"");	// put label internally on top
			GGMLFileWriter.WriteLine("\t\t\tfontSize\t14");
			GGMLFileWriter.WriteLine("\t\t\tfontStyle\t\"bold\"");
			GGMLFileWriter.WriteLine("\t\t]");
		}

		UE_LOG(LogConsoleResponse,Log, TEXT("Node#%d '%s'"), PassId, *PassDebugName);
	
		GGMLFileWriter.WriteLine("\t\tisGroup\t1");
		GGMLFileWriter.WriteLine("\t]");

		uint32 InputId = 0;
		while(FRenderingCompositeOutputRef* OutputRefIt = Pass->GetInput((EPassInputId)(InputId++)))
		{
			if(OutputRefIt->Source)
			{
				// source is hooked up 
				FString InputName = OutputRefIt->Source->ConstructDebugName();

				int32 TargetPassId = ComputeUniquePassId(OutputRefIt->Source);

				UE_LOG(LogConsoleResponse,Log, TEXT("  ePId_Input%d: Node#%d @ ePId_Output%d '%s'"), InputId - 1, TargetPassId, (uint32)OutputRefIt->PassOutputId, *InputName);

				// input connection to another node
				{
					GGMLFileWriter.WriteLine("\tedge");
					GGMLFileWriter.WriteLine("\t[");
					{
						FCStringAnsi::Sprintf(Line, "\t\tsource\t%d", ComputeUniqueOutputId(OutputRefIt->Source, OutputRefIt->PassOutputId));
						GGMLFileWriter.WriteLine(Line);
						FCStringAnsi::Sprintf(Line, "\t\ttarget\t%d", PassId);
						GGMLFileWriter.WriteLine(Line);
					}
					{
						FString EdgeName = FString::Printf(TEXT("ePId_Input%d"), InputId - 1);

						GGMLFileWriter.WriteLine("\t\tLabelGraphics");
						GGMLFileWriter.WriteLine("\t\t[");
						FCStringAnsi::Sprintf(Line, "\t\t\ttext\t\"%s\"", (const char *)TCHAR_TO_ANSI(*EdgeName));
						GGMLFileWriter.WriteLine(Line);
						GGMLFileWriter.WriteLine("\t\t\tmodel\t\"three_center\"");
						GGMLFileWriter.WriteLine("\t\t\tposition\t\"tcentr\"");
						GGMLFileWriter.WriteLine("\t\t]");
					}
					GGMLFileWriter.WriteLine("\t]");
				}
			}
			else
			{
				// source is not hooked up 
				UE_LOG(LogConsoleResponse,Log, TEXT("  ePId_Input%d:"), InputId - 1);
			}
		}

		uint32 DepId = 0;
		while(FRenderingCompositeOutputRef* OutputRefIt = Pass->GetAdditionalDependency(DepId++))
		{
			check(OutputRefIt->Source);

			FString InputName = OutputRefIt->Source->ConstructDebugName();
			int32 TargetPassId = ComputeUniquePassId(OutputRefIt->Source);

			UE_LOG(LogConsoleResponse,Log, TEXT("  Dependency: Node#%d @ ePId_Output%d '%s'"), TargetPassId, (uint32)OutputRefIt->PassOutputId, *InputName);

			// dependency connection to another node
			{
				GGMLFileWriter.WriteLine("\tedge");
				GGMLFileWriter.WriteLine("\t[");
				{
					FCStringAnsi::Sprintf(Line, "\t\tsource\t%d", ComputeUniqueOutputId(OutputRefIt->Source, OutputRefIt->PassOutputId));
					GGMLFileWriter.WriteLine(Line);
					FCStringAnsi::Sprintf(Line, "\t\ttarget\t%d", PassId);
					GGMLFileWriter.WriteLine(Line);
				}
				// dashed line
				{
					GGMLFileWriter.WriteLine("\t\tgraphics");
					GGMLFileWriter.WriteLine("\t\t[");
					GGMLFileWriter.WriteLine("\t\t\tstyle\t\"dashed\"");
					GGMLFileWriter.WriteLine("\t\t]");
				}
				{
					FString EdgeName = TEXT("Dependency");

					GGMLFileWriter.WriteLine("\t\tLabelGraphics");
					GGMLFileWriter.WriteLine("\t\t[");
					FCStringAnsi::Sprintf(Line, "\t\t\ttext\t\"%s\"", (const char *)TCHAR_TO_ANSI(*EdgeName));
					GGMLFileWriter.WriteLine(Line);
					GGMLFileWriter.WriteLine("\t\t\tmodel\t\"three_center\"");
					GGMLFileWriter.WriteLine("\t\t\tposition\t\"tcentr\"");
					GGMLFileWriter.WriteLine("\t\t]");
				}
				GGMLFileWriter.WriteLine("\t]");
			}
		}

		uint32 OutputId = 0;
		while(FRenderingCompositeOutput* PassOutput = Pass->GetOutput((EPassOutputId)(OutputId)))
		{
			UE_LOG(LogConsoleResponse,Log, TEXT("  ePId_Output%d %s %s Dep: %d"), OutputId, *PassOutput->RenderTargetDesc.GenerateInfoString(), PassOutput->RenderTargetDesc.DebugName, PassOutput->GetDependencyCount());

			GGMLFileWriter.WriteLine("\tnode");
			GGMLFileWriter.WriteLine("\t[");

			{
				GGMLFileWriter.WriteLine("\t\tgraphics");
				GGMLFileWriter.WriteLine("\t\t[");
				FCStringAnsi::Sprintf(Line, "\t\t\tw\t%d", 220);
				GGMLFileWriter.WriteLine(Line);
				FCStringAnsi::Sprintf(Line, "\t\t\th\t%d", 40);
				GGMLFileWriter.WriteLine(Line);
				GGMLFileWriter.WriteLine("\t\t]");
			}

			{
				FCStringAnsi::Sprintf(Line, "\t\tid\t%d", ComputeUniqueOutputId(Pass, (EPassOutputId)(OutputId)));
				GGMLFileWriter.WriteLine(Line);
				GGMLFileWriter.WriteLine("\t\tLabelGraphics");
				GGMLFileWriter.WriteLine("\t\t[");
				FCStringAnsi::Sprintf(Line, "\t\t\ttext\t\"ePId_Output%d '%s'\r%s\"", 
					OutputId, 
					(const char *)TCHAR_TO_ANSI(PassOutput->RenderTargetDesc.DebugName),
					(const char *)TCHAR_TO_ANSI(*PassOutput->RenderTargetDesc.GenerateInfoString()));
				GGMLFileWriter.WriteLine(Line);
				GGMLFileWriter.WriteLine("\t\t]");
			}


			{
				FCStringAnsi::Sprintf(Line, "\t\tgid\t%d", PassId);
				GGMLFileWriter.WriteLine(Line);
			}

			GGMLFileWriter.WriteLine("\t]");

			++OutputId;
		}

		UE_LOG(LogConsoleResponse,Log, TEXT(""));
	}
#endif

	// for VisualizeTexture and output buffer dumping
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	{
		uint32 OutputId = 0;

		while(FRenderingCompositeOutput* PassOutput = Pass->GetOutput((EPassOutputId)OutputId))
		{
			// use intermediate texture unless it's the last one where we render to the final output
			if(PassOutput->PooledRenderTarget)
			{
				GVisualizeTexture.SetCheckPoint(Context.RHICmdList, PassOutput->PooledRenderTarget);

				// If this buffer was given a pipe to push its output onto, do that now
				FImagePixelPipe* OutputPipe = Pass->GetOutputDumpPipe((EPassOutputId)OutputId);
				if (OutputPipe)
				{
					DumpOutputToPipe(Context, OutputPipe, PassOutput);
				}

				// If this buffer was given a dump filename, write it out
				const FString& Filename = Pass->GetOutputDumpFilename((EPassOutputId)OutputId);
				if (!Filename.IsEmpty())
				{
					DumpOutputToFile(Context, Filename, PassOutput);
				}

				// If we've been asked to write out the pixel data for this pass to an external array, do it now
				TArray<FColor>* OutputColorArray = Pass->GetOutputColorArray((EPassOutputId)OutputId);
				if (OutputColorArray)
				{
					Context.RHICmdList.ReadSurfaceData(
						PassOutput->PooledRenderTarget->GetRenderTargetItem().TargetableTexture,
						Context.View.ViewRect,
						*OutputColorArray,
						FReadSurfaceDataFlags()
						);
				}
			}

			OutputId++;
		}
	}
#endif

	// iterate through all inputs of this pass and decrement the references for it's inputs
	// this can release some intermediate RT so they can be reused
	{
		uint32 InputId = 0;

		while(const FRenderingCompositeOutputRef* OutputRefIt = Pass->GetDependency(InputId++))
		{
			FRenderingCompositeOutput* Input = OutputRefIt->GetOutput();

			if(Input)
			{
				Input->ResolveDependencies();
			}
		}
	}
}

// for debugging purpose O(n)
int32 FRenderingCompositionGraph::ComputeUniquePassId(FRenderingCompositePass* Pass) const
{
	for(uint32 i = 0; i < (uint32)Nodes.Num(); ++i)
	{
		FRenderingCompositePass *Element = Nodes[i];

		if(Element == Pass)
		{
			return i;
		}
	}

	return -1;
}

int32 FRenderingCompositionGraph::ComputeUniqueOutputId(FRenderingCompositePass* Pass, EPassOutputId OutputId) const
{
	uint32 Ret = Nodes.Num();

	for(uint32 i = 0; i < (uint32)Nodes.Num(); ++i)
	{
		FRenderingCompositePass *Element = Nodes[i];

		if(Element == Pass)
		{
			return (int32)(Ret + (uint32)OutputId);
		}

		uint32 OutputCount = 0;
		while(Pass->GetOutput((EPassOutputId)OutputCount))
		{
			++OutputCount;
		}

		Ret += OutputCount;
	}

	return -1;
}

FRenderingCompositeOutput *FRenderingCompositeOutputRef::GetOutput() const
{
	if(Source == 0)
	{
		return 0;
	}

	return Source->GetOutput(PassOutputId); 
}

// -----------------------------------------------------------------

void FPostProcessPassParameters::Bind(const FShaderParameterMap& ParameterMap)
{
	ViewportSize.Bind(ParameterMap,TEXT("ViewportSize"));
	ViewportRect.Bind(ParameterMap,TEXT("ViewportRect"));
	ScreenPosToPixel.Bind(ParameterMap, TEXT("ScreenPosToPixel"));
	SceneColorBufferUVViewport.Bind(ParameterMap, TEXT("SceneColorBufferUVViewport"));
	
	for(uint32 i = 0; i < ePId_Input_MAX; ++i)
	{
		PostprocessInputParameter[i].Bind(ParameterMap, *FString::Printf(TEXT("PostprocessInput%d"), i));
		PostprocessInputParameterSampler[i].Bind(ParameterMap, *FString::Printf(TEXT("PostprocessInput%dSampler"), i));
		PostprocessInputSizeParameter[i].Bind(ParameterMap, *FString::Printf(TEXT("PostprocessInput%dSize"), i));
		PostProcessInputMinMaxParameter[i].Bind(ParameterMap, *FString::Printf(TEXT("PostprocessInput%dMinMax"), i));
	}
}

template <typename TRHICmdList>
void FPostProcessPassParameters::SetPS(TRHICmdList& RHICmdList, FRHIPixelShader* ShaderRHI, const FRenderingCompositePassContext& Context, FRHISamplerState* Filter, EFallbackColor FallbackColor, FRHISamplerState** FilterOverrideArray)
{
	Set(RHICmdList, ShaderRHI, Context, Filter, FallbackColor, FilterOverrideArray);
}

template RENDERER_API void FPostProcessPassParameters::SetPS(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, const FRenderingCompositePassContext& Context, FRHISamplerState* Filter, EFallbackColor FallbackColor, FRHISamplerState** FilterOverrideArray);
template RENDERER_API void FPostProcessPassParameters::SetPS(FRHICommandListImmediate& RHICmdList, FRHIPixelShader* ShaderRHI, const FRenderingCompositePassContext& Context, FRHISamplerState* Filter, EFallbackColor FallbackColor, FRHISamplerState** FilterOverrideArray);

template< typename TRHICmdList >
void FPostProcessPassParameters::SetCS(FRHIComputeShader* ShaderRHI, const FRenderingCompositePassContext& Context, TRHICmdList& RHICmdList, FRHISamplerState* Filter, EFallbackColor FallbackColor, FRHISamplerState** FilterOverrideArray)
{
	Set(RHICmdList, ShaderRHI, Context, Filter, FallbackColor, FilterOverrideArray);
}
template RENDERER_API void FPostProcessPassParameters::SetCS< FRHICommandListImmediate >(
	FRHIComputeShader* ShaderRHI,
	const FRenderingCompositePassContext& Context,
	FRHICommandListImmediate& RHICmdList,
	FRHISamplerState* Filter,
	EFallbackColor FallbackColor,
	FRHISamplerState** FilterOverrideArray
	);

template RENDERER_API void FPostProcessPassParameters::SetCS< FRHIAsyncComputeCommandListImmediate >(
	FRHIComputeShader* ShaderRHI,
	const FRenderingCompositePassContext& Context,
	FRHIAsyncComputeCommandListImmediate& RHICmdList,
	FRHISamplerState* Filter,
	EFallbackColor FallbackColor,
	FRHISamplerState** FilterOverrideArray
	);

void FPostProcessPassParameters::SetVS(FRHIVertexShader* ShaderRHI, const FRenderingCompositePassContext& Context, FRHISamplerState* Filter, EFallbackColor FallbackColor, FRHISamplerState** FilterOverrideArray)
{
	Set(Context.RHICmdList, ShaderRHI, Context, Filter, FallbackColor, FilterOverrideArray);
}

template< typename TRHIShader, typename TRHICmdList >
void FPostProcessPassParameters::Set(
	TRHICmdList& RHICmdList,
	TRHIShader* ShaderRHI,
	const FRenderingCompositePassContext& Context,
	FRHISamplerState* Filter,
	EFallbackColor FallbackColor,
	FRHISamplerState** FilterOverrideArray)
{
	// assuming all outputs have the same size
	FRenderingCompositeOutput* Output = Context.Pass->GetOutput(ePId_Output0);

	// Output0 should always exist
	check(Output);

	// one should be on
	check(FilterOverrideArray || Filter);
	// but not both
	check(!FilterOverrideArray || !Filter);

	if(ViewportSize.IsBound() || ScreenPosToPixel.IsBound() || ViewportRect.IsBound())
	{
		FIntRect LocalViewport = Context.GetViewport();

		FIntPoint ViewportOffset = LocalViewport.Min;
		FIntPoint ViewportExtent = LocalViewport.Size();

		{
			FVector4 Value(ViewportExtent.X, ViewportExtent.Y, 1.0f / ViewportExtent.X, 1.0f / ViewportExtent.Y);

			SetShaderValue(RHICmdList, ShaderRHI, ViewportSize, Value);
		}

		{
			SetShaderValue(RHICmdList, ShaderRHI, ViewportRect, Context.GetViewport());
		}

		{
			FVector4 ScreenPosToPixelValue(
				ViewportExtent.X * 0.5f,
				-ViewportExtent.Y * 0.5f, 
				ViewportExtent.X * 0.5f - 0.5f + ViewportOffset.X,
				ViewportExtent.Y * 0.5f - 0.5f + ViewportOffset.Y);
			SetShaderValue(RHICmdList, ShaderRHI, ScreenPosToPixel, ScreenPosToPixelValue);
		}
	}

	//Calculate a base scene texture min max which will be pulled in by a pixel for each PP input.
	FIntRect ContextViewportRect = Context.IsViewportValid() ? Context.SceneColorViewRect : FIntRect(0,0,0,0);
	const FIntPoint SceneRTSize = Context.ReferenceBufferSize;
	FVector4 BaseSceneTexMinMax(	((float)ContextViewportRect.Min.X/SceneRTSize.X), 
									((float)ContextViewportRect.Min.Y/SceneRTSize.Y), 
									((float)ContextViewportRect.Max.X/SceneRTSize.X), 
									((float)ContextViewportRect.Max.Y/SceneRTSize.Y) );

	if (SceneColorBufferUVViewport.IsBound())
	{
		FVector4 SceneColorBufferUVViewportValue(
			ContextViewportRect.Width() / float(SceneRTSize.X), ContextViewportRect.Height() / float(SceneRTSize.Y),
			BaseSceneTexMinMax.X, BaseSceneTexMinMax.Y);
		SetShaderValue(RHICmdList, ShaderRHI, SceneColorBufferUVViewport, SceneColorBufferUVViewportValue);
	}

	const FTextureRHIRef& FallbackTexture = GetFallbackTexture(FallbackColor);

	// ePId_Input0, ePId_Input1, ...
	for(uint32 Id = 0; Id < (uint32)ePId_Input_MAX; ++Id)
	{
		FRenderingCompositeOutputRef* OutputRef = Context.Pass->GetInput((EPassInputId)Id);

		if(!OutputRef)
		{
			// Pass doesn't have more inputs
			break;
		}

		const auto FeatureLevel = Context.GetFeatureLevel();

		FRenderingCompositeOutput* Input = OutputRef->GetOutput();

		TRefCountPtr<IPooledRenderTarget> InputPooledElement;

		if(Input)
		{
			InputPooledElement = Input->RequestInput();
		}

		FRHISamplerState* LocalFilter = FilterOverrideArray ? FilterOverrideArray[Id] : Filter;

		if(InputPooledElement)
		{
			check(!InputPooledElement->IsFree());

			const FTextureRHIRef& SrcTexture = InputPooledElement->GetRenderTargetItem().ShaderResourceTexture;

			SetTextureParameter(RHICmdList, ShaderRHI, PostprocessInputParameter[Id], PostprocessInputParameterSampler[Id], LocalFilter, SrcTexture);

			if(PostprocessInputSizeParameter[Id].IsBound() || PostProcessInputMinMaxParameter[Id].IsBound())
			{
				float Width = InputPooledElement->GetDesc().Extent.X;
				float Height = InputPooledElement->GetDesc().Extent.Y;
				
				FVector2D OnePPInputPixelUVSize = FVector2D(1.0f / Width, 1.0f / Height);

				FVector4 TextureSize(Width, Height, OnePPInputPixelUVSize.X, OnePPInputPixelUVSize.Y);
				SetShaderValue(RHICmdList, ShaderRHI, PostprocessInputSizeParameter[Id], TextureSize);

				//We could use the main scene min max here if it weren't that we need to pull the max in by a pixel on a per input basis.
				FVector4 PPInputMinMax = BaseSceneTexMinMax;
				PPInputMinMax.X += 0.5f * OnePPInputPixelUVSize.X;
				PPInputMinMax.Y += 0.5f * OnePPInputPixelUVSize.Y;
				PPInputMinMax.Z -= 0.5f * OnePPInputPixelUVSize.X;
				PPInputMinMax.W -= 0.5f * OnePPInputPixelUVSize.Y;
				SetShaderValue(RHICmdList, ShaderRHI, PostProcessInputMinMaxParameter[Id], PPInputMinMax);
			}
		}
		else
		{
			// if the input is not there but the shader request it we give it at least some data to avoid d3ddebug errors and shader permutations
			// to make features optional we use default black for additive passes without shader permutations
			SetTextureParameter(RHICmdList, ShaderRHI, PostprocessInputParameter[Id], PostprocessInputParameterSampler[Id], LocalFilter, FallbackTexture);

			FVector4 Dummy(1, 1, 1, 1);
			SetShaderValue(RHICmdList, ShaderRHI, PostprocessInputSizeParameter[Id], Dummy);
			SetShaderValue(RHICmdList, ShaderRHI, PostProcessInputMinMaxParameter[Id], Dummy);
		}
	}

	// todo warning if Input[] or InputSize[] is bound but not available, maybe set a specific input texture (blinking?)
}

#define IMPLEMENT_POST_PROCESS_PARAM_SET( TRHIShader, TRHICmdList ) \
	template RENDERER_API void FPostProcessPassParameters::Set< TRHIShader >( \
		TRHICmdList& RHICmdList,						\
		TRHIShader* ShaderRHI,			\
		const FRenderingCompositePassContext& Context,	\
		FRHISamplerState* Filter,				\
		EFallbackColor FallbackColor,					\
		FRHISamplerState** FilterOverrideArray	\
	);

IMPLEMENT_POST_PROCESS_PARAM_SET(FRHIVertexShader, FRHICommandListImmediate );
IMPLEMENT_POST_PROCESS_PARAM_SET(FRHIHullShader, FRHICommandListImmediate );
IMPLEMENT_POST_PROCESS_PARAM_SET(FRHIDomainShader, FRHICommandListImmediate );
IMPLEMENT_POST_PROCESS_PARAM_SET(FRHIGeometryShader, FRHICommandListImmediate );
IMPLEMENT_POST_PROCESS_PARAM_SET(FRHIPixelShader, FRHICommandListImmediate );
IMPLEMENT_POST_PROCESS_PARAM_SET(FRHIComputeShader, FRHICommandListImmediate );
IMPLEMENT_POST_PROCESS_PARAM_SET(FRHIComputeShader, FRHIAsyncComputeCommandListImmediate );

FArchive& operator<<(FArchive& Ar, FPostProcessPassParameters& P)
{
	Ar << P.ViewportSize << P.ScreenPosToPixel << P.SceneColorBufferUVViewport << P.ViewportRect;

	for(uint32 i = 0; i < ePId_Input_MAX; ++i)
	{
		Ar << P.PostprocessInputParameter[i];
		Ar << P.PostprocessInputParameterSampler[i];
		Ar << P.PostprocessInputSizeParameter[i];
		Ar << P.PostProcessInputMinMaxParameter[i];
	}

	return Ar;
}

// -----------------------------------------------------------------

const FSceneRenderTargetItem& FRenderingCompositeOutput::RequestSurface(const FRenderingCompositePassContext& Context)
{
	if(PooledRenderTarget)
	{
		Context.RHICmdList.Transition(FRHITransitionInfo(PooledRenderTarget->GetRenderTargetItem().TargetableTexture, ERHIAccess::Unknown, ERHIAccess::RTV));
		return PooledRenderTarget->GetRenderTargetItem();
	}

	if(!RenderTargetDesc.IsValid())
	{
		// useful to use the CompositingGraph dependency resolve but pass the data between nodes differently
		static FSceneRenderTargetItem Null;

		return Null;
	}

	if(!PooledRenderTarget)
	{
		GRenderTargetPool.FindFreeElement(Context.RHICmdList, RenderTargetDesc, PooledRenderTarget, RenderTargetDesc.DebugName);
	}

	check(!PooledRenderTarget->IsFree());

	FSceneRenderTargetItem& RenderTargetItem = PooledRenderTarget->GetRenderTargetItem();

	return RenderTargetItem;
}


const FPooledRenderTargetDesc* FRenderingCompositePass::GetInputDesc(EPassInputId InPassInputId) const
{
	// to overcome const issues, this way it's kept local
	FRenderingCompositePass* This = (FRenderingCompositePass*)this;

	const FRenderingCompositeOutputRef* OutputRef = This->GetInput(InPassInputId);

	if(!OutputRef)
	{
		return 0;
	}

	FRenderingCompositeOutput* Input = OutputRef->GetOutput();

	if(!Input)
	{
		return 0;
	}

	return &Input->RenderTargetDesc;
}

uint32 FRenderingCompositePass::ComputeInputCount()
{
	for(uint32 i = 0; ; ++i)
	{
		if(!GetInput((EPassInputId)i))
		{
			return i;
		}
	}
}

uint32 FRenderingCompositePass::ComputeOutputCount()
{
	for(uint32 i = 0; ; ++i)
	{
		if(!GetOutput((EPassOutputId)i))
		{
			return i;
		}
	}
}

FString FRenderingCompositePass::ConstructDebugName()
{
	FString Name;

	uint32 OutputId = 0;
	while(FRenderingCompositeOutput* Output = GetOutput((EPassOutputId)OutputId))
	{
		Name += Output->RenderTargetDesc.DebugName;

		++OutputId;
	}

	if(Name.IsEmpty())
	{
		Name = TEXT("UnknownName");
	}

	return Name;
}

FRDGTextureRef FRenderingCompositePass::CreateRDGTextureForOptionalInput(
	FRDGBuilder& GraphBuilder,
	EPassInputId InputId,
	const TCHAR* InputName)
{
	if (const FRenderingCompositeOutputRef* OutputRef = GetInput(InputId))
	{
		if (FRenderingCompositeOutput* Input = OutputRef->GetOutput())
		{
			return GraphBuilder.RegisterExternalTexture(Input->RequestInput(), InputName);
		}
	}
	return nullptr;
}

FRDGTextureRef FRenderingCompositePass::CreateRDGTextureForInputWithFallback(
	FRDGBuilder& GraphBuilder,
	EPassInputId InputId,
	const TCHAR* InputName,
	EFallbackColor FallbackColor)
{
	if (FRDGTextureRef RDGTexture = CreateRDGTextureForOptionalInput(GraphBuilder, InputId, InputName))
	{
		return RDGTexture;
	}
	return GraphBuilder.RegisterExternalTexture(GetFallbackTarget(FallbackColor));
}

void FRenderingCompositePass::ExtractRDGTextureForOutput(FRDGBuilder& GraphBuilder, EPassOutputId OutputId, FRDGTextureRef Texture)
{
	check(Texture);

	if (FRenderingCompositeOutput* Output = GetOutput(OutputId))
	{
		Output->RenderTargetDesc = Translate(Texture->Desc);
		GraphBuilder.QueueTextureExtraction(Texture, &Output->PooledRenderTarget);
	}
}

FRDGTextureRef FRenderingCompositePass::FindOrCreateRDGTextureForOutput(
	FRDGBuilder& GraphBuilder,
	EPassOutputId OutputId,
	const FRDGTextureDesc& TextureDesc,
	const TCHAR* TextureName)
{
	if (FRDGTextureRef OutputTexture = FindRDGTextureForOutput(GraphBuilder, OutputId, TextureName))
	{
		return OutputTexture;
	}
	return GraphBuilder.CreateTexture(TextureDesc, TextureName);
}

FRDGTextureRef FRenderingCompositePass::FindRDGTextureForOutput(
	FRDGBuilder& GraphBuilder,
	EPassOutputId OutputId,
	const TCHAR* TextureName)
{
	if (FRenderingCompositeOutput* Output = GetOutput(OutputId))
	{
		if (const TRefCountPtr<IPooledRenderTarget>& ExistingTarget = Output->PooledRenderTarget)
		{
			return GraphBuilder.RegisterExternalTexture(ExistingTarget, TextureName);
		}
	}
	return nullptr;
}