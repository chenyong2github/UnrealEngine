// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFAccessorConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFSkinWeightVertexBufferHack.h"
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

	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	TArray<FGLTFRawVector3> Positions;
	Positions.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const uint32 MappedVertexIndex = IndexMap[VertexIndex];
		Positions[VertexIndex] = FGLTFConverterUtility::ConvertPosition(VertexBuffer->VertexPosition(MappedVertexIndex), Builder.ExportOptions->ExportUniformScale);
	}

	// More accurate bounding box if based on raw vertex values
	FGLTFRawVector3 MinPosition = VertexCount > 0 ? Positions[0] : FGLTFRawVector3(0, 0, 0); // TODO: make static const
	FGLTFRawVector3 MaxPosition = VertexCount > 0 ? Positions[0] : FGLTFRawVector3(0, 0, 0); // TODO: make static const

	for (uint32 VertexIndex = 1; VertexIndex < VertexCount; ++VertexIndex)
	{
		const FGLTFRawVector3& Position = Positions[VertexIndex];
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
			BufferViewIndex = ConvertBufferView<FGLTFPackedVector16, FPackedRGBA16N>(MeshSection, VertexBuffer);
			Builder.GetBufferView(BufferViewIndex).ByteStride = sizeof(FGLTFPackedVector16);
		}
		else
		{
			ComponentType = EGLTFJsonComponentType::S8;
			BufferViewIndex = ConvertBufferView<FGLTFPackedVector8, FPackedNormal>(MeshSection, VertexBuffer);
			Builder.GetBufferView(BufferViewIndex).ByteStride = sizeof(FGLTFPackedVector8);
		}
	}
	else
	{
		ComponentType = EGLTFJsonComponentType::F32;
		BufferViewIndex = bHighPrecision
			? ConvertBufferView<FGLTFRawVector3, FPackedRGBA16N>(MeshSection, VertexBuffer)
			: ConvertBufferView<FGLTFRawVector3, FPackedNormal>(MeshSection, VertexBuffer);
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
FGLTFJsonBufferViewIndex FGLTFNormalBufferConverter::ConvertBufferView(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer)
{
	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	const void* TangentData = const_cast<FStaticMeshVertexBuffer*>(VertexBuffer)->GetTangentData();
	if (TangentData == nullptr)
	{
		// TODO: report error
		return FGLTFJsonBufferViewIndex(INDEX_NONE);
	}

	typedef TStaticMeshVertexTangentDatum<SourceType> VertexTangentType;
	const VertexTangentType* VertexTangents = static_cast<const VertexTangentType*>(TangentData);

	TArray<DestinationType> Normals;
	Normals.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const uint32 MappedVertexIndex = IndexMap[VertexIndex];
		const FVector SafeNormal = VertexTangents[MappedVertexIndex].TangentZ.ToFVector().GetSafeNormal();

		typedef typename TConditional<TIsSame<DestinationType, FGLTFRawVector3>::Value, FVector, SourceType>::Type IntermediateType;
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
			BufferViewIndex = ConvertBufferView<FGLTFPackedVector16, FPackedRGBA16N>(MeshSection, VertexBuffer);
		}
		else
		{
			ComponentType = EGLTFJsonComponentType::S8;
			BufferViewIndex = ConvertBufferView<FGLTFPackedVector8, FPackedNormal>(MeshSection, VertexBuffer);
		}
	}
	else
	{
		ComponentType = EGLTFJsonComponentType::F32;
		BufferViewIndex = bHighPrecision
			? ConvertBufferView<FGLTFRawVector4, FPackedRGBA16N>(MeshSection, VertexBuffer)
			: ConvertBufferView<FGLTFRawVector4, FPackedNormal>(MeshSection, VertexBuffer);
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
FGLTFJsonBufferViewIndex FGLTFTangentBufferConverter::ConvertBufferView(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer)
{
	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	const void* TangentData = const_cast<FStaticMeshVertexBuffer*>(VertexBuffer)->GetTangentData();
	if (TangentData == nullptr)
	{
		// TODO: report error
		return FGLTFJsonBufferViewIndex(INDEX_NONE);
	}

	typedef TStaticMeshVertexTangentDatum<SourceType> VertexTangentType;
	const VertexTangentType* VertexTangents = static_cast<const VertexTangentType*>(TangentData);

	TArray<DestinationType> Tangents;
	Tangents.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const uint32 MappedVertexIndex = IndexMap[VertexIndex];
		const FVector SafeTangent = VertexTangents[MappedVertexIndex].TangentX.ToFVector().GetSafeNormal();

		typedef typename TConditional<TIsSame<DestinationType, FGLTFRawVector4>::Value, FVector, SourceType>::Type IntermediateType;
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

	const TArray<uint32>& IndexMap = MeshSection->IndexMap;
	const uint32 VertexCount = IndexMap.Num();

	const uint32 UVCount = VertexBuffer->GetNumTexCoords();
	if (UVIndex >= UVCount)
	{
		// TODO: report warning
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	// TODO: report warning or add support for half float precision UVs, i.e. !VertexBuffer->GetUseFullPrecisionUVs()?

	TArray<FGLTFRawVector2> UVs;
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

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const TArray<FBoneIndexType>& BoneMap = MeshSection->BoneMaps[MeshSection->BoneMapLookup[VertexIndex]];
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
