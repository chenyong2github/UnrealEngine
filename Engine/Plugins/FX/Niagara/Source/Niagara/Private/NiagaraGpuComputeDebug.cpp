// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraGpuComputeDebug.h"
#include "NiagaraDebugShaders.h"

#include "CanvasTypes.h"
#include "CommonRenderResources.h"
#include "RenderGraphBuilder.h"
#include "RHI.h"
#include "ScreenRendering.h"
#include "Engine/Font.h"
#include "Modules/ModuleManager.h"
#include "Runtime/Renderer/Private/ScreenPass.h"

int32 GNiagaraGpuComputeDebug_MinTextureHeight = 128;
static FAutoConsoleVariableRef CVarNiagaraGpuComputeDebug_MinTextureHeight(
	TEXT("fx.Niagara.GpuComputeDebug.MinTextureHeight"),
	GNiagaraGpuComputeDebug_MinTextureHeight,
	TEXT("The minimum height we will visualize a texture at, smaller textures will be scaled up to match this."),
	ECVF_Default
);

int32 GNiagaraGpuComputeDebug_MaxTextureHeight = 128;
static FAutoConsoleVariableRef CVarNiagaraGpuComputeDebug_MaxTextureHeight(
	TEXT("fx.Niagara.GpuComputeDebug.MaxTextureHeight"),
	GNiagaraGpuComputeDebug_MaxTextureHeight,
	TEXT("The maximum height we will visualize a texture at, this is to avoid things becoming too large on screen."),
	ECVF_Default
);

int32 GNiagaraGpuComputeDebug_MaxLineInstances = 4096;
static FAutoConsoleVariableRef CVarNiagaraGpuComputeDebug_MaxLineInstances(
	TEXT("fx.Niagara.GpuComputeDebug.MaxLineInstances"),
	GNiagaraGpuComputeDebug_MaxLineInstances,
	TEXT("Maximum number of line draw we support in a single frame."),
	ECVF_Default
);

int32 GNiagaraGpuComputeDebug_DrawDebugEnabled = 1;
static FAutoConsoleVariableRef CVarNiagaraGpuComputeDebug_DrawDebugEnabled(
	TEXT("fx.Niagara.GpuComputeDebug.DrawDebugEnabled"),
	GNiagaraGpuComputeDebug_DrawDebugEnabled,
	TEXT("Should we draw any of the debug information or not."),
	ECVF_Default
);

#if NIAGARA_COMPUTEDEBUG_ENABLED

//////////////////////////////////////////////////////////////////////////

FNiagaraGpuComputeDebug::FNiagaraGpuComputeDebug(ERHIFeatureLevel::Type InFeatureLevel)
	: FeatureLevel(InFeatureLevel)
{
}

void FNiagaraGpuComputeDebug::Tick(FRHICommandListImmediate& RHICmdList)
{
	for (auto it=DebugDrawBuffers.CreateConstIterator(); it; ++it)
	{
		FNiagaraSimulationDebugDrawData* DebugDrawData = it.Value().Get();
		if ( !DebugDrawData->bRequiresUpdate )
		{
			continue;
		}

		DebugDrawData->bRequiresUpdate = false;
		if (DebugDrawData->GpuLineMaxInstances > 0)
		{
			//-OPT: Batch UAV clears
			NiagaraDebugShaders::ClearUAV(RHICmdList, DebugDrawData->GpuLineBufferArgs.UAV, FUintVector4(2, 0, 0, 0), 4);
			RHICmdList.Transition(FRHITransitionInfo(DebugDrawData->GpuLineBufferArgs.UAV, ERHIAccess::UAVCompute, ERHIAccess::IndirectArgs));
		}

		DebugDrawData->StaticLineCount = DebugDrawData->StaticLines.Num();
		if (DebugDrawData->StaticLineCount > 0 )
		{
			constexpr uint32 NumUintsPerLine = 7;
			static_assert(sizeof(FNiagaraSimulationDebugDrawData::FGpuLine) == (NumUintsPerLine * sizeof(uint32)), "Line size does not match expected GPU size");

			const uint32 NumElements = FMath::DivideAndRoundUp(DebugDrawData->StaticLineCount, 64u) * 64u * NumUintsPerLine;
			const uint32 RequiredBytes = NumElements * sizeof(uint32);
			if ( DebugDrawData->StaticLineBuffer.NumBytes < RequiredBytes )
			{
				DebugDrawData->StaticLineBuffer.Release();
				DebugDrawData->StaticLineBuffer.Initialize(TEXT("NiagaraGpuComputeDebug::StaticLineBuffer"), sizeof(uint32), NumElements, EPixelFormat::PF_R32_UINT);
			}
			void* VertexData = RHILockBuffer(DebugDrawData->StaticLineBuffer.Buffer, 0, RequiredBytes, RLM_WriteOnly);
			FMemory::Memcpy(VertexData, DebugDrawData->StaticLines.GetData(), DebugDrawData->StaticLineCount * DebugDrawData->StaticLines.GetTypeSize());
			RHIUnlockBuffer(DebugDrawData->StaticLineBuffer.Buffer);

			DebugDrawData->StaticLines.Reset();
		}
		else
		{
			//-OPT: Release buffer?
		}
	}
}

