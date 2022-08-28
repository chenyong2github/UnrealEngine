// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFAccessorConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFSkinWeightVertexBufferHack.h"
#include "Builders/GLTFConvertBuilder.h"

namespace
{
    struct FGLTFNormalConverter
	{
		template <typename PackedType>
		static auto Convert(TStaticMeshVertexTangentDatum<PackedType> TangentVectors)
		{
			return FGLTFConverterUtility::ConvertNormal(FVector(TangentVectors.GetTangentZ()));
		}
	};

	struct FGLTFPackedNormalConverter
	{
		template <typename PackedType>
        static auto Convert(TStaticMeshVertexTangentDatum<PackedType> TangentVectors)
		{
			return FGLTFConverterUtility::ConvertNormal(TangentVectors.TangentZ);
		}
	};

	struct FGLTFTangentConverter
	{
		template <typename PackedType>
        static auto Convert(TStaticMeshVertexTangentDatum<PackedType> TangentVectors)
		{
			return FGLTFConverterUtility::ConvertTangent(TangentVectors.GetTangentX());
		}
	};

    struct FGLTFPackedTangentConverter
    {
    	template <typename PackedType>
        static auto Convert(TStaticMeshVertexTangentDatum<PackedType> TangentVectors)
    	{
    		return FGLTFConverterUtility::ConvertTangent(TangentVectors.TangentX);
    	}
	};

	template <typename VectorType, typename PackedType, typename ConverterType>
    FGLTFJsonBufferViewIndex AddTangentDataBufferView(FGLTFConvertBuilder& Builder, const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer)
	{
		const TArray<uint32>& IndexMap = MeshSection->IndexMap;
		const uint32 VertexCount = IndexMap.Num();

		typedef TStaticMeshVertexTangentDatum<PackedType> TangentType;
		const void* TangentData = const_cast<FStaticMeshVertexBuffer*>(VertexBuffer)->GetTangentData();
		const TangentType* TangentVectors = static_cast<const TangentType*>(TangentData);

		TArray<VectorType> Vectors;
		Vectors.AddUninitialized(VertexCount);

		for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
		{
			const uint32 MappedVertexIndex = IndexMap[VertexIndex];
			Vectors[VertexIndex] = ConverterType::Convert(TangentVectors[MappedVertexIndex]);
		}

		return Builder.AddBufferView(Vectors);
	}
}

