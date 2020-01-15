// Copyright Epic Games, Inc. All Rights Reserved.

#include "VT/RuntimeVirtualTextureSceneProxy.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "VirtualTextureSystem.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureProducer.h"

int32 FRuntimeVirtualTextureSceneProxy::ProducerIdGenerator = 1;

FRuntimeVirtualTextureSceneProxy::FRuntimeVirtualTextureSceneProxy(URuntimeVirtualTextureComponent* InComponent)
	: SceneIndex(0)
	, ProducerId(0)
	, VirtualTexture(nullptr)
	, CombinedDirtyRect(0, 0, 0, 0)
{
	if (InComponent->GetVirtualTexture() != nullptr && InComponent->GetVirtualTexture()->GetEnabled())
	{
		// We store a ProducerId here so that we will be able to find our SceneIndex from the Producer during rendering.
		// We will need the SceneIndex to determine which primitives should render to this Producer.
		ProducerId = ProducerIdGenerator++;

		VirtualTexture = InComponent->GetVirtualTexture();
		Transform = InComponent->GetVirtualTextureTransform();

		// The producer description is calculated using the transform to determine the aspect ratio
		FVTProducerDescription Desc;
		VirtualTexture->GetProducerDescription(Desc, Transform);
		VirtualTextureSize = FIntPoint(Desc.BlockWidthInTiles * Desc.TileSize, Desc.BlockHeightInTiles * Desc.TileSize);
		// We only need to dirty flush up to the producer description MaxLevel which accounts for the RemoveLowMips
		MaxDirtyLevel = Desc.MaxLevel;

		const ERuntimeVirtualTextureMaterialType MaterialType = VirtualTexture->GetMaterialType();
		const bool bClearTextures = VirtualTexture->GetClearTextures();

		// The Producer object created here will be passed into the virtual texture system which will take ownership.
		IVirtualTexture* Producer = new FRuntimeVirtualTextureProducer(Desc, ProducerId, MaterialType, bClearTextures, InComponent->GetScene(), Transform);

		if (InComponent->IsStreamingLowMips() && VirtualTexture->GetStreamLowMips() > 0)
		{
			// Streaming mips start from the MaxLevel before taking into account the RemoveLowMips
			const int32 MaxLevel = FMath::CeilLogTwo(FMath::Max(Desc.BlockWidthInTiles, Desc.BlockHeightInTiles));
			// Wrap our producer to use a streaming producer for low mips
			int32 StreamingTransitionLevel;
			Producer = VirtualTexture->CreateStreamingTextureProducer(Producer, MaxLevel, StreamingTransitionLevel);
			// Any dirty flushes don't need to flush the streaming mips (they only change with a build step).
			MaxDirtyLevel = FMath::Min(MaxDirtyLevel, StreamingTransitionLevel);
		}

		// The Initialize() call will allocate the virtual texture by spawning work on the render thread.
		VirtualTexture->Initialize(Producer, Transform);
	}
}

FRuntimeVirtualTextureSceneProxy::~FRuntimeVirtualTextureSceneProxy()
{
	checkSlow(IsInRenderingThread());
}

void FRuntimeVirtualTextureSceneProxy::Release()
{
	if (VirtualTexture != nullptr)
	{
		VirtualTexture->Release();
		VirtualTexture = nullptr;
	}
}

void FRuntimeVirtualTextureSceneProxy::Dirty(FBoxSphereBounds const& Bounds)
{
	// Transform world bounds into Virtual Texture UV space
	const FVector O = Transform.GetTranslation();
	const FVector U = Transform.GetUnitAxis(EAxis::X) * 1.f / Transform.GetScale3D().X;
	const FVector V = Transform.GetUnitAxis(EAxis::Y) * 1.f / Transform.GetScale3D().Y;
	const FVector P = Bounds.GetSphere().Center - O;
	const FVector2D UVCenter = FVector2D(FVector::DotProduct(P, U), FVector::DotProduct(P, V));
	const float Scale = FMath::Max(1.f / Transform.GetScale3D().X, 1.f / Transform.GetScale3D().Y);
	const float UVRadius = Bounds.GetSphere().W * Scale;
	const FVector2D UVExtent(UVRadius, UVRadius);
	const FBox2D UVRect = FBox2D(UVCenter - UVExtent, UVCenter + UVExtent);

	// Convert to Texel coordinate space
	const FIntRect TextureRect(0, 0, VirtualTextureSize.X, VirtualTextureSize.Y);
	FIntRect TexelRect(
		FMath::FloorToInt(UVRect.Min.X * VirtualTextureSize.X),
		FMath::FloorToInt(UVRect.Min.Y * VirtualTextureSize.Y),
		FMath::CeilToInt(UVRect.Max.X * VirtualTextureSize.X),
		FMath::CeilToInt(UVRect.Max.Y * VirtualTextureSize.Y));
	TexelRect.Clip(TextureRect);

	// Only add rect if it has some area
	if (TexelRect.Min != TexelRect.Max)
	{
		const bool bFirst = DirtyRects.Add(TexelRect) == 0;
		if (bFirst)
		{
			CombinedDirtyRect = TexelRect;
		}
		else
		{
			CombinedDirtyRect.Union(TexelRect);
		}
	}
}

void FRuntimeVirtualTextureSceneProxy::FlushDirtyPages()
{
	// If Producer handle is not initialized yet it's safe to do nothing because we won't have rendered anything to the VT that needs flushing.
	if (ProducerHandle.PackedValue != 0)
	{
		//todo[vt]: 
		// Profile to work out best heuristic for when we should use the CombinedDirtyRect
		// Also consider using some other structure to represent dirty area such as a course 2D bitfield
		const bool bCombinedFlush = (DirtyRects.Num() > 2 || CombinedDirtyRect == FIntRect(0, 0, VirtualTextureSize.X, VirtualTextureSize.Y));

		if (bCombinedFlush)
		{
			FVirtualTextureSystem::Get().FlushCache(ProducerHandle, CombinedDirtyRect, MaxDirtyLevel);
		}
		else
		{
			for (FIntRect Rect : DirtyRects)
			{
				FVirtualTextureSystem::Get().FlushCache(ProducerHandle, Rect, MaxDirtyLevel);
			}
		}
	}

	DirtyRects.Reset();
	CombinedDirtyRect = FIntRect(0, 0, 0, 0);
}
