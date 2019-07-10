// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VT/RuntimeVirtualTextureSceneProxy.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureProducer.h"

int32 FRuntimeVirtualTextureSceneProxy::ProducerIdGenerator = 1;

FRuntimeVirtualTextureSceneProxy::FRuntimeVirtualTextureSceneProxy(URuntimeVirtualTextureComponent* InComponent)
	: ProducerId(0)
	, VirtualTexture(InComponent->GetVirtualTexture())
{
	if (VirtualTexture != nullptr)
	{
		// We store a ProducerId here so that we will be able to find our SceneIndex from the Producer during rendering.
		// We will need the SceneIndex to determine which primitives should render to this Producer.
		ProducerId = ProducerIdGenerator++;

		const ERuntimeVirtualTextureMaterialType MaterialType = VirtualTexture->GetMaterialType();

		// Transform is based on bottom left of the URuntimeVirtualTextureComponent unit box (which is centered on the origin)
		FTransform Transform = FTransform(FVector(-0.5f, -0.5f, 0.f)) * InComponent->GetComponentTransform();

		// The producer description is calculated using the transform to determine the aspect ratio
		FVTProducerDescription Desc;
		VirtualTexture->GetProducerDescription(Desc, Transform);

		// The Producer object created here will be passed into the virtual texture system which will take ownership.
		// The Initialize() call will allocate the virtual texture by spawning work on the render thread.
		FRuntimeVirtualTextureProducer* Producer = new FRuntimeVirtualTextureProducer(Desc, ProducerId, MaterialType, InComponent->GetScene(), Transform);
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