void FNiagaraGpuComputeDebug::AddSystemInstance(FNiagaraSystemInstanceID SystemInstanceID, FString SystemName)
{
	SystemInstancesToWatch.FindOrAdd(SystemInstanceID) = SystemName;
}

void FNiagaraGpuComputeDebug::RemoveSystemInstance(FNiagaraSystemInstanceID SystemInstanceID)
{
	SystemInstancesToWatch.Remove(SystemInstanceID);
	VisualizeTextures.RemoveAll([&SystemInstanceID](const FNiagaraVisualizeTexture& Texture) -> bool { return Texture.SystemInstanceID == SystemInstanceID; });
}

void FNiagaraGpuComputeDebug::OnSystemDeallocated(FNiagaraSystemInstanceID SystemInstanceID)
{
	VisualizeTextures.RemoveAll([&SystemInstanceID](const FNiagaraVisualizeTexture& Texture) -> bool { return Texture.SystemInstanceID == SystemInstanceID; });
	DebugDrawBuffers.Remove(SystemInstanceID);
}

void FNiagaraGpuComputeDebug::AddTexture(FRDGBuilder& GraphBuilder, FNiagaraSystemInstanceID SystemInstanceID, FName SourceName, FRDGTextureRef Texture, FVector2D PreviewDisplayRange)
{
	AddAttributeTexture(GraphBuilder, SystemInstanceID, SourceName, Texture, FIntPoint::ZeroValue, FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE), PreviewDisplayRange);
}

void FNiagaraGpuComputeDebug::AddAttributeTexture(FRDGBuilder& GraphBuilder, FNiagaraSystemInstanceID SystemInstanceID, FName SourceName, FRDGTextureRef Texture, FIntPoint NumTextureAttributes, FIntVector4 AttributeIndices, FVector2D PreviewDisplayRange)
{
	FIntVector4 TextureAttributesInt4 = FIntVector4(NumTextureAttributes.X, NumTextureAttributes.Y, 0, 0);
	AddAttributeTexture(GraphBuilder, SystemInstanceID, SourceName, Texture, TextureAttributesInt4, AttributeIndices, PreviewDisplayRange);
}

