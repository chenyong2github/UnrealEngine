// Copyright Epic Games, Inc. All Rights Reserved.


#include "Text3DComponent.h"

#include "Bevel.h"
#include "Data.h"
#include "Engine/Font.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Misc/FileHelper.h"
#include "PrimitiveSceneProxy.h"
#include "RenderResource.h"
#include "StaticMeshResources.h"
#include "Text3DPrivate.h"
#include "TextShaper.h"
#include "UObject/ConstructorHelpers.h"

#include "Misc/EngineVersionComparison.h"


THIRD_PARTY_INCLUDES_START
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#endif //PLATFORM_WINDOWS
#include "GL/glcorearb.h"
#include "FTVectoriser.h"

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif //PLATFORM_WINDOWS
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "Text3D"


using TTextMeshDynamicData = TArray<TUniquePtr<FText3DDynamicData>, TFixedAllocator<static_cast<int32>(EText3DMeshType::TypeCount)>>;


class FTextIndexBuffer : public FIndexBuffer
{
public:
	virtual void InitRHI() override
	{
		FRHIResourceCreateInfo CreateInfo;
		IndexBufferRHI = RHICreateIndexBuffer(sizeof(int32), uint32(NumIndices) * sizeof(int32), BUF_Dynamic, CreateInfo);
	}

	int32 NumIndices;
};

class FText3DSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FText3DSceneProxy(UText3DComponent* const Component)
		: FPrimitiveSceneProxy(Component)
	{
		const TText3DMeshList * ComponentMeshes = Component->Meshes.Get();
		for (int32 Index = 0; Index < static_cast<int32>(EText3DMeshType::TypeCount); Index++)
		{
			Meshes.Add(MakeUnique<FProxyMesh>(this, Index, (*ComponentMeshes)[Index], Component->GetMaterial(Index)));
		}
	}

	void UpdateData()
	{
		for (TUniquePtr<FProxyMesh>& Mesh : Meshes)
		{
			Mesh->UpdateData();
		}
	}

	/** Called on render thread to assign new dynamic data */
	void SetDynamicData_RenderThread(const TTextMeshDynamicData& NewDynamicData)
	{
		check(IsInRenderingThread());

		for (int32 Index = 0; Index < static_cast<int32>(EText3DMeshType::TypeCount); Index++)
		{
			Meshes[Index]->SetDynamicData_RenderThread(NewDynamicData[Index]);
		}
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		bool bEmpty = true;

		for (const TUniquePtr<FProxyMesh>& Mesh : Meshes)
		{
			bEmpty = bEmpty && Mesh->IsEmpty();
		}

		if (bEmpty)
		{
			return;
		}


		QUICK_SCOPE_CYCLE_COUNTER(STAT_TextSceneProxy_GetDynamicMeshElements);

		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

		FColoredMaterialRenderProxy* const WireframeMaterialInstance = new FColoredMaterialRenderProxy(
					GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
					FLinearColor(0, 0.5f, 1.f)
					);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				for (const TUniquePtr<FProxyMesh>& Mesh : Meshes)
				{
					Mesh->GetDynamicMeshElements(&Collector, bWireframe, WireframeMaterialInstance, this, ViewIndex);
				}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
#endif
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bDynamicRelevance = true;
		return Result;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }

	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }

