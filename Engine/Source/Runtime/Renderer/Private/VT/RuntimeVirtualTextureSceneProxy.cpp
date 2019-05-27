// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VT/RuntimeVirtualTextureSceneProxy.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureProducer.h"


FRuntimeVirtualTextureSceneProxy::FRuntimeVirtualTextureSceneProxy(URuntimeVirtualTextureComponent* InComponent)
	: VirtualTexture(InComponent->GetVirtualTexture())
{
	if (VirtualTexture != nullptr)
	{
		// The Producer object created here will be passed into the Virtual Texture system which will take ownership
		FVTProducerDescription Desc;
		VirtualTexture->GetProducerDescription(Desc);

		const ERuntimeVirtualTextureMaterialType MaterialType = VirtualTexture->GetMaterialType();

		// Transform is based on bottom left of the URuntimeVirtualTextureComponent unit box (which is centered on the origin)
		FTransform Transform = FTransform(FVector(-0.5f, -0.5f, 0.f)) * InComponent->GetComponentTransform();

		FRuntimeVirtualTextureProducer* Producer = new FRuntimeVirtualTextureProducer(Desc, MaterialType, InComponent->GetScene(), Transform);
		VirtualTexture->Initialize(Producer, Transform);
	}
}

FRuntimeVirtualTextureSceneProxy::~FRuntimeVirtualTextureSceneProxy()
{
	if (VirtualTexture != nullptr)
	{
		VirtualTexture->Release();
	}
}