void FNiagaraGpuComputeDebug::AddAttributeTexture(FRDGBuilder& GraphBuilder, FNiagaraSystemInstanceID SystemInstanceID, FName SourceName, FRDGTextureRef Texture, FIntVector4 NumTextureAttributes, FIntVector4 AttributeIndices, FVector2D PreviewDisplayRange)
{
	if (!SystemInstancesToWatch.Contains(SystemInstanceID))
	{
		return;
	}

	if (SourceName.IsNone() || (Texture == nullptr))
	{
		return;
	}

	const FRDGTextureDesc& TextureDesc = Texture->Desc;

	bool bCreateTexture = false;

	FNiagaraVisualizeTexture* VisualizeEntry = VisualizeTextures.FindByPredicate([&SourceName, &SystemInstanceID](const FNiagaraVisualizeTexture& Texture) -> bool { return Texture.SystemInstanceID == SystemInstanceID && Texture.SourceName == SourceName; });
	if (!VisualizeEntry)
	{
		VisualizeEntry = &VisualizeTextures.AddDefaulted_GetRef();
		VisualizeEntry->SystemInstanceID = SystemInstanceID;
		VisualizeEntry->SourceName = SourceName;
		bCreateTexture = true;
	}
	else
	{
		const FPooledRenderTargetDesc& EntryTextureDesc = VisualizeEntry->Texture->GetDesc();
		bCreateTexture = 
			(EntryTextureDesc.GetSize() != TextureDesc.GetSize()) ||
			(EntryTextureDesc.Format != TextureDesc.Format) ||
			(EntryTextureDesc.NumMips != TextureDesc.NumMips) ||
			(EntryTextureDesc.ArraySize != TextureDesc.ArraySize);
	}
	VisualizeEntry->NumTextureAttributes = NumTextureAttributes;
	VisualizeEntry->AttributesToVisualize = AttributeIndices;
	VisualizeEntry->PreviewDisplayRange = PreviewDisplayRange;

	// Do we need to create a texture to copy into?
	if ( bCreateTexture )
	{
		// Create a minimal copy of the Texture's Desc
		const FRHITextureCreateDesc NewTextureDesc =
			FRHITextureCreateDesc(TEXT("FNiagaraGpuComputeDebug"), TextureDesc.Dimension)
			.SetExtent(TextureDesc.Extent)
			.SetDepth(TextureDesc.Depth)
			.SetArraySize(TextureDesc.ArraySize)
			.SetFormat(TextureDesc.Format)
			.SetFlags(ETextureCreateFlags::ShaderResource)
			.SetInitialState(ERHIAccess::SRVMask);

		VisualizeEntry->Texture = CreateRenderTarget(RHICreateTexture(NewTextureDesc), TEXT("FNiagaraGpuComputeDebug"));
	}
	check(VisualizeEntry->Texture.IsValid());

	FRHICopyTextureInfo CopyInfo;
	CopyInfo.NumMips = TextureDesc.NumMips;
	CopyInfo.NumSlices = TextureDesc.ArraySize;
	AddCopyTexturePass(GraphBuilder, Texture, GraphBuilder.RegisterExternalTexture(VisualizeEntry->Texture), CopyInfo);
}

FNiagaraSimulationDebugDrawData* FNiagaraGpuComputeDebug::GetSimulationDebugDrawData(FNiagaraSystemInstanceID SystemInstanceID, bool bRequiresGpuBuffers, uint32 OverrideMaxDebugLines)
{
	TUniquePtr<FNiagaraSimulationDebugDrawData>& DebugDrawDataPtr = DebugDrawBuffers.FindOrAdd(SystemInstanceID);
	if (!DebugDrawDataPtr.IsValid())
	{
		DebugDrawDataPtr.Reset(new FNiagaraSimulationDebugDrawData());
	}

	int MaxLineInstancesToUse = FMath::Max3(DebugDrawDataPtr->GpuLineMaxInstances, (uint32) GNiagaraGpuComputeDebug_MaxLineInstances, OverrideMaxDebugLines);

	if (bRequiresGpuBuffers && (DebugDrawDataPtr->GpuLineMaxInstances != MaxLineInstancesToUse))
	{
		check(IsInRenderingThread());
		DebugDrawDataPtr->GpuLineBufferArgs.Release();
		DebugDrawDataPtr->GpuLineVertexBuffer.Release();
		DebugDrawDataPtr->GpuLineMaxInstances = MaxLineInstancesToUse;
		if (DebugDrawDataPtr->GpuLineMaxInstances > 0)
		{
			DebugDrawDataPtr->GpuLineBufferArgs.Initialize(TEXT("NiagaraGpuComputeDebug::DrawLineBufferArgs"), sizeof(uint32), 4, EPixelFormat::PF_R32_UINT, BUF_Static | BUF_DrawIndirect);
			DebugDrawDataPtr->GpuLineVertexBuffer.Initialize(TEXT("NiagaraGpuComputeDebug::DrawLineVertexBuffer"), sizeof(uint32), 7 * DebugDrawDataPtr->GpuLineMaxInstances, EPixelFormat::PF_R32_UINT, BUF_Static);

			auto& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
			{
				FUintVector4* IndirectArgs = reinterpret_cast<FUintVector4*>(RHILockBuffer(DebugDrawDataPtr->GpuLineBufferArgs.Buffer, 0, sizeof(uint32) * 4, RLM_WriteOnly));
				*IndirectArgs = FUintVector4(2, 0, 0, 0);
				RHIUnlockBuffer(DebugDrawDataPtr->GpuLineBufferArgs.Buffer);
			}

			FRHITransitionInfo Transitions[] =
			{
				FRHITransitionInfo(DebugDrawDataPtr->GpuLineBufferArgs.UAV, ERHIAccess::Unknown, ERHIAccess::IndirectArgs),
				FRHITransitionInfo(DebugDrawDataPtr->GpuLineVertexBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask),
			};
			RHICmdList.Transition(Transitions);
		}
	}

	return DebugDrawDataPtr.Get();
}

