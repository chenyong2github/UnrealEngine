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
			const uint32 NumElements = FMath::DivideAndRoundUp(DebugDrawData->StaticLineCount, 64u) * 64u * 7u;
			const uint32 RequiredBytes = NumElements * sizeof(float);
			if ( DebugDrawData->StaticLineBuffer.NumBytes < RequiredBytes )
			{
				DebugDrawData->StaticLineBuffer.Release();
				DebugDrawData->StaticLineBuffer.Initialize(sizeof(float), NumElements, EPixelFormat::PF_R32_FLOAT, 0, TEXT("NiagaraGpuComputeDebug::StaticLineBuffer"));
			}
			void* VertexData = RHILockVertexBuffer(DebugDrawData->StaticLineBuffer.Buffer, 0, RequiredBytes, RLM_WriteOnly);
			FMemory::Memcpy(VertexData, DebugDrawData->StaticLines.GetData(), DebugDrawData->StaticLineCount * DebugDrawData->StaticLines.GetTypeSize());
			RHIUnlockVertexBuffer(DebugDrawData->StaticLineBuffer.Buffer);

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

void FNiagaraGpuComputeDebug::AddTexture(FRHICommandList& RHICmdList, FNiagaraSystemInstanceID SystemInstanceID, FName SourceName, FRHITexture* Texture, FVector2D PreviewDisplayRange)
{
	AddAttributeTexture(RHICmdList, SystemInstanceID, SourceName, Texture, FIntPoint::ZeroValue, FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE), PreviewDisplayRange);
}

void FNiagaraGpuComputeDebug::AddAttributeTexture(FRHICommandList& RHICmdList, FNiagaraSystemInstanceID SystemInstanceID, FName SourceName, FRHITexture* Texture, FIntPoint NumTextureAttributes, FIntVector4 AttributeIndices, FVector2D PreviewDisplayRange)
{
	FIntVector4 TextureAttributesInt4 = FIntVector4(NumTextureAttributes.X, NumTextureAttributes.Y, 0, 0);
	AddAttributeTexture( RHICmdList, SystemInstanceID, SourceName,  Texture, TextureAttributesInt4, AttributeIndices, PreviewDisplayRange);
}

void FNiagaraGpuComputeDebug::AddAttributeTexture(FRHICommandList& RHICmdList, FNiagaraSystemInstanceID SystemInstanceID, FName SourceName, FRHITexture* Texture, FIntVector4 NumTextureAttributes, FIntVector4 AttributeIndices, FVector2D PreviewDisplayRange)
{
	if (!SystemInstancesToWatch.Contains(SystemInstanceID))
	{
		return;
	}

	if (SourceName.IsNone() || (Texture == nullptr))
	{
		return;
	}

	FRHITexture2D* SrcTexture2D = Texture->GetTexture2D();
	FRHITexture2DArray* SrcTexture2DArray = Texture->GetTexture2DArray();
	FRHITexture3D* SrcTexture3D = Texture->GetTexture3D();
	FRHITextureCube* SrcTextureCube = Texture->GetTextureCube();
	if ( (SrcTexture2D == nullptr) && (SrcTexture2DArray == nullptr) && (SrcTexture3D == nullptr) && (SrcTextureCube == nullptr) )
	{
		return;
	}

	bool bCreateTexture = false;

	const FIntVector SrcSize = Texture->GetSizeXYZ();
	const EPixelFormat SrcFormat = Texture->GetFormat();

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
		bCreateTexture = (VisualizeEntry->Texture->GetSizeXYZ() != SrcSize) || (VisualizeEntry->Texture->GetFormat() != SrcFormat);
	}
	VisualizeEntry->NumTextureAttributes = NumTextureAttributes;
	VisualizeEntry->AttributesToVisualize = AttributeIndices;
	VisualizeEntry->PreviewDisplayRange = PreviewDisplayRange;

	// Do we need to create a texture to copy into?
	FTextureRHIRef Destination;
	if ( bCreateTexture )
	{
		if (SrcTexture2D != nullptr)
		{
			FRHIResourceCreateInfo CreateInfo;
			Destination = RHICreateTexture2D(SrcSize.X, SrcSize.Y, SrcFormat, 1, 1, TexCreate_ShaderResource, CreateInfo);
			VisualizeEntry->Texture = Destination;
		}
		else if (SrcTexture2DArray != nullptr)
		{
			FRHIResourceCreateInfo CreateInfo;
			Destination = RHICreateTexture2DArray(SrcSize.X, SrcSize.Y, SrcSize.Z, SrcFormat, 1, 1, TexCreate_ShaderResource, CreateInfo);
			VisualizeEntry->Texture = Destination;
		}
		else if (SrcTexture3D != nullptr)
		{
			FRHIResourceCreateInfo CreateInfo;
			Destination = RHICreateTexture3D(SrcSize.X, SrcSize.Y, SrcSize.Z, SrcFormat, 1, TexCreate_ShaderResource, CreateInfo);
			VisualizeEntry->Texture = Destination;
		}
		else if (SrcTextureCube != nullptr)
		{
			FRHIResourceCreateInfo CreateInfo;
			Destination = RHICreateTextureCube(SrcSize.X, SrcFormat, 1, TexCreate_ShaderResource, CreateInfo);
			VisualizeEntry->Texture = Destination;
		}
	}
	else
	{
		Destination = VisualizeEntry->Texture;
		check(Destination != nullptr);
	}

	// Copy texture
	{
		FRHITransitionInfo TransitionsBefore[] =
		{
			FRHITransitionInfo(Texture, ERHIAccess::SRVMask, ERHIAccess::CopySrc),
			FRHITransitionInfo(Destination, ERHIAccess::SRVMask, ERHIAccess::CopyDest)
		};
		RHICmdList.Transition(MakeArrayView(TransitionsBefore, UE_ARRAY_COUNT(TransitionsBefore)));
	}

	FRHICopyTextureInfo CopyInfo;
	RHICmdList.CopyTexture(Texture, Destination, CopyInfo);

	{
		FRHITransitionInfo TransitionsAfter[] =
		{
			FRHITransitionInfo(Texture, ERHIAccess::CopySrc, ERHIAccess::SRVMask),
			FRHITransitionInfo(Destination, ERHIAccess::CopyDest, ERHIAccess::SRVMask)
		};
		RHICmdList.Transition(MakeArrayView(TransitionsAfter, UE_ARRAY_COUNT(TransitionsAfter)));
	}
}