private:
	struct FProxyMesh
	{
		const FText3DMesh& ComponentMesh;

		FLocalVertexFactory VertexFactory;
		int32 VertexCount;
		FStaticMeshVertexBuffers VertexBuffers;

		int32 IndicesCount;
		FTextIndexBuffer IndexBuffer;

		UMaterialInterface* Material;
		bool bInitialized;


		FProxyMesh(const FText3DSceneProxy* const Proxy, const int32 MeshType, const FText3DMesh& ComponentMeshIn, UMaterialInterface * const InMaterial)
			: ComponentMesh(ComponentMeshIn)

			, VertexFactory(Proxy->GetScene().GetFeatureLevel(), "FText3DSceneProxyMesh")
			, VertexCount(0)

			, IndicesCount(0)

			, Material(InMaterial)
		{

			if (Material == nullptr)
			{
				Material = UMaterial::GetDefaultMaterial(MD_Surface);
			}


			if (ComponentMesh.IsEmpty())
			{
				bInitialized = false;
				return;
			}

			VertexCount = ComponentMesh.Vertices.Num();
			VertexBuffers.InitWithDummyData(&VertexFactory, uint32(VertexCount));

			IndicesCount = ComponentMesh.Indices.Num();
			IndexBuffer.NumIndices = IndicesCount;

			BeginInitResource(&IndexBuffer);
			bInitialized = true;
		}

		~FProxyMesh()
		{
			IndexBuffer.ReleaseResource();

			VertexBuffers.PositionVertexBuffer.ReleaseResource();
			VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
			VertexBuffers.ColorVertexBuffer.ReleaseResource();
			VertexFactory.ReleaseResource();
		}

		bool IsEmpty() const
		{
			return VertexCount == 0 || IndicesCount == 0;
		}

		void UpdateData()
		{
			if (ComponentMesh.IsEmpty())
			{
				VertexCount = 0;
				IndicesCount = 0;

				bInitialized = false;
				return;
			}

			const int32 NewVertexCount = ComponentMesh.Vertices.Num();
			const int32 NewIndicesCount = ComponentMesh.Indices.Num();

			if (VertexCount != NewVertexCount || IndicesCount != NewIndicesCount)
			{
				VertexCount = NewVertexCount;
				VertexBuffers.InitWithDummyData(&VertexFactory, uint32(VertexCount));

				IndicesCount = NewIndicesCount;
				IndexBuffer.NumIndices = IndicesCount;

				if (bInitialized)
				{
					BeginUpdateResourceRHI(&IndexBuffer);
				}
				else
				{
					BeginInitResource(&IndexBuffer);
					bInitialized = true;
				}

				return;
			}

			bInitialized = false;
		}

		void SetDynamicData_RenderThread(const TUniquePtr<FText3DDynamicData>& DynamicData)
		{
			if (DynamicData->Vertices.Num() == 0 || DynamicData->Indices.Num() == 0)
			{
				return;
			}

			for (uint32 Index = 0; Index < uint32(DynamicData->Vertices.Num()); Index++)
			{
				const FDynamicMeshVertex& Vertex = DynamicData->Vertices[int32(Index)];

				VertexBuffers.PositionVertexBuffer.VertexPosition(Index) = Vertex.Position;
				VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(Index, Vertex.TangentX.ToFVector(), Vertex.GetTangentY(), Vertex.TangentZ.ToFVector());
				VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(Index, 0, Vertex.TextureCoordinate[0]);
				VertexBuffers.ColorVertexBuffer.VertexColor(Index) = Vertex.Color;
			}

			{
				FPositionVertexBuffer& VertexBuffer = VertexBuffers.PositionVertexBuffer;
				void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
				FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
				RHIUnlockVertexBuffer(VertexBuffer.VertexBufferRHI);
			}

			{
				FColorVertexBuffer& VertexBuffer = VertexBuffers.ColorVertexBuffer;
				void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
				FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
				RHIUnlockVertexBuffer(VertexBuffer.VertexBufferRHI);
			}

			{
				FStaticMeshVertexBuffer& VertexBuffer = VertexBuffers.StaticMeshVertexBuffer;
				void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI, 0, uint32(VertexBuffer.GetTangentSize()), RLM_WriteOnly);
				FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTangentData(), uint32(VertexBuffer.GetTangentSize()));
				RHIUnlockVertexBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI);
			}

			{
				FStaticMeshVertexBuffer& VertexBuffer = VertexBuffers.StaticMeshVertexBuffer;
				void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI, 0, uint32(VertexBuffer.GetTexCoordSize()), RLM_WriteOnly);
				FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTexCoordData(), uint32(VertexBuffer.GetTexCoordSize()));
				RHIUnlockVertexBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI);
			}

			void* IndexBufferData = RHILockIndexBuffer(IndexBuffer.IndexBufferRHI, 0, uint32(DynamicData->Indices.Num()) * sizeof(int32), RLM_WriteOnly);
			FMemory::Memcpy(IndexBufferData, &DynamicData->Indices[0], uint32(DynamicData->Indices.Num()) * sizeof(int32));
			RHIUnlockIndexBuffer(IndexBuffer.IndexBufferRHI);
		}

		void GetDynamicMeshElements(FMeshElementCollector* const Collector, const bool bWireframe, const FColoredMaterialRenderProxy* const WireframeMaterialInstance, const FPrimitiveSceneProxy* const Proxy, const int32 ViewIndex) const
		{
			if (IsEmpty())
			{
				return;
			}

			FMeshBatch& Mesh = Collector->AllocateMesh();
			FMeshBatchElement& BatchElement = Mesh.Elements[0];
			BatchElement.IndexBuffer = &IndexBuffer;
			Mesh.bWireframe = bWireframe;
			Mesh.VertexFactory = &VertexFactory;
			Mesh.MaterialRenderProxy = bWireframe ? WireframeMaterialInstance : Material->GetRenderProxy();

			bool bHasPrecomputedVolumetricLightmap = false;
			FMatrix PreviousLocalToWorld;
			int32 SingleCaptureIndex = 0;
			bool bOutputVelocity = false;

#if UE_VERSION_OLDER_THAN(4, 23, 0)
			Proxy->GetScene().GetPrimitiveUniformShaderParameters_RenderThread(Proxy->GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex);
			FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector->AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			DynamicPrimitiveUniformBuffer.Set(Proxy->GetLocalToWorld(), PreviousLocalToWorld, Proxy->GetBounds(), Proxy->GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, Proxy->UseEditorDepthTest());
#else
			Proxy->GetScene().GetPrimitiveUniformShaderParameters_RenderThread(Proxy->GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);
			FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector->AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			DynamicPrimitiveUniformBuffer.Set(Proxy->GetLocalToWorld(), PreviousLocalToWorld, Proxy->GetBounds(), Proxy->GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, Proxy->DrawsVelocity(), bOutputVelocity);
#endif
			BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

			BatchElement.FirstIndex = 0;
			BatchElement.NumPrimitives = uint32(IndicesCount) / 3;
			BatchElement.MinVertexIndex = 0;
			BatchElement.MaxVertexIndex = uint32(VertexCount);
			Mesh.ReverseCulling = Proxy->IsLocalToWorldDeterminantNegative();
			Mesh.Type = PT_TriangleList;
			Mesh.DepthPriorityGroup = SDPG_World;
			Mesh.bCanApplyViewModeOverrides = false;
			Collector->AddMesh(ViewIndex, Mesh);
		}
	};


	TArray<TUniquePtr<FProxyMesh>, TFixedAllocator<static_cast<int32>(EText3DMeshType::TypeCount)>> Meshes;
};