void FNiagaraGpuComputeDebug::RemoveSimulationDebugDrawData(FNiagaraSystemInstanceID SystemInstanceID)
{
	DebugDrawBuffers.Remove(SystemInstanceID);
}

bool FNiagaraGpuComputeDebug::ShouldDrawDebug() const
{
	return GNiagaraGpuComputeDebug_DrawDebugEnabled && (VisualizeTextures.Num() > 0);
}

void FNiagaraGpuComputeDebug::DrawDebug(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FScreenPassRenderTarget& Output)
{
	if (!GNiagaraGpuComputeDebug_DrawDebugEnabled || (VisualizeTextures.Num() == 0))
	{
		return;
	}

	++TickCounter;

	const UFont* Font = GEngine->GetTinyFont();
	const float FontHeight = Font->GetMaxCharHeight();

	FIntPoint Location(10.0f, Output.ViewRect.Height() - 10.0f);

	const int32 DisplayMinHeight = GNiagaraGpuComputeDebug_MinTextureHeight > 0 ? GNiagaraGpuComputeDebug_MinTextureHeight : 0;
	const int32 DisplayMaxHeight = GNiagaraGpuComputeDebug_MaxTextureHeight > 0 ? GNiagaraGpuComputeDebug_MaxTextureHeight : TNumericLimits<int32>::Max();

	for (const FNiagaraVisualizeTexture& VisualizeEntry : VisualizeTextures)
	{
		FIntVector TextureSize = VisualizeEntry.Texture->GetDesc().GetSize();
		if ( VisualizeEntry.NumTextureAttributes.X > 0 )
		{
			check(VisualizeEntry.NumTextureAttributes.Y > 0);
			TextureSize.X /= VisualizeEntry.NumTextureAttributes.X;
			TextureSize.Y /= VisualizeEntry.NumTextureAttributes.Y;
		}

		// Get system name
		const FString& SystemName = SystemInstancesToWatch.FindRef(VisualizeEntry.SystemInstanceID);

		const int32 DisplayHeight = FMath::Clamp(TextureSize.Y, DisplayMinHeight, DisplayMaxHeight);

		Location.Y -= DisplayHeight;

		NiagaraDebugShaders::VisualizeTexture(GraphBuilder, View, Output, Location, DisplayHeight, VisualizeEntry.AttributesToVisualize, GraphBuilder.RegisterExternalTexture(VisualizeEntry.Texture), VisualizeEntry.NumTextureAttributes, TickCounter, VisualizeEntry.PreviewDisplayRange);

		Location.Y -= FontHeight;

		AddDrawCanvasPass(GraphBuilder, {}, View, Output,
			[Location, SourceName=VisualizeEntry.SourceName.ToString(), SystemName, Font](FCanvas& Canvas)
			{
				Canvas.SetAllowSwitchVerticalAxis(true);
				Canvas.DrawShadowedString(Location.X, Location.Y, *FString::Printf(TEXT("DataInterface: %s, System: %s"), *SourceName, *SystemName), Font, FLinearColor(1, 1, 1));
			}
		);

		Location.Y -= 1.0f;
	}
}

void FNiagaraGpuComputeDebug::DrawSceneDebug(class FRDGBuilder& GraphBuilder, const class FViewInfo& View, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth)
{
	if (!GNiagaraGpuComputeDebug_DrawDebugEnabled)
	{
		return;
	}

	for (auto it = DebugDrawBuffers.CreateConstIterator(); it; ++it)
	{
		FNiagaraSimulationDebugDrawData* DebugDrawData = it.Value().Get();
		if (DebugDrawData->StaticLineCount > 0)
		{
			NiagaraDebugShaders::DrawDebugLines(
				GraphBuilder, View, SceneColor, SceneDepth,
				DebugDrawData->StaticLineCount,
				DebugDrawData->StaticLineBuffer.SRV
			);
		}
		if (DebugDrawData->GpuLineMaxInstances > 0)
		{
			NiagaraDebugShaders::DrawDebugLines(
				GraphBuilder, View, SceneColor, SceneDepth,
				DebugDrawData->GpuLineBufferArgs.Buffer,
				DebugDrawData->GpuLineVertexBuffer.SRV
			);
		}
	}
}

#endif //NIAGARA_COMPUTEDEBUG_ENABLED