FNiagaraSimulationDebugDrawData* FNiagaraGpuComputeDebug::GetSimulationDebugDrawData(FNiagaraSystemInstanceID SystemInstanceID, bool bRequiresGpuBuffers)
{
	TUniquePtr<FNiagaraSimulationDebugDrawData>& DebugDrawDataPtr = DebugDrawBuffers.FindOrAdd(SystemInstanceID);
	if (!DebugDrawDataPtr.IsValid())
	{
		DebugDrawDataPtr.Reset(new FNiagaraSimulationDebugDrawData());
	}

	if (bRequiresGpuBuffers && (DebugDrawDataPtr->GpuLineMaxInstances != GNiagaraGpuComputeDebug_MaxLineInstances))
	{
		check(IsInRenderingThread());
		DebugDrawDataPtr->GpuLineBufferArgs.Release();
		DebugDrawDataPtr->GpuLineVertexBuffer.Release();
		DebugDrawDataPtr->GpuLineMaxInstances = GNiagaraGpuComputeDebug_MaxLineInstances;
		if (DebugDrawDataPtr->GpuLineMaxInstances > 0)
		{
			DebugDrawDataPtr->GpuLineBufferArgs.Initialize(sizeof(uint32), 4, EPixelFormat::PF_R32_UINT, BUF_Static | BUF_DrawIndirect, TEXT("NiagaraGpuComputeDebug::DrawLineBufferArgs"));
			DebugDrawDataPtr->GpuLineVertexBuffer.Initialize(sizeof(float), 7 * DebugDrawDataPtr->GpuLineMaxInstances, EPixelFormat::PF_R32_FLOAT, BUF_Static, TEXT("NiagaraGpuComputeDebug::DrawLineVertexBuffer"));

			auto& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
			{
				FUintVector4* IndirectArgs = reinterpret_cast<FUintVector4*>(RHILockVertexBuffer(DebugDrawDataPtr->GpuLineBufferArgs.Buffer, 0, sizeof(uint32) * 4, RLM_WriteOnly));
				*IndirectArgs = FUintVector4(2, 0, 0, 0);
				RHIUnlockVertexBuffer(DebugDrawDataPtr->GpuLineBufferArgs.Buffer);
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

void FNiagaraGpuComputeDebug::DrawDebug(class FRDGBuilder& GraphBuilder, const FViewInfo& View, const FScreenPassRenderTarget& Output)
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
		FIntVector TextureSize = VisualizeEntry.Texture->GetSizeXYZ();
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

		NiagaraDebugShaders::VisualizeTexture(GraphBuilder, View, Output, Location, DisplayHeight, VisualizeEntry.AttributesToVisualize, VisualizeEntry.Texture, VisualizeEntry.NumTextureAttributes, TickCounter, VisualizeEntry.PreviewDisplayRange);

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