UText3DComponent::UText3DComponent()
{
	Meshes = MakeShared<TText3DMeshList>();
	Meshes->SetNum(static_cast<int32>(EText3DMeshType::TypeCount));

	if (!IsRunningDedicatedServer())
	{
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinder<UFont> Font;
			ConstructorHelpers::FObjectFinder<UMaterial> Material;
			FConstructorStatics()
				: Font(TEXT("/Engine/EngineFonts/Roboto"))
				, Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		Font = ConstructorStatics.Font.Object;
		UMaterial* DefaultMaterial = ConstructorStatics.Material.Object;
		FrontMaterial = DefaultMaterial;
		BevelMaterial = DefaultMaterial;
		ExtrudeMaterial = DefaultMaterial;
		BackMaterial = DefaultMaterial;
	}

	CastShadow = true;
	bUseAsOccluder = true;

	Text = LOCTEXT("DefaultText", "Text");
	HorizontalAlignment = EText3DHorizontalTextAlignment::Left;
	VerticalAlignment = EText3DVerticalTextAlignment::FirstLine;
	Extrude = 5.0f;
	Kerning = 0.0f;
	LineSpacing = 0.0f;
	WordSpacing = 0.0f;
	
	bHasMaxWidth = false;
	MaxWidth = 500.0;
	Bevel = 0.0f;
	BevelType = EText3DBevelType::HalfCircle;
	HalfCircleSegments = 4;

	bHasMaxHeight = false;
	MaxHeight = 500.0f;
	bScaleProportionally = true;

	bAutoActivate = true;
	PendingBuild = false;
	FreezeBuild = false;
}

bool UText3DComponent::ShouldRecreateProxyOnUpdateTransform() const
{
	return false;
}

FPrimitiveSceneProxy* UText3DComponent::CreateSceneProxy()
{
	return new FText3DSceneProxy(this);
}

void UText3DComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	int32 NumMaterials = static_cast<int32>(EText3DMeshType::TypeCount);
	for (int32 Index = 0; Index < NumMaterials; Index++)
	{
		UMaterialInterface* Material = GetMaterial(Index);
		if (Material != nullptr)
		{
			OutMaterials.Add(Material);
		}
	}
}

void UText3DComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* InMaterial)
{
	bool bChanged = false;
	switch (ElementIndex)
	{
	case static_cast<int32>(EText3DMeshType::Front):
	{
		if (FrontMaterial != InMaterial)
		{
			FrontMaterial = InMaterial;
			bChanged = true;
		}
		break;
	}

	case static_cast<int32>(EText3DMeshType::Bevel):
	{
		if (BevelMaterial != InMaterial)
		{
			BevelMaterial = InMaterial;
			bChanged = true;
		}
		break;
	}

	case static_cast<int32>(EText3DMeshType::Extrude):
	{
		if (ExtrudeMaterial != InMaterial)
		{
			ExtrudeMaterial = InMaterial;
			bChanged = true;
		}
		break;
	}

	case static_cast<int32>(EText3DMeshType::Back):
	{
		if (BackMaterial != InMaterial)
		{
			BackMaterial = InMaterial;
			bChanged = true;
		}
		break;
	}
	}

	if (bChanged && !FreezeBuild)
	{
		MarkRenderStateDirty();
	}
}

