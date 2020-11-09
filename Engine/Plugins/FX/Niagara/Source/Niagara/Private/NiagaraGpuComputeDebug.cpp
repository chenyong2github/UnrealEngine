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

int32 GNiagaraGpuComputeDebug_MaxTextureHeight = 128;
static FAutoConsoleVariableRef CVarNiagaraGpuComputeDebug_MaxTextureHeight(
	TEXT("fx.Niagara.GpuComputeDebug.MaxTextureHeight"),
	GNiagaraGpuComputeDebug_MaxTextureHeight,
	TEXT("The maximum height we will visualize a texture at, this is to avoid things becoming too large on screen."),
	ECVF_Default
);

//////////////////////////////////////////////////////////////////////////

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
}

void FNiagaraGpuComputeDebug::AddTexture(FRHICommandList& RHICmdList, FNiagaraSystemInstanceID SystemInstanceID, FName SourceName, FRHITexture* Texture)
{
	AddAttributeTexture(RHICmdList, SystemInstanceID, SourceName, Texture, FIntPoint::ZeroValue, FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE));
}

void FNiagaraGpuComputeDebug::AddAttributeTexture(FRHICommandList& RHICmdList, FNiagaraSystemInstanceID SystemInstanceID, FName SourceName, FRHITexture* Texture, FIntPoint NumTextureAttributes, FIntVector4 AttributeIndices)
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
	if ( (SrcTexture2D == nullptr) && (SrcTexture2DArray == nullptr) && (SrcTexture3D == nullptr) )
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

bool FNiagaraGpuComputeDebug::ShouldDrawDebug() const
{
	return VisualizeTextures.Num() > 0;
}

void FNiagaraGpuComputeDebug::DrawDebug(class FRDGBuilder& GraphBuilder, const FViewInfo& View, const FScreenPassRenderTarget& Output)
{
	if (VisualizeTextures.Num() == 0)
	{
		return;
	}

	++TickCounter;

	const UFont* Font = GEngine->GetTinyFont();
	const float FontHeight = Font->GetMaxCharHeight();

	FIntPoint Location(10.0f, Output.ViewRect.Height() - 10.0f);

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

		const int32 DisplayHeight = GNiagaraGpuComputeDebug_MaxTextureHeight > 0 ? GNiagaraGpuComputeDebug_MaxTextureHeight : TextureSize.Y;
		Location.Y -= DisplayHeight;

		NiagaraDebugShaders::VisualizeTexture(GraphBuilder, View, Output, Location, DisplayHeight, VisualizeEntry.AttributesToVisualize, VisualizeEntry.Texture, VisualizeEntry.NumTextureAttributes, TickCounter);

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
