// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFAccessorConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Builders/GLTFConvertBuilder.h"

// TODO: Unreal-style implementation of std::conditional to avoid mixing in STL. Should be added to the engine.
template <bool Condition, class TypeIfTrue, class TypeIfFalse>
class TConditional
{
public:
	typedef TypeIfFalse Type;
};

template <class TypeIfTrue, class TypeIfFalse>
class TConditional<true, TypeIfTrue, TypeIfFalse>
{
public:
	typedef TypeIfTrue Type;
};

FGLTFJsonAccessorIndex FGLTFPositionBufferConverter::Convert(const FGLTFMeshSection* MeshSection, const FPositionVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr || VertexBuffer->GetNumVertices() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	if (VertexBuffer->GetVertexData() == nullptr)
	{
		// TODO: report error
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	TArray<FGLTFVector3> Positions;
	Positions.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const uint32 MappedVertexIndex = IndexMap[VertexIndex];
		Positions[VertexIndex] = FGLTFConverterUtility::ConvertPosition(VertexBuffer->VertexPosition(MappedVertexIndex), Builder.ExportOptions->ExportUniformScale);
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(Positions, EGLTFJsonBufferTarget::ArrayBuffer);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec3;

	if (VertexCount > 0)
	{
		// Calculate accurate bounding box based on raw vertex values
		JsonAccessor.MinMaxLength = 3;

		for (int32 ComponentIndex = 0; ComponentIndex < JsonAccessor.MinMaxLength; ComponentIndex++)
		{
			JsonAccessor.Min[ComponentIndex] = Positions[0].Components[ComponentIndex];
			JsonAccessor.Max[ComponentIndex] = Positions[0].Components[ComponentIndex];
		}

		for (uint32 VertexIndex = 1; VertexIndex < VertexCount; ++VertexIndex)
		{
			const FGLTFVector3& Position = Positions[VertexIndex];
			for (int32 ComponentIndex = 0; ComponentIndex < JsonAccessor.MinMaxLength; ComponentIndex++)
			{
				JsonAccessor.Min[ComponentIndex] = FMath::Min(JsonAccessor.Min[ComponentIndex], Position.Components[ComponentIndex]);
				JsonAccessor.Max[ComponentIndex] = FMath::Max(JsonAccessor.Max[ComponentIndex], Position.Components[ComponentIndex]);
			}
		}
	}

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFColorBufferConverter::Convert(const FGLTFMeshSection* MeshSection, const FColorVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr || VertexBuffer->GetNumVertices() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	if (VertexBuffer->GetVertexData() == nullptr)
	{
		// TODO: report error
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	TArray<FGLTFUInt8Color4> Colors;
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

	const void* TangentData = const_cast<FStaticMeshVertexBuffer*>(VertexBuffer)->GetTangentData();
	if (TangentData == nullptr)
	{
		// TODO: report error
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	FGLTFJsonBufferViewIndex BufferViewIndex;
	EGLTFJsonComponentType ComponentType;

	const bool bMeshQuantization = Builder.ExportOptions->bUseMeshQuantization;
	const bool bHighPrecision = VertexBuffer->GetUseHighPrecisionTangentBasis();

	if (bMeshQuantization)
	{
		Builder.AddExtension(EGLTFJsonExtension::KHR_MeshQuantization, true);

		if (bHighPrecision)
		{
			ComponentType = EGLTFJsonComponentType::S16;
			BufferViewIndex = ConvertBufferView<FGLTFInt16Vector4, FPackedRGBA16N>(MeshSection, TangentData);
			Builder.GetBufferView(BufferViewIndex).ByteStride = sizeof(FGLTFInt16Vector4);
		}
		else
		{
			ComponentType = EGLTFJsonComponentType::S8;
			BufferViewIndex = ConvertBufferView<FGLTFInt8Vector4, FPackedNormal>(MeshSection, TangentData);
			Builder.GetBufferView(BufferViewIndex).ByteStride = sizeof(FGLTFInt8Vector4);
		}
	}
	else
	{
		ComponentType = EGLTFJsonComponentType::F32;
		BufferViewIndex = bHighPrecision
			? ConvertBufferView<FGLTFVector3, FPackedRGBA16N>(MeshSection, TangentData)
			: ConvertBufferView<FGLTFVector3, FPackedNormal>(MeshSection, TangentData);
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = BufferViewIndex;
	JsonAccessor.ComponentType = ComponentType;
	JsonAccessor.Count = MeshSection->IndexMap.Num();
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec3;
	JsonAccessor.bNormalized = bMeshQuantization;

	return Builder.AddAccessor(JsonAccessor);
}

template <typename DestinationType, typename SourceType>
FGLTFJsonBufferViewIndex FGLTFNormalBufferConverter::ConvertBufferView(const FGLTFMeshSection* MeshSection, const void* TangentData)
{
	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	typedef TStaticMeshVertexTangentDatum<SourceType> VertexTangentType;
	const VertexTangentType* VertexTangents = static_cast<const VertexTangentType*>(TangentData);

	TArray<DestinationType> Normals;
	Normals.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const uint32 MappedVertexIndex = IndexMap[VertexIndex];
		const FVector SafeNormal = VertexTangents[MappedVertexIndex].TangentZ.ToFVector().GetSafeNormal();

		typedef typename TConditional<TIsSame<DestinationType, FGLTFVector3>::Value, FVector, SourceType>::Type IntermediateType;
		Normals[VertexIndex] = FGLTFConverterUtility::ConvertNormal(IntermediateType(SafeNormal));
	}

	return Builder.AddBufferView(Normals, EGLTFJsonBufferTarget::ArrayBuffer);
}

FGLTFJsonAccessorIndex FGLTFTangentBufferConverter::Convert(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr || VertexBuffer->GetNumVertices() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const void* TangentData = const_cast<FStaticMeshVertexBuffer*>(VertexBuffer)->GetTangentData();
	if (TangentData == nullptr)
	{
		// TODO: report error
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	FGLTFJsonBufferViewIndex BufferViewIndex;
	EGLTFJsonComponentType ComponentType;

	const bool bMeshQuantization = Builder.ExportOptions->bUseMeshQuantization;
	const bool bHighPrecision = VertexBuffer->GetUseHighPrecisionTangentBasis();

	if (bMeshQuantization)
	{
		Builder.AddExtension(EGLTFJsonExtension::KHR_MeshQuantization, true);

		if (bHighPrecision)
		{
			ComponentType = EGLTFJsonComponentType::S16;
			BufferViewIndex = ConvertBufferView<FGLTFInt16Vector4, FPackedRGBA16N>(MeshSection, TangentData);
		}
		else
		{
			ComponentType = EGLTFJsonComponentType::S8;
			BufferViewIndex = ConvertBufferView<FGLTFInt8Vector4, FPackedNormal>(MeshSection, TangentData);
		}
	}
	else
	{
		ComponentType = EGLTFJsonComponentType::F32;
		BufferViewIndex = bHighPrecision
			? ConvertBufferView<FGLTFVector4, FPackedRGBA16N>(MeshSection, TangentData)
			: ConvertBufferView<FGLTFVector4, FPackedNormal>(MeshSection, TangentData);
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = BufferViewIndex;
	JsonAccessor.ComponentType = ComponentType;
	JsonAccessor.Count = MeshSection->IndexMap.Num();
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;
	JsonAccessor.bNormalized = bMeshQuantization;

	return Builder.AddAccessor(JsonAccessor);
}

template <typename DestinationType, typename SourceType>
FGLTFJsonBufferViewIndex FGLTFTangentBufferConverter::ConvertBufferView(const FGLTFMeshSection* MeshSection, const void* TangentData)
{
	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	typedef TStaticMeshVertexTangentDatum<SourceType> VertexTangentType;
	const VertexTangentType* VertexTangents = static_cast<const VertexTangentType*>(TangentData);

	TArray<DestinationType> Tangents;
	Tangents.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const uint32 MappedVertexIndex = IndexMap[VertexIndex];
		const FVector SafeTangent = VertexTangents[MappedVertexIndex].TangentX.ToFVector().GetSafeNormal();

		typedef typename TConditional<TIsSame<DestinationType, FGLTFVector4>::Value, FVector, SourceType>::Type IntermediateType;
		Tangents[VertexIndex] = FGLTFConverterUtility::ConvertTangent(IntermediateType(SafeTangent));
	}

	return Builder.AddBufferView(Tangents, EGLTFJsonBufferTarget::ArrayBuffer);
}

FGLTFJsonAccessorIndex FGLTFUVBufferConverter::Convert(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer, uint32 UVIndex)
{
	if (VertexBuffer == nullptr || VertexBuffer->GetNumVertices() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	if (const_cast<FStaticMeshVertexBuffer*>(VertexBuffer)->GetTexCoordData() == nullptr)
	{
		// TODO: report error
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

	// TODO: report warning or add support for half float precision UVs, i.e. !VertexBuffer->GetUseFullPrecisionUVs()?

	TArray<FGLTFVector2> UVs;
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

	if (VertexBuffer->GetDataVertexBuffer()->GetWeightData() == nullptr)
	{
		// TODO: report error
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

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const TArray<FBoneIndexType>& BoneMap = MeshSection->BoneMaps[MeshSection->BoneMapLookup[VertexIndex]];
		const uint32 MappedVertexIndex = IndexMap[VertexIndex];
		VertexBoneIndices& VertexBones = BoneIndices[VertexIndex];

		for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
		{
			const uint32 UnmappedBoneIndex = VertexBuffer->GetBoneIndex(MappedVertexIndex, InfluenceOffset + InfluenceIndex);
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

	if (VertexBuffer->GetDataVertexBuffer()->GetWeightData() == nullptr)
	{
		// TODO: report error
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
			const uint8 BoneWeight = VertexBuffer->GetBoneWeight(MappedVertexIndex, InfluenceOffset + InfluenceIndex);
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