UMaterialInterface* UText3DComponent::GetMaterial(int32 ElementIndex) const
{
	switch (ElementIndex)
	{
	case static_cast<int32>(EText3DMeshType::Front):
	{
		return FrontMaterial;
	}

	case static_cast<int32>(EText3DMeshType::Bevel):
	{
		return BevelMaterial;
	}

	case static_cast<int32>(EText3DMeshType::Extrude):
	{
		return ExtrudeMaterial;
	}

	case static_cast<int32>(EText3DMeshType::Back):
	{
		return BackMaterial;
	}

	}

	return nullptr;
}

void UText3DComponent::SetText(const FText& Value)
{
	if (!Text.EqualTo(Value))
	{
		Text = Value;
		Rebuild();
	}
}

void UText3DComponent::SetKerning(const float Value)
{
	if (Kerning != Value)
	{
		Kerning = Value;
		Rebuild();
	}
}

void UText3DComponent::SetLineSpacing(const float Value)
{
	if (LineSpacing != Value)
	{
		LineSpacing = Value;
		Rebuild();
	}
}

void UText3DComponent::SetWordSpacing(const float Value)
{
	if (WordSpacing != Value)
	{
		WordSpacing = Value;
		Rebuild();
	}
}

void UText3DComponent::SetHorizontalAlignment(const EText3DHorizontalTextAlignment Value)
{
	if (HorizontalAlignment != Value)
	{
		HorizontalAlignment = Value;
		Rebuild();
	}
}

void UText3DComponent::SetVerticalAlignment(const EText3DVerticalTextAlignment Value)
{
	if (VerticalAlignment != Value)
	{
		VerticalAlignment = Value;
		Rebuild();
	}
}

void UText3DComponent::SetExtrude(const float Value)
{
	const float NewValue = FMath::Max(0.0f, Value);
	if (Extrude != NewValue)
	{
		Extrude = NewValue;
		CheckBevel();
		Rebuild();
	}
}

void UText3DComponent::SetFont(UFont* const InFont)
{
	if (Font != InFont)
	{
		Font = InFont;
		Rebuild();
	}
}

void UText3DComponent::SetHasMaxWidth(const bool Value)
{
	if (bHasMaxWidth != Value)
	{
		bHasMaxWidth = Value;
		Rebuild();
	}
}

void UText3DComponent::SetMaxWidth(const float Value)
{
	const float NewValue = FMath::Max(1.0f, Value);
	if (MaxWidth != NewValue)
	{
		MaxWidth = NewValue;
		Rebuild();
	}
}

void UText3DComponent::SetHasMaxHeight(const bool Value)
{
	if (bHasMaxHeight != Value)
	{
		bHasMaxHeight = Value;
		Rebuild();
	}
}

void UText3DComponent::SetMaxHeight(const float Value)
{
	const float NewValue = FMath::Max(1.0f, Value);
	if (MaxHeight != NewValue)
	{
		MaxHeight = NewValue;
		Rebuild();
	}
}

void UText3DComponent::SetScaleProportionally(const bool Value)
{
	if (bScaleProportionally != Value)
	{
		bScaleProportionally = Value;
		Rebuild();
	}
}

void UText3DComponent::SetBevel(const float Value)
{
	const float NewValue = FMath::Clamp(Value, 0.f, MaxBevel());

	if (Bevel != NewValue)
	{
		Bevel = NewValue;
		Rebuild();
	}
}

void UText3DComponent::SetBevelType(const EText3DBevelType Value)
{
	if (BevelType != Value)
	{
		BevelType = Value;
		Rebuild();
	}
}

void UText3DComponent::SetHalfCircleSegments(const int32 Value)
{
	if (BevelType != EText3DBevelType::HalfCircle)
	{
		return;
	}

	const int NewValue = FMath::Clamp(Value, 1, 10);

	if (HalfCircleSegments != NewValue)
	{
		HalfCircleSegments = NewValue;
		Rebuild();
	}
}

void UText3DComponent::SetFreeze(const bool bFreeze)
{
	FreezeBuild = bFreeze;
	if (bFreeze)
	{
		PendingBuild = false;
	}
	else if (PendingBuild)
	{
		Rebuild();
	}
}

