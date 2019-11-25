// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PaperSpriteVertexBuffer.h"
#include "Materials/Material.h"
#include "SceneManagement.h"
#include "PhysicsEngine/BodySetup.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "PaperSpriteComponent.h"

//////////////////////////////////////////////////////////////////////////
// FPaperSpriteVertexBuffer

void FPaperSpriteVertexBuffer::SetDynamicUsage(bool bInDynamicUsage)
{
	bDynamicUsage = bInDynamicUsage;
}

void FPaperSpriteVertexBuffer::CreateBuffers(int32 InNumVertices)
{
	//Make sure we don't have dangling buffers
	if (NumAllocatedVertices > 0)
	{
		ReleaseBuffers();
	}

	//The buffer will always be a shader resource, but they can be static/dynamic depending of the usage
	uint32 Usage = BUF_ShaderResource | (bDynamicUsage ? BUF_Dynamic : BUF_Static);
	NumAllocatedVertices = InNumVertices;

	uint32 PositionSize = NumAllocatedVertices * sizeof(FVector);
	// create vertex buffer
	{
		FRHIResourceCreateInfo CreateInfo;
		PositionBuffer.VertexBufferRHI = RHICreateVertexBuffer(PositionSize, Usage, CreateInfo);
		if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
		{
			PositionBufferSRV = RHICreateShaderResourceView(PositionBuffer.VertexBufferRHI, sizeof(float), PF_R32_FLOAT);
		}

	}

	uint32 TangentSize = NumAllocatedVertices * 2 * sizeof(FPackedNormal);
	// create vertex buffer
	{
		FRHIResourceCreateInfo CreateInfo;
		TangentBuffer.VertexBufferRHI = RHICreateVertexBuffer(TangentSize, Usage, CreateInfo);
		if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
		{
			TangentBufferSRV = RHICreateShaderResourceView(TangentBuffer.VertexBufferRHI, sizeof(FPackedNormal), PF_R8G8B8A8_SNORM);
		}
	}

	uint32 TexCoordSize = NumAllocatedVertices * sizeof(FVector2D);
	// create vertex buffer
	{
		FRHIResourceCreateInfo CreateInfo;
		TexCoordBuffer.VertexBufferRHI = RHICreateVertexBuffer(TexCoordSize, Usage, CreateInfo);
		if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
		{
			TexCoordBufferSRV = RHICreateShaderResourceView(TexCoordBuffer.VertexBufferRHI, sizeof(FVector2D), PF_G32R32F);
		}
	}

	uint32 ColorSize = NumAllocatedVertices * sizeof(FColor);
	// create vertex buffer
	{
		FRHIResourceCreateInfo CreateInfo;
		ColorBuffer.VertexBufferRHI = RHICreateVertexBuffer(ColorSize, Usage, CreateInfo);
		if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
		{
			ColorBufferSRV = RHICreateShaderResourceView(ColorBuffer.VertexBufferRHI, sizeof(FColor), PF_R8G8B8A8);
		}
	}

	//Create Index Buffer
	{
		FRHIResourceCreateInfo CreateInfo;
		IndexBuffer.IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint32), Vertices.Num() * sizeof(uint32), Usage, CreateInfo);
	}
}

void FPaperSpriteVertexBuffer::ReleaseBuffers()
{
	PositionBuffer.ReleaseRHI();
	TangentBuffer.ReleaseRHI();
	TexCoordBuffer.ReleaseRHI();
	ColorBuffer.ReleaseRHI();
	IndexBuffer.ReleaseRHI();

	TangentBufferSRV.SafeRelease();
	TexCoordBufferSRV.SafeRelease();
	ColorBufferSRV.SafeRelease();
	PositionBufferSRV.SafeRelease();

	NumAllocatedVertices = 0;
}

void FPaperSpriteVertexBuffer::CommitVertexData()
{
	if (Vertices.Num())
	{
		//Check if we have to accommodate the buffer size
		if (NumAllocatedVertices != Vertices.Num())
		{
			CreateBuffers(Vertices.Num());
		}

		//Lock vertices
		FVector* PositionBufferData = nullptr;
		uint32 PositionSize = Vertices.Num() * sizeof(FVector);
		{
			void* Data = RHILockVertexBuffer(PositionBuffer.VertexBufferRHI, 0, PositionSize, RLM_WriteOnly);
			PositionBufferData = static_cast<FVector*>(Data);
		}

		FPackedNormal* TangentBufferData = nullptr;
		uint32 TangentSize = Vertices.Num() * 2 * sizeof(FPackedNormal);
		{
			void* Data = RHILockVertexBuffer(TangentBuffer.VertexBufferRHI, 0, TangentSize, RLM_WriteOnly);
			TangentBufferData = static_cast<FPackedNormal*>(Data);
		}

		FVector2D* TexCoordBufferData = nullptr;
		uint32 TexCoordSize = Vertices.Num() * sizeof(FVector2D);
		{
			void* Data = RHILockVertexBuffer(TexCoordBuffer.VertexBufferRHI, 0, TexCoordSize, RLM_WriteOnly);
			TexCoordBufferData = static_cast<FVector2D*>(Data);
		}

		FColor* ColorBufferData = nullptr;
		uint32 ColorSize = Vertices.Num() * sizeof(FColor);
		{
			void* Data = RHILockVertexBuffer(ColorBuffer.VertexBufferRHI, 0, ColorSize, RLM_WriteOnly);
			ColorBufferData = static_cast<FColor*>(Data);
		}

		uint32* IndexBufferData = nullptr;
		uint32 IndexSize = Vertices.Num() * sizeof(uint32);
		{
			void* Data = RHILockIndexBuffer(IndexBuffer.IndexBufferRHI, 0, IndexSize, RLM_WriteOnly);
			IndexBufferData = static_cast<uint32*>(Data);
		}

		//Fill verts
		for (int32 i = 0; i < Vertices.Num(); i++)
		{
			PositionBufferData[i] = Vertices[i].Position;
			TangentBufferData[2 * i + 0] = Vertices[i].TangentX;
			TangentBufferData[2 * i + 1] = Vertices[i].TangentZ;
			ColorBufferData[i] = Vertices[i].Color;
			TexCoordBufferData[i] = Vertices[i].TextureCoordinate[0];
			IndexBufferData[i] = i;
		}

		// Unlock the buffer.
		RHIUnlockVertexBuffer(PositionBuffer.VertexBufferRHI);
		RHIUnlockVertexBuffer(TangentBuffer.VertexBufferRHI);
		RHIUnlockVertexBuffer(TexCoordBuffer.VertexBufferRHI);
		RHIUnlockVertexBuffer(ColorBuffer.VertexBufferRHI);
		RHIUnlockIndexBuffer(IndexBuffer.IndexBufferRHI);

		//We clear the vertex data, as it isn't needed anymore
		Vertices.Empty();
	}
}

