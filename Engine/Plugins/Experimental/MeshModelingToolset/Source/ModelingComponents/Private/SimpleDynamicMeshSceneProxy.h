// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved. 

#pragma once

#include "SimpleDynamicMeshComponent.h"


/**
 * Scene Proxy for USimpleDynamicMeshComponent.
 * 
 * Based on FProceduralMeshSceneProxy but simplified in various ways.
 * 
 * Supports wireframe-on-shaded rendering.
 * 
 */
class FSimpleDynamicMeshSceneProxy final : public FPrimitiveSceneProxy
{
private:
	UMaterialInterface* Material;

	/** The buffer containing vertex data. */
	FStaticMeshVertexBuffer StaticMeshVertexBuffer;
	/** The buffer containing the position vertex data. */
	FPositionVertexBuffer PositionVertexBuffer;
	/** The buffer containing the vertex color data. */
	FColorVertexBuffer ColorVertexBuffer;

	FDynamicMeshIndexBuffer32 IndexBuffer;
	FLocalVertexFactory VertexFactory;

	FMaterialRelevance MaterialRelevance;



public:
	/** Component that created this proxy (is there a way to look this up?) */
	USimpleDynamicMeshComponent* ParentComponent;


	FColor ConstantVertexColor = FColor::White;
	bool bIgnoreVertexColors = false;
	bool bIgnoreVertexNormals = false;


	bool bUsePerTriangleColor = false;
	TFunction<FColor(int)> PerTriangleColorFunc;


	FSimpleDynamicMeshSceneProxy(USimpleDynamicMeshComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, VertexFactory(GetScene().GetFeatureLevel(), "FSimpleDynamicMeshSceneProxy")
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
	{
		ParentComponent = Component;

		// Grab material
		Material = Component->GetMaterial(0);
		if (Material == NULL)
		{
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}
	}


	virtual ~FSimpleDynamicMeshSceneProxy()
	{
		PositionVertexBuffer.ReleaseResource();
		StaticMeshVertexBuffer.ReleaseResource();
		ColorVertexBuffer.ReleaseResource();
		IndexBuffer.ReleaseResource();
		VertexFactory.ReleaseResource();
	}





	virtual void Initialize()
	{
		FDynamicMesh3* Mesh = ParentComponent->GetMesh();

		// find suitable overlays
		FDynamicMeshUVOverlay* UVOverlay = nullptr;
		FDynamicMeshNormalOverlay* NormalOverlay = nullptr;
		if (Mesh->HasAttributes())
		{
			UVOverlay = Mesh->Attributes()->PrimaryUV();
			NormalOverlay = Mesh->Attributes()->PrimaryNormals();
		}

		// fill our buffers
		if (UVOverlay != nullptr || NormalOverlay != nullptr)
		{
			InitializeBuffersFromOverlays(Mesh, UVOverlay, NormalOverlay);
		}
		else
		{
			InitializeBuffersFromMesh_SharedVertices(Mesh);
		}

		// transfer to render thread?
		// copied from FStaticMeshVertexBuffers::InitFromDynamicVertex
		int LightMapIndex = 0;
		ENQUEUE_RENDER_COMMAND(DynamicMeshComponentVertexBuffersInit)(
			[this, LightMapIndex](FRHICommandListImmediate& RHICmdList)
		{
			InitOrUpdateResource(&this->PositionVertexBuffer);
			InitOrUpdateResource(&this->StaticMeshVertexBuffer);
			InitOrUpdateResource(&this->ColorVertexBuffer);

			FLocalVertexFactory::FDataType Data;
			this->PositionVertexBuffer.BindPositionVertexBuffer(&this->VertexFactory, Data);
			this->StaticMeshVertexBuffer.BindTangentVertexBuffer(&this->VertexFactory, Data);
			this->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&this->VertexFactory, Data);
			this->StaticMeshVertexBuffer.BindLightMapVertexBuffer(&this->VertexFactory, Data, LightMapIndex);
			this->ColorVertexBuffer.BindColorVertexBuffer(&this->VertexFactory, Data);
			this->VertexFactory.SetData(Data);

			InitOrUpdateResource(&this->VertexFactory);
		});

		// Enqueue initialization of render resource
		BeginInitResource(&PositionVertexBuffer);
		BeginInitResource(&StaticMeshVertexBuffer);
		BeginInitResource(&ColorVertexBuffer);
		BeginInitResource(&IndexBuffer);
		BeginInitResource(&VertexFactory);
	}