void UText3DComponent::SetFrontMaterial(UMaterialInterface* Value)
{
	if (Value != FrontMaterial)
	{
		FrontMaterial = Value;
		if (!FreezeBuild)
		{
			MarkRenderStateDirty();
		}
	}
}

void UText3DComponent::SetBevelMaterial(UMaterialInterface* Value)
{
	if (Value != BevelMaterial)
	{
		BevelMaterial = Value;
		if (!FreezeBuild)
		{
			MarkRenderStateDirty();
		}
	}
}

void UText3DComponent::SetExtrudeMaterial(UMaterialInterface* Value)
{
	if (Value != ExtrudeMaterial)
	{
		ExtrudeMaterial = Value;
		if (!FreezeBuild)
		{
			MarkRenderStateDirty();
		}
	}
}

void UText3DComponent::SetBackMaterial(UMaterialInterface* Value)
{
	if (Value != BackMaterial)
	{
		BackMaterial = Value;
		if (!FreezeBuild)
		{
			MarkRenderStateDirty();
		}
	}
}

void UText3DComponent::Rebuild()
{
	PendingBuild = true;
	if (!FreezeBuild)
	{
		MarkRenderStateDirty();
	}
}

void UText3DComponent::BuildTextMesh()
{
	for (FText3DMesh& Mesh : *Meshes)
	{
		Mesh.Vertices.Reset();
		Mesh.Indices.Reset();
	}

	if (!Font)
	{
		return;
	}

	const FCompositeFont* const CompositeFont = Font->GetCompositeFont();
	if (!CompositeFont || CompositeFont->DefaultTypeface.Fonts.Num() == 0)
	{
		return;
	}

	const FTypefaceEntry& Typeface = CompositeFont->DefaultTypeface.Fonts[0];
	const FFontFaceDataConstPtr FaceData = Typeface.Font.GetFontFaceData();

	TArray<uint8> Data;
	if (FaceData->HasData())
	{
		Data = FaceData->GetData();
	}
	else if (!FFileHelper::LoadFileToArray(Data, *Typeface.Font.GetFontFilename()))
	{
		UE_LOG(LogText3D, Error, TEXT("Failed to load font file '%s'"), *Typeface.Font.GetFontFilename());
		return;
	}

	if (Data.Num() == 0)
	{
		FString FontName = Typeface.Name.ToString();
		UE_LOG(LogText3D, Error, TEXT("Failed to load font data '%s'"), *FontName);
		return;
	}

	FT_Face Face = nullptr;
	FT_New_Memory_Face(FText3DModule::GetFreeTypeLibrary(), Data.GetData(), Data.Num(), 0, &Face);
	if (!Face)
	{
		return;
	}


	FT_Set_Char_Size(Face, FontSize, FontSize, 96, 96);
	FT_Set_Pixel_Sizes(Face, FontSize, FontSize);

	TArray<FShapedGlyphLine> ShapedLines;
	FTextShaper::Get()->ShapeBidirectionalText(Face, Text.ToString(), ShapedLines);

	// Add extra kerning
	float TextMaxWidth = 0.0f;
	for (FShapedGlyphLine& ShapedLine : ShapedLines)
	{
		ShapedLine.AddKerning(Kerning, WordSpacing);
		TextMaxWidth = FMath::Max(TextMaxWidth, ShapedLine.Width);
	}

	FVector Scale(1.0f, 1.0f, 1.0f);
	if (bHasMaxWidth && TextMaxWidth > MaxWidth && TextMaxWidth > 0.0f)
	{
		Scale.Y *= MaxWidth / TextMaxWidth;
		if (bScaleProportionally)
		{
			Scale.Z = Scale.Y;
		}
	}

	float VerticalOffset = 0.0f;
	const float LineHeight = Face->size->metrics.height * FontInverseScale;
	const float TotalHeight = ShapedLines.Num() * LineHeight + (ShapedLines.Num() - 1) * LineSpacing;
	if (bHasMaxHeight && TotalHeight > MaxHeight && TotalHeight > 0.0f)
	{
		Scale.Z *= MaxHeight / TotalHeight;
		if (bScaleProportionally)
		{
			Scale.Y = Scale.Z;
		}
	}

	if (bScaleProportionally)
	{
		Scale.X = Scale.Y;
	}

	if (VerticalAlignment != EText3DVerticalTextAlignment::FirstLine)
	{
		VerticalOffset -= LineHeight;							// Align to Top
		if (VerticalAlignment == EText3DVerticalTextAlignment::Center)
		{
			VerticalOffset += TotalHeight * 0.5f;
		}
		else if (VerticalAlignment == EText3DVerticalTextAlignment::Bottom)
		{
			VerticalOffset += TotalHeight;
		}
	}

	for (FText3DMesh& Mesh : *Meshes)
	{
		Mesh.GlyphStartVertices.Empty();
	}

	const TSharedPtr<FData> MeshesData = MakeShared<FData>(Meshes, Bevel, FontInverseScale, Scale);
	MeshesData->SetVerticalOffset(VerticalOffset);

	for (const FShapedGlyphLine& ShapedLine : ShapedLines)
	{
		float HorizontalOffset = 0.0f;
		if (HorizontalAlignment == EText3DHorizontalTextAlignment::Center)
		{
			HorizontalOffset = -ShapedLine.Width * 0.5f;
		}
		else if (HorizontalAlignment == EText3DHorizontalTextAlignment::Right)
		{
			HorizontalOffset = -ShapedLine.Width;
		}

		MeshesData->SetHorizontalOffset(HorizontalOffset);

		for (const FShapedGlyphEntry& ShapedGlyph : ShapedLine.GlyphsToRender)
		{
			if (ShapedGlyph.bIsVisible)
			{
				MeshesData->SetCurrentMesh(EText3DMeshType::Front);
				MeshesData->ResetDoneExtrude();

				if (FT_Load_Glyph(Face, ShapedGlyph.GlyphIndex, FT_LOAD_DEFAULT))
				{
					continue;
				}

				FTVectoriser Vectoriser(Face->glyph);
				Vectoriser.MakeMesh(FTGL_BACK_FACING);
				const FTMesh* const Mesh = Vectoriser.GetMesh();
				const size_t TesselateCount = Mesh->TesselationCount();

				for (size_t i = 0; i < TesselateCount; i++)
				{
					const FTTesselation* const Tessselate = Mesh->Tesselation(i);
					const int32 PointCount = Tessselate->PointCount();

					if (PointCount < 3)
					{
						continue;
					}

					MeshesData->SetMinBevelTarget();
					const int32 StartVertex = MeshesData->AddVertices(PointCount);

					for (int32 PointIndex = 0; PointIndex < PointCount; PointIndex++)
					{
						const FTPoint* const Point = &Tessselate->Point(PointIndex);
						MeshesData->AddVertex({Point->Xf(), Point->Yf()}, {1, 0}, {0, 0, -1});
					}

					switch (Tessselate->PolygonType())
					{
					case GL_TRIANGLES:
					{
						const int32 TrianglesCount = PointCount / 3;
						MeshesData->AddTriangles(TrianglesCount);

						for (int32 Index = 0; Index < TrianglesCount; Index++)
						{
							const int32 TriangleStart = StartVertex + Index * 3;
							MeshesData->AddTriangle(TriangleStart + 0, TriangleStart + 2, TriangleStart + 1);
						}

						break;
					}
					case GL_TRIANGLE_STRIP:
					{
						MeshesData->AddTriangles(PointCount - 2);

						for (int32 Index = 0; Index < PointCount - 2; Index++)
						{
							const int32 TriangleStart = StartVertex + Index;
							const bool bIsOdd = (Index % 2) != 0;
							MeshesData->AddTriangle(TriangleStart + (bIsOdd ? 0 : 1), TriangleStart + (bIsOdd ? 1 : 0), TriangleStart + 2);
						}

						break;
					}
					case GL_TRIANGLE_FAN:
					{
						MeshesData->AddTriangles(PointCount - 2);

						for (int32 Index = 1; Index < PointCount - 1; Index++)
						{
							const int32 TriangleStart = StartVertex + Index;
							MeshesData->AddTriangle(StartVertex, TriangleStart + 1, TriangleStart + 0);
						}

						break;
					}

					default:
					{
						UE_LOG(LogText3D, Error, TEXT("Unknown polygon type: %X"), Tessselate->PolygonType());
						break;
					}
					}
				}

				if (Extrude > 0.0f)
				{
					FText3DDebug Debugger;
#if WITH_EDITOR
					Debugger = DebugVariables;
#endif

					BevelContours(MeshesData, Vectoriser, Extrude, Bevel, BevelType, HalfCircleSegments,
						Debugger.Iterations, Debugger.bHidePrevious, Debugger.MarkedVertex, Debugger.Segments, Debugger.VisibleFace);
				}
			}

			HorizontalOffset += ShapedGlyph.XAdvance;
			MeshesData->SetHorizontalOffset(HorizontalOffset);
		}

		VerticalOffset -= LineHeight + LineSpacing;
		MeshesData->SetVerticalOffset(VerticalOffset);
	}

	FT_Done_Face(Face);
	Face = nullptr;


	for (int32 Type = 0; Type < static_cast<int32>(EText3DMeshType::TypeCount); Type++)
	{
		MeshesData->SetCurrentMesh(static_cast<EText3DMeshType>(Type));
	}

	struct FBoundingBox
	{
		FBox2D Box;
		FVector2D Size;
	};

	TArray<FBoundingBox> BoundingBoxes;
	FVector2D MaxSize(0, 0);
	{
		EText3DMeshType MeshType = FMath::IsNearlyZero(Bevel) ? EText3DMeshType::Front : EText3DMeshType::Bevel;
		const FText3DMesh& Mesh = (*Meshes.Get())[static_cast<int32>(MeshType)];
		const TArray<int32>& GlyphStartVertices = Mesh.GlyphStartVertices;
		BoundingBoxes.AddUninitialized(GlyphStartVertices.Num() - 1);

		for (int32 GlyphIndex = 0; GlyphIndex < BoundingBoxes.Num(); GlyphIndex++)
		{
			FBoundingBox& BoundingBox = BoundingBoxes[GlyphIndex];
			FBox2D& Box = BoundingBox.Box;

			const int32 FirstIndex = GlyphStartVertices[GlyphIndex];
			const int32 LastIndex = GlyphStartVertices[GlyphIndex + 1];

			if (FirstIndex < LastIndex)
			{
				const FVector& Position = Mesh.Vertices[FirstIndex].Position;
				const FVector2D PositionFlat = {Position.Y, Position.Z};

				Box.Min = PositionFlat;
				Box.Max = PositionFlat;
			}

			for (int32 VertexIndex = FirstIndex + 1; VertexIndex < LastIndex; VertexIndex++)
			{
				const FDynamicMeshVertex& Vertex = Mesh.Vertices[VertexIndex];
				const FVector& Position = Vertex.Position;

				Box.Min.X = FMath::Min(Box.Min.X, Position.Y);
				Box.Min.Y = FMath::Min(Box.Min.Y, Position.Z);
				Box.Max.X = FMath::Max(Box.Max.X, Position.Y);
				Box.Max.Y = FMath::Max(Box.Max.Y, Position.Z);
			}

			FVector2D& Size = BoundingBox.Size;
			Size = Box.GetSize();

			MaxSize.X = FMath::Max(MaxSize.X, Size.X);
			MaxSize.Y = FMath::Max(MaxSize.Y, Size.Y);
		}
	}

	for (int32 GlyphIndex = 0; GlyphIndex < BoundingBoxes.Num(); GlyphIndex++)
	{
		const int32 NextGlyphIndex = GlyphIndex + 1;
		FBoundingBox& BoundingBox = BoundingBoxes[GlyphIndex];

		TSharedPtr<TText3DMeshList> MeshesLocal = Meshes;
		auto SetTextureCoordinates = [MeshesLocal, NextGlyphIndex, GlyphIndex, &BoundingBox, MaxSize](const EText3DMeshType Mesh)
		{
			FText3DMesh& CurrentMesh = (*MeshesLocal.Get())[static_cast<int32>(Mesh)];
			const TArray<int32>& GlyphStartVertices = CurrentMesh.GlyphStartVertices;

			if (NextGlyphIndex >= GlyphStartVertices.Num())
			{
				return;
			}

			for (int32 VertexIndex = GlyphStartVertices[GlyphIndex]; VertexIndex < GlyphStartVertices[NextGlyphIndex]; VertexIndex++)
			{
				FDynamicMeshVertex& Vertex = CurrentMesh.Vertices[VertexIndex];
				const FVector2D TextureCoordinate = (FVector2D(Vertex.Position.Y, Vertex.Position.Z) - BoundingBox.Box.Min) / MaxSize;

				for (int32 Index = 0; Index < MAX_STATIC_TEXCOORDS; Index++)
				{
					Vertex.TextureCoordinate[Index] = {TextureCoordinate.X, 1 - TextureCoordinate.Y};
				}
			}
		};

		SetTextureCoordinates(EText3DMeshType::Front);
		SetTextureCoordinates(EText3DMeshType::Bevel);
		SetTextureCoordinates(EText3DMeshType::Back);
	}


	// When TEXT3D_WITH_INTERSECTION=1 the following functions will not do anything
	MirrorMesh(EText3DMeshType::Bevel, EText3DMeshType::Bevel, Scale.X);
	MirrorMesh(EText3DMeshType::Front, EText3DMeshType::Back, Scale.X);
}