void FPaperSpriteVertexBuffer::InitRHI()
{
	//Automatically try to create the data and use it
	CommitVertexData();
}

void FPaperSpriteVertexBuffer::ReleaseRHI()
{
	PositionBuffer.ReleaseRHI();
	TangentBuffer.ReleaseRHI();
	TexCoordBuffer.ReleaseRHI();
	ColorBuffer.ReleaseRHI();
	IndexBuffer.ReleaseRHI();

	TangentBufferSRV.SafeRelease();
	TexCoordBufferSRV.SafeRelease();
	ColorBufferSRV.SafeRelease();
	PositionBufferSRV.SafeRelease();
}

void FPaperSpriteVertexBuffer::InitResource()
{
	FRenderResource::InitResource();
	PositionBuffer.InitResource();
	TangentBuffer.InitResource();
	TexCoordBuffer.InitResource();
	ColorBuffer.InitResource();
	IndexBuffer.InitResource();
}

void FPaperSpriteVertexBuffer::ReleaseResource()
{
	FRenderResource::ReleaseResource();
	PositionBuffer.ReleaseResource();
	TangentBuffer.ReleaseResource();
	TexCoordBuffer.ReleaseResource();
	ColorBuffer.ReleaseResource();
	IndexBuffer.ReleaseResource();
}

//////////////////////////////////////////////////////////////////////////
// FPaperSpriteVertexFactory

FPaperSpriteVertexFactory::FPaperSpriteVertexFactory(ERHIFeatureLevel::Type FeatureLevel)
	: FLocalVertexFactory(FeatureLevel, "FPaperSpriteVertexFactory")
{
}

void FPaperSpriteVertexFactory::Init(const FPaperSpriteVertexBuffer* InVertexBuffer)
{
	if (IsInRenderingThread())
	{
		FLocalVertexFactory::FDataType VertexData;
		VertexData.NumTexCoords = 1;

		//SRV setup
		VertexData.LightMapCoordinateIndex = 0;
		VertexData.TangentsSRV = InVertexBuffer->TangentBufferSRV;
		VertexData.TextureCoordinatesSRV = InVertexBuffer->TexCoordBufferSRV;
		VertexData.ColorComponentsSRV = InVertexBuffer->ColorBufferSRV;
		VertexData.PositionComponentSRV = InVertexBuffer->PositionBufferSRV;

		// Vertex Streams
		VertexData.PositionComponent = FVertexStreamComponent(&InVertexBuffer->PositionBuffer, 0, sizeof(FVector), VET_Float3, EVertexStreamUsage::Default);
		VertexData.TangentBasisComponents[0] = FVertexStreamComponent(&InVertexBuffer->TangentBuffer, 0, 2 * sizeof(FPackedNormal), VET_PackedNormal, EVertexStreamUsage::ManualFetch);
		VertexData.TangentBasisComponents[1] = FVertexStreamComponent(&InVertexBuffer->TangentBuffer, sizeof(FPackedNormal), 2 * sizeof(FPackedNormal), VET_PackedNormal, EVertexStreamUsage::ManualFetch);
		VertexData.ColorComponent = FVertexStreamComponent(&InVertexBuffer->ColorBuffer, 0, sizeof(FColor), VET_Color, EVertexStreamUsage::ManualFetch);
		VertexData.TextureCoordinates.Add(FVertexStreamComponent(&InVertexBuffer->TexCoordBuffer, 0, sizeof(FVector2D), VET_Float2, EVertexStreamUsage::ManualFetch));

		SetData(VertexData);
		VertexBuffer = InVertexBuffer;

		InitResource();
	}
	else
	{
		FPaperSpriteVertexFactory* ThisFactory = this;
		ENQUEUE_RENDER_COMMAND(SpriteVertexFactoryInit)(
			[ThisFactory, InVertexBuffer](FRHICommandListImmediate& RHICmdList)
		{
			ThisFactory->Init(InVertexBuffer);
		});
	}
}