	virtual void FastUpdatePositions(bool bNormals)
	{
		check(bNormals == false);

		FDynamicMesh3* Mesh = ParentComponent->GetMesh();
		int NumVertices = Mesh->TriangleCount() * 3;
		check(PositionVertexBuffer.GetNumVertices() == NumVertices);

		int VertIdx = 0;
		for (int TriangleID : Mesh->TriangleIndicesItr())
		{
			FIndex3i Tri = Mesh->GetTriangle(TriangleID);
			for (int j = 0; j < 3; ++j)
			{
				PositionVertexBuffer.VertexPosition(VertIdx++) = Mesh->GetVertex(Tri[j]);
			}
		}

		// @todo is it necessary to rebind everything? does this re-upload the buffers?
		int LightMapIndex = 0;
		ENQUEUE_RENDER_COMMAND(DynamicMeshComponentFastUpdatePositions)(
			[this, LightMapIndex](FRHICommandListImmediate& RHICmdList)
		{
			InitOrUpdateResource(&this->PositionVertexBuffer);

			FLocalVertexFactory::FDataType Data;
			this->PositionVertexBuffer.BindPositionVertexBuffer(&this->VertexFactory, Data);
			this->StaticMeshVertexBuffer.BindTangentVertexBuffer(&this->VertexFactory, Data);
			this->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&this->VertexFactory, Data);
			this->StaticMeshVertexBuffer.BindLightMapVertexBuffer(&this->VertexFactory, Data, LightMapIndex);
			this->ColorVertexBuffer.BindColorVertexBuffer(&this->VertexFactory, Data);
			this->VertexFactory.SetData(Data);

			InitOrUpdateResource(&this->VertexFactory);
		});

		// don't need to do this because they aren't new buffers??
		//BeginInitResource(&PositionVertexBuffer);
		//BeginInitResource(&VertexFactory);
	}






	virtual void FastUpdateColors()
	{
		FDynamicMesh3* Mesh = ParentComponent->GetMesh();
		int NumVertices = Mesh->TriangleCount() * 3;
		check(ColorVertexBuffer.GetNumVertices() == NumVertices);

		bool bHaveColors = Mesh->HasVertexColors() && (bIgnoreVertexColors == false);

		int VertIdx = 0;
		for (int TriangleID : Mesh->TriangleIndicesItr())
		{
			FIndex3i Tri = Mesh->GetTriangle(TriangleID);

			FColor TriColor = ConstantVertexColor;
			if (bUsePerTriangleColor && PerTriangleColorFunc)
			{
				TriColor = PerTriangleColorFunc(TriangleID);
				bHaveColors = false;
			}

			for (int j = 0; j < 3; ++j)
			{
				ColorVertexBuffer.VertexColor(VertIdx) = (bHaveColors) ?
					(FColor)Mesh->GetVertexColor(Tri[j]) : TriColor;
				VertIdx++;
			}
		}

		// @todo is it necessary to rebind everything? does this re-upload the buffers?
		int LightMapIndex = 0;
		ENQUEUE_RENDER_COMMAND(DynamicMeshComponentFastUpdateColors)(
			[this, LightMapIndex](FRHICommandListImmediate& RHICmdList)
		{
			InitOrUpdateResource(&this->ColorVertexBuffer);

			FLocalVertexFactory::FDataType Data;
			this->PositionVertexBuffer.BindPositionVertexBuffer(&this->VertexFactory, Data);
			this->StaticMeshVertexBuffer.BindTangentVertexBuffer(&this->VertexFactory, Data);
			this->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&this->VertexFactory, Data);
			this->StaticMeshVertexBuffer.BindLightMapVertexBuffer(&this->VertexFactory, Data, LightMapIndex);
			this->ColorVertexBuffer.BindColorVertexBuffer(&this->VertexFactory, Data);
			this->VertexFactory.SetData(Data);

			InitOrUpdateResource(&this->VertexFactory);
		});

		BeginInitResource(&ColorVertexBuffer);		// calls ENQUEUE_RENDER_COMMAND
		//BeginInitResource(&VertexFactory);
	}