void UText3DComponent::CheckBevel()
{
	if (Bevel > MaxBevel())
	{
		Bevel = MaxBevel();
	}
}

float UText3DComponent::MaxBevel() const
{
#if TEXT3D_WITH_INTERSECTION
	return Extrude;
#else
	return Extrude / 2.0f;
#endif
}

void UText3DComponent::MirrorMesh(const EText3DMeshType TypeIn, const EText3DMeshType TypeOut, const float ScaleX)
{
#if !TEXT3D_WITH_INTERSECTION
	const FText3DMesh& MeshIn = (*Meshes.Get())[static_cast<int32>(TypeIn)];
	FText3DMesh& MeshOut = (*Meshes.Get())[static_cast<int32>(TypeOut)];


	const TArray<FDynamicMeshVertex>& VerticesIn = MeshIn.Vertices;
	TArray<FDynamicMeshVertex>& VerticesOut = MeshOut.Vertices;

	const int32 VerticesInNum = VerticesIn.Num();
	const int32 VerticesOutNum = VerticesOut.Num();

	VerticesOut.AddUninitialized(VerticesInNum);

	for (int32 Index = 0; Index < VerticesInNum; Index++)
	{
		const FDynamicMeshVertex& Vertex = VerticesIn[Index];

		const FVector& Position = Vertex.Position;
		const FVector TangentX = Vertex.TangentX.ToFVector();
		const FVector TangentZ = Vertex.TangentZ.ToFVector();

		VerticesOut[VerticesOutNum + Index] = FDynamicMeshVertex({Extrude * ScaleX - Position.X, Position.Y, Position.Z}, {-TangentX.X, TangentX.Y, TangentX.Z}, {-TangentZ.X, TangentZ.Y, TangentZ.Z}, Vertex.TextureCoordinate[0], Vertex.Color);
	}


	const TArray<int32>& IndicesIn = MeshIn.Indices;
	TArray<int32>& IndicesOut = MeshOut.Indices;

	const int32 IndicesInNum = IndicesIn.Num();
	const int32 IndicesOutNum = IndicesOut.Num();

	IndicesOut.AddUninitialized(IndicesInNum);

	for (int32 Index = 0; Index < IndicesInNum / 3; Index++)
	{
		const int32 OldTriangle = Index * 3;
		const int32 NewTriangle = IndicesOutNum + OldTriangle;

		IndicesOut[NewTriangle + 0] = VerticesOutNum + IndicesIn[OldTriangle + 0];
		IndicesOut[NewTriangle + 1] = VerticesOutNum + IndicesIn[OldTriangle + 2];
		IndicesOut[NewTriangle + 2] = VerticesOutNum + IndicesIn[OldTriangle + 1];
	}
#endif
}