FGLTFJsonAccessorIndex FGLTFPositionBufferConverter::Convert(const FGLTFMeshSection* MeshSection, const FPositionVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr || VertexBuffer->GetNumVertices() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	TArray<FGLTFJsonVector3> Positions;
	Positions.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const uint32 MappedVertexIndex = IndexMap[VertexIndex];
		Positions[VertexIndex] = FGLTFConverterUtility::ConvertPosition(VertexBuffer->VertexPosition(MappedVertexIndex), Builder.ExportOptions->ExportScale);
	}

	// More accurate bounding box if based on raw vertex values
	FGLTFJsonVector3 MinPosition = Positions[0];
	FGLTFJsonVector3 MaxPosition = Positions[0];

	for (uint32 VertexIndex = 1; VertexIndex < VertexCount; ++VertexIndex)
	{
		const FGLTFJsonVector3& Position = Positions[VertexIndex];
		MinPosition.X = FMath::Min(MinPosition.X, Position.X);
		MinPosition.Y = FMath::Min(MinPosition.Y, Position.Y);
		MinPosition.Z = FMath::Min(MinPosition.Z, Position.Z);
		MaxPosition.X = FMath::Max(MaxPosition.X, Position.X);
		MaxPosition.Y = FMath::Max(MaxPosition.Y, Position.Y);
		MaxPosition.Z = FMath::Max(MaxPosition.Z, Position.Z);
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(Positions, EGLTFJsonBufferTarget::ArrayBuffer);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec3;

	JsonAccessor.MinMaxLength = 3;
	JsonAccessor.Min[0] = MinPosition.X;
	JsonAccessor.Min[1] = MinPosition.Y;
	JsonAccessor.Min[2] = MinPosition.Z;
	JsonAccessor.Max[0] = MaxPosition.X;
	JsonAccessor.Max[1] = MaxPosition.Y;
	JsonAccessor.Max[2] = MaxPosition.Z;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFColorBufferConverter::Convert(const FGLTFMeshSection* MeshSection, const FColorVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr || VertexBuffer->GetNumVertices() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	TArray<FGLTFPackedColor> Colors;
	Colors.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const uint32 MappedVertexIndex = IndexMap[VertexIndex];
		Colors[VertexIndex] = FGLTFConverterUtility::ConvertColor(VertexBuffer->VertexColor(MappedVertexIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(Colors, EGLTFJsonBufferTarget::ArrayBuffer);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::U8;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;
	JsonAccessor.bNormalized = true;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFNormalBufferConverter::Convert(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr || VertexBuffer->GetNumVertices() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	FGLTFJsonBufferViewIndex BufferViewIndex;
	EGLTFJsonComponentType ComponentType;

	const bool bQuantization = Builder.ExportOptions->bUseMeshQuantization;
	const bool bHighPrecision = VertexBuffer->GetUseHighPrecisionTangentBasis();

	if (bQuantization)
	{
		Builder.AddExtension(EGLTFJsonExtension::KHR_MeshQuantization);

		if (bHighPrecision)
		{
			ComponentType = EGLTFJsonComponentType::S16;
			BufferViewIndex = AddTangentDataBufferView<FGLTFPackedVector16, FPackedRGBA16N, FGLTFPackedNormalConverter>(Builder, MeshSection, VertexBuffer);
		}
		else
		{
			ComponentType = EGLTFJsonComponentType::S8;
			BufferViewIndex = AddTangentDataBufferView<FGLTFPackedVector8, FPackedNormal, FGLTFPackedNormalConverter>(Builder, MeshSection, VertexBuffer);
			Builder.GetBufferView(BufferViewIndex).ByteStride = 4;
		}
	}
	else
	{
		ComponentType = EGLTFJsonComponentType::F32;
		BufferViewIndex = bHighPrecision
            ? AddTangentDataBufferView<FGLTFJsonVector3, FPackedRGBA16N, FGLTFNormalConverter>(Builder, MeshSection, VertexBuffer)
            : AddTangentDataBufferView<FGLTFJsonVector3, FPackedNormal, FGLTFNormalConverter>(Builder, MeshSection, VertexBuffer);
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = BufferViewIndex;
	JsonAccessor.ComponentType = ComponentType;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec3;
	JsonAccessor.bNormalized = bQuantization;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFTangentBufferConverter::Convert(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr || VertexBuffer->GetNumVertices() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	FGLTFJsonBufferViewIndex BufferViewIndex;
	EGLTFJsonComponentType ComponentType;

	const bool bQuantization = Builder.ExportOptions->bUseMeshQuantization;
	const bool bHighPrecision = VertexBuffer->GetUseHighPrecisionTangentBasis();

	if (bQuantization)
	{
		Builder.AddExtension(EGLTFJsonExtension::KHR_MeshQuantization);

		if (bHighPrecision)
		{
			ComponentType = EGLTFJsonComponentType::S16;
			BufferViewIndex = AddTangentDataBufferView<FGLTFPackedVector16, FPackedRGBA16N, FGLTFPackedTangentConverter>(Builder, MeshSection, VertexBuffer);
		}
		else
		{
			ComponentType = EGLTFJsonComponentType::S8;
			BufferViewIndex = AddTangentDataBufferView<FGLTFPackedVector8, FPackedNormal, FGLTFPackedTangentConverter>(Builder, MeshSection, VertexBuffer);
		}
	}
	else
	{
		ComponentType = EGLTFJsonComponentType::F32;
		BufferViewIndex = bHighPrecision
            ? AddTangentDataBufferView<FGLTFJsonVector4, FPackedRGBA16N, FGLTFTangentConverter>(Builder, MeshSection, VertexBuffer)
            : AddTangentDataBufferView<FGLTFJsonVector4, FPackedNormal, FGLTFTangentConverter>(Builder, MeshSection, VertexBuffer);
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = BufferViewIndex;
	JsonAccessor.ComponentType = ComponentType;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;
	JsonAccessor.bNormalized = bQuantization;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFUVBufferConverter::Convert(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer, uint32 UVIndex)
{
	if (VertexBuffer == nullptr || VertexBuffer->GetNumVertices() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	const uint32 UVCount = VertexBuffer->GetNumTexCoords();
	if (UVIndex >= UVCount)
	{
		// TODO: report warning
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FGLTFJsonVector2> UVs;
	UVs.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const uint32 MappedVertexIndex = IndexMap[VertexIndex];
		UVs[VertexIndex] = FGLTFConverterUtility::ConvertUV(VertexBuffer->GetVertexUV(MappedVertexIndex, UVIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(UVs, EGLTFJsonBufferTarget::ArrayBuffer);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec2;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFBoneIndexBufferConverter::Convert(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, uint32 InfluenceOffset)
{
	return MeshSection->MaxBoneIndex <= UINT8_MAX
		? Convert<uint8>(MeshSection, VertexBuffer, InfluenceOffset)
		: Convert<uint16>(MeshSection, VertexBuffer, InfluenceOffset);
}

template <typename IndexType>
FGLTFJsonAccessorIndex FGLTFBoneIndexBufferConverter::Convert(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, uint32 InfluenceOffset) const
{
	if (VertexBuffer == nullptr || VertexBuffer->GetNumVertices() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const uint32 InfluenceCount = VertexBuffer->GetMaxBoneInfluences();
	if (InfluenceOffset >= InfluenceCount)
	{
		// TODO: report warning
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	struct VertexBoneIndices
	{
		IndexType Index[4];
	};

	TArray<VertexBoneIndices> BoneIndices;
	BoneIndices.AddUninitialized(VertexCount);

	const TArray<FBoneIndexType>& BoneMap = MeshSection->BoneMap;

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const uint32 MappedVertexIndex = IndexMap[VertexIndex];
		VertexBoneIndices& VertexBones = BoneIndices[VertexIndex];

		for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
		{
			// TODO: remove hack
			const uint32 UnmappedBoneIndex = FGLTFSkinWeightVertexBufferHack(VertexBuffer).GetBoneIndex(MappedVertexIndex, InfluenceOffset + InfluenceIndex);
			VertexBones.Index[InfluenceIndex] = static_cast<IndexType>(BoneMap[UnmappedBoneIndex]);
		}
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(BoneIndices, EGLTFJsonBufferTarget::ArrayBuffer);
	JsonAccessor.ComponentType = FGLTFConverterUtility::GetComponentType<IndexType>();
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFBoneWeightBufferConverter::Convert(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, uint32 InfluenceOffset)
{
	if (VertexBuffer == nullptr || VertexBuffer->GetNumVertices() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const uint32 InfluenceCount = VertexBuffer->GetMaxBoneInfluences();
	if (InfluenceOffset >= InfluenceCount)
	{
		// TODO: report warning
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	struct VertexBoneWeights
	{
		uint8 Weights[4];
	};

	TArray<VertexBoneWeights> BoneWeights;
	BoneWeights.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const uint32 MappedVertexIndex = IndexMap[VertexIndex];
		VertexBoneWeights& VertexWeights = BoneWeights[VertexIndex];

		for (uint32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
		{
			// TODO: remove hack
			const uint8 BoneWeight = FGLTFSkinWeightVertexBufferHack(VertexBuffer).GetBoneWeight(MappedVertexIndex, InfluenceOffset + InfluenceIndex);
			VertexWeights.Weights[InfluenceIndex] = BoneWeight;
		}
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(BoneWeights, EGLTFJsonBufferTarget::ArrayBuffer);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::U8;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;
	JsonAccessor.bNormalized = true;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFIndexBufferConverter::Convert(const FGLTFMeshSection* MeshSection)
{
	const uint32 MaxVertexIndex = MeshSection->IndexMap.Num() - 1;
	if (MaxVertexIndex <= UINT8_MAX) return Convert<uint8>(MeshSection);
	if (MaxVertexIndex <= UINT16_MAX) return Convert<uint16>(MeshSection);
	return Convert<uint32>(MeshSection);
}

template <typename IndexType>
FGLTFJsonAccessorIndex FGLTFIndexBufferConverter::Convert(const FGLTFMeshSection* MeshSection) const
{
	const TArray<uint32>& IndexBuffer = MeshSection->IndexBuffer;
	const uint32 IndexCount = IndexBuffer.Num();
	if (IndexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<IndexType> Indices;
	Indices.AddUninitialized(IndexCount);

	for (uint32 Index = 0; Index < IndexCount; ++Index)
	{
		Indices[Index] = static_cast<IndexType>(IndexBuffer[Index]);
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(Indices, EGLTFJsonBufferTarget::ElementArrayBuffer, sizeof(IndexType));
	JsonAccessor.ComponentType = FGLTFConverterUtility::GetComponentType<IndexType>();
	JsonAccessor.Count = IndexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Scalar;

	return Builder.AddAccessor(JsonAccessor);
}