protected:


	/**
	 * Initialize rendering buffers from given attribute overlays.
	 * Creates three vertices per triangle, IE no shared vertices in buffers.
	 */
	void InitializeBuffersFromOverlays(FDynamicMesh3* Mesh, FDynamicMeshUVOverlay* UVOverlay, FDynamicMeshNormalOverlay* NormalOverlay)
	{
		// compute normals if we need to
		FMeshNormals Normals(Mesh);
		bool bUseComputedNormals = false;
		if (NormalOverlay == nullptr && (Mesh->HasVertexNormals() == false || bIgnoreVertexNormals) )
		{
			Normals.ComputeVertexNormals();
			bUseComputedNormals = true;
		}

		bool bHaveColors = Mesh->HasVertexColors() && (bIgnoreVertexColors == false);

		// this may be null
		FMeshTangentsf* Tangents = ParentComponent->GetTangents();


		int NumVertices = Mesh->TriangleCount() * 3;
		int NumTexCoords = 1;		// no! zero!
		PositionVertexBuffer.Init(NumVertices);
		StaticMeshVertexBuffer.Init(NumVertices, NumTexCoords);
		ColorVertexBuffer.Init(NumVertices);

		IndexBuffer.Indices.AddUninitialized(Mesh->TriangleCount() * 3);
		int TriIdx = 0, VertIdx = 0;
		FVector3f TangentX, TangentY;
		for (int TriangleID : Mesh->TriangleIndicesItr())
		{
			FIndex3i Tri = Mesh->GetTriangle(TriangleID);
			FIndex3i TriUV = (UVOverlay != nullptr) ? UVOverlay->GetTriangle(TriangleID) : FIndex3i::Zero();
			FIndex3i TriNormal = (NormalOverlay != nullptr) ? NormalOverlay->GetTriangle(TriangleID) : FIndex3i::Zero();

			FColor TriColor = ConstantVertexColor;
			if (bUsePerTriangleColor && PerTriangleColorFunc)
			{
				TriColor = PerTriangleColorFunc(TriangleID);
				bHaveColors = false;
			}

			for (int j = 0; j < 3; ++j)
			{
				PositionVertexBuffer.VertexPosition(VertIdx) = Mesh->GetVertex(Tri[j]);

				FVector3f Normal = (NormalOverlay != nullptr && TriNormal[j] != FDynamicMesh3::InvalidID) ? NormalOverlay->GetElement(TriNormal[j])
					: ((bUseComputedNormals) ? (FVector3f)Normals[Tri[j]] : Mesh->GetVertexNormal(Tri[j]));

				if (Tangents == nullptr)
				{
					// calculate a nonsense tangent
					VectorUtil::MakePerpVectors(Normal, TangentX, TangentY);
					StaticMeshVertexBuffer.SetVertexTangents(VertIdx, TangentX, TangentY, Normal);
				}
				else
				{
					Tangents->GetPerTriangleTangent(TriangleID, j, TangentX, TangentY);
				}

				StaticMeshVertexBuffer.SetVertexTangents(VertIdx, TangentX, TangentY, Normal);

				FVector2f UV = (UVOverlay != nullptr && TriUV[j] != FDynamicMesh3::InvalidID) ? UVOverlay->GetElement(TriUV[j]) : FVector2f::Zero();
				StaticMeshVertexBuffer.SetVertexUV(VertIdx, 0, UV);

				ColorVertexBuffer.VertexColor(VertIdx) = (bHaveColors) ?
					(FColor)Mesh->GetVertexColor(Tri[j]) : TriColor;

				IndexBuffer.Indices[TriIdx++] = VertIdx;
				VertIdx++;
			}
		}
	}





	/**
	 * Initialize rendering buffers from DMesh using shared vertices
	 * (ie no attribute overlays)
	 */
	void InitializeBuffersFromMesh_SharedVertices(FDynamicMesh3* Mesh)
	{
		// compute normals if we need to
		FMeshNormals Normals(Mesh);
		bool bUseComputedNormals = false;
		if (Mesh->HasVertexNormals() == false || bIgnoreVertexNormals)
		{
			Normals.ComputeVertexNormals();
			bUseComputedNormals = true;
		}

		bool bHaveColors = Mesh->HasVertexColors() && (bIgnoreVertexColors == false);

		int NumVertices = Mesh->MaxVertexID();
		int NumTexCoords = 1;		// no! zero!
		PositionVertexBuffer.Init(NumVertices);
		StaticMeshVertexBuffer.Init(NumVertices, NumTexCoords);
		ColorVertexBuffer.Init(NumVertices);

		for (int VertIdx : Mesh->VertexIndicesItr())
		{
			PositionVertexBuffer.VertexPosition(VertIdx) = Mesh->GetVertex(VertIdx);

			FVector3d Normal = (bUseComputedNormals) ? Normals[VertIdx] : (FVector3d)Mesh->GetVertexNormal(VertIdx);
			FVector3d TangentX, TangentY;
			VectorUtil::MakePerpVectors(Normal, TangentX, TangentY);

			StaticMeshVertexBuffer.SetVertexTangents(VertIdx, TangentX, TangentY, Normal);
			StaticMeshVertexBuffer.SetVertexUV(VertIdx, 0, FVector2D::ZeroVector);

			ColorVertexBuffer.VertexColor(VertIdx) = (bHaveColors) ?
				(FColor)Mesh->GetVertexColor(VertIdx) : ConstantVertexColor;
		}

		IndexBuffer.Indices.AddUninitialized(Mesh->TriangleCount() * 3);
		int TriIndex = 0;
		for (FIndex3i Tri : Mesh->TrianglesItr())
		{
			IndexBuffer.Indices[TriIndex++] = Tri.A;
			IndexBuffer.Indices[TriIndex++] = Tri.B;
			IndexBuffer.Indices[TriIndex++] = Tri.C;
		}
	}