void UText3DComponent::OnRegister()
{
	CheckBevel();
	BuildTextMesh();
	Super::OnRegister();

	if (!FreezeBuild)
	{
		MarkRenderStateDirty();
	}
}

void UText3DComponent::CreateRenderState_Concurrent()
{
	Super::CreateRenderState_Concurrent();

	SendRenderDynamicData_Concurrent();
}

void UText3DComponent::SendRenderDynamicData_Concurrent()
{
	FText3DSceneProxy* TextSceneProxy = static_cast<FText3DSceneProxy*>(SceneProxy);
	if (!TextSceneProxy)
	{
		return;
	}

	if (PendingBuild)
	{
		BuildTextMesh();
		UpdateBounds();
		TextSceneProxy->UpdateData();
		PendingBuild = false;
	}


	bool bAllEmpty = true;

	for (const FText3DMesh& Mesh : *Meshes)
	{
		bAllEmpty = bAllEmpty && Mesh.IsEmpty();
	}

	if (bAllEmpty)
	{
		return;
	}

	TTextMeshDynamicData DynamicData;

	for (FText3DMesh& Mesh : *Meshes)
	{
		DynamicData.Emplace(MakeUnique<FText3DDynamicData>(Mesh.Indices, Mesh.Vertices));
	}

	// Enqueue command to send to render thread
	ENQUEUE_RENDER_COMMAND(FSendText3DDynamicData)(
		[TextSceneProxy, d{ MoveTemp(DynamicData) }](FRHICommandListImmediate& RHICmdList)
		{
			TextSceneProxy->SetDynamicData_RenderThread(d);
		});
}

FBoxSphereBounds UText3DComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox BBox(ForceInit);

	for (const FText3DMesh& Mesh : *Meshes)
	{
		for (const FDynamicMeshVertex& Vertex : Mesh.Vertices)
		{
			BBox += Vertex.Position;
		}
	}

	return FBoxSphereBounds(BBox).TransformBy(LocalToWorld);
}

#undef LOCTEXT_NAMESPACE