public:



	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_SimpleDynamicMeshSceneProxy_GetDynamicMeshElements);

		const bool bWireframe = (AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe)
			|| ParentComponent->bExplicitShowWireframe;

		FColoredMaterialRenderProxy* WireframeMaterialInstance = new FColoredMaterialRenderProxy(
			GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
			FLinearColor(0, 0.5f, 1.f)
		);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);

		FMaterialRenderProxy* MaterialProxy = Material->GetRenderProxy();
		FMaterialRenderProxy* WireframeMaterialProxy = WireframeMaterialInstance;

		ESceneDepthPriorityGroup DepthPriority = (ParentComponent->bDrawOnTop) ? SDPG_Foreground : SDPG_World;


		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];

				bool bHasPrecomputedVolumetricLightmap;
				FMatrix PreviousLocalToWorld;
				int32 SingleCaptureIndex;
				bool bOutputVelocity;
				GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

				FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
				DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, DrawsVelocity(), bOutputVelocity);

				// Draw the mesh.
				DrawBatch(Collector, MaterialProxy, false, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
				if (bWireframe)
				{
					DrawBatch(Collector, WireframeMaterialProxy, true, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
				}
			}
		}
	}



	virtual void DrawBatch(FMeshElementCollector& Collector, 
		FMaterialRenderProxy* UseMaterial, 
		bool bWireframe,
		ESceneDepthPriorityGroup DepthPriority,
		int ViewIndex,
		FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer) const
	{
		FMeshBatch& Mesh = Collector.AllocateMesh();
		FMeshBatchElement& BatchElement = Mesh.Elements[0];
		BatchElement.IndexBuffer = &IndexBuffer;
		Mesh.bWireframe = bWireframe;
		Mesh.VertexFactory = &VertexFactory;
		Mesh.MaterialRenderProxy = UseMaterial;

		BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

		BatchElement.FirstIndex = 0;
		BatchElement.NumPrimitives = IndexBuffer.Indices.Num() / 3;
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = PositionVertexBuffer.GetNumVertices() - 1;
		Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
		Mesh.Type = PT_TriangleList;
		Mesh.DepthPriorityGroup = DepthPriority;
		Mesh.bCanApplyViewModeOverrides = false;
		Collector.AddMesh(ViewIndex, Mesh);
	}



	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;


		if (ParentComponent->bDrawOnTop)
		{
			Result.bDrawRelevance = IsShown(View);
			Result.bDynamicRelevance = true;
			Result.bShadowRelevance = false;
			Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
			Result.bEditorNoDepthTestPrimitiveRelevance = true;
		}
		else
		{
			Result.bDrawRelevance = IsShown(View);
			Result.bShadowRelevance = IsShadowCast(View);
			Result.bDynamicRelevance = true;
			Result.bRenderInMainPass = ShouldRenderInMainPass();
			Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
			Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
			Result.bRenderCustomDepth = ShouldRenderCustomDepth();
			MaterialRelevance.SetPrimitiveViewRelevance(Result);
			Result.bVelocityRelevance = IsMovable() && Result.bOpaqueRelevance && Result.bRenderInMainPass;
		}

		return Result;
	}


	virtual void GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const override
	{
		if (ParentComponent->bDrawOnTop)
		{
			bDynamic = false;
			bRelevant = false;
			bLightMapped = false;
			bShadowMapped = false;
		}
		else
		{
			FPrimitiveSceneProxy::GetLightRelevance(LightSceneProxy, bDynamic, bRelevant, bLightMapped, bShadowMapped);
		}
	}


	virtual bool CanBeOccluded() const override
	{
		//if (ParentComponent->bDrawOnTop)
		//{
		//	return false;
		//}
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }

	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }



	virtual void SetMaterial(UMaterialInterface* MaterialIn)
	{
		this->Material = MaterialIn;
	}


	// copied from StaticMesh.cpp
	void InitOrUpdateResource(FRenderResource* Resource)
	{
		if (!Resource->IsInitialized())
		{
			Resource->InitResource();
		}
		else
		{
			Resource->UpdateRHI();
		}
	}


	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}
};







