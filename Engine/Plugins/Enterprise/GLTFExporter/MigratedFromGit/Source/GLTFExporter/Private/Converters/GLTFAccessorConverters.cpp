// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFAccessorConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFSkinWeightVertexBufferHack.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Math/NumericLimits.h"
#include "Rendering/MultiSizeIndexContainer.h"
#include "Rendering/SkeletalMeshRenderData.h"

FGLTFJsonAccessorIndex FGLTFPositionBufferConverter::Convert(const FPositionVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FGLTFJsonVector3> Positions;
	Positions.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		Positions[VertexIndex] = FGLTFConverterUtility::ConvertPosition(VertexBuffer->VertexPosition(VertexIndex), Builder.ExportOptions->ExportScale);
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
	JsonAccessor.BufferView = Builder.AddBufferView(Positions);
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

FGLTFJsonAccessorIndex FGLTFColorBufferConverter::Convert(const FColorVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FGLTFPackedColor> Colors;
	Colors.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		Colors[VertexIndex] = FGLTFConverterUtility::ConvertColor(VertexBuffer->VertexColor(VertexIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(Colors);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::U8;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;
	JsonAccessor.bNormalized = true;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFNormalBufferConverter::Convert(const FStaticMeshVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FGLTFJsonVector3> Normals;
	Normals.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		Normals[VertexIndex] = FGLTFConverterUtility::ConvertNormal(VertexBuffer->VertexTangentZ(VertexIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(Normals);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec3;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFTangentBufferConverter::Convert(const FStaticMeshVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FGLTFJsonVector4> Tangents;
	Tangents.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		Tangents[VertexIndex] = FGLTFConverterUtility::ConvertTangent(VertexBuffer->VertexTangentX(VertexIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(Tangents);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFUVBufferConverter::Convert(const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0 || UVIndex < 0 || VertexBuffer->GetNumTexCoords() <= static_cast<uint32>(UVIndex))
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FGLTFJsonVector2> UVs;
	UVs.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		UVs[VertexIndex] = FGLTFConverterUtility::ConvertUV(VertexBuffer->GetVertexUV(VertexIndex, UVIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(UVs);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec2;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFBoneIndexBufferConverter::Convert(const FSkinWeightVertexBuffer* VertexBuffer, int32 InfluenceOffset, FGLTFBoneMap BoneMap)
{
	return VertexBuffer->Use16BitBoneIndex() ? Convert<uint16>(VertexBuffer, InfluenceOffset, BoneMap) : Convert<uint8>(VertexBuffer, InfluenceOffset, BoneMap);
}

template <typename IndexType>
FGLTFJsonAccessorIndex FGLTFBoneIndexBufferConverter::Convert(const FSkinWeightVertexBuffer* VertexBuffer, int32 JointsGroupIndex, FGLTFBoneMap BoneMap)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	const int32 MaxInfluenceCount = VertexBuffer->GetMaxBoneInfluences();
	const int32 InfluenceOffset = JointsGroupIndex * 4;

	if (VertexCount == 0 || InfluenceOffset < 0 || MaxInfluenceCount <= InfluenceOffset)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	struct VertexBoneIndices
	{
		IndexType Index[4];
	};

	TArray<VertexBoneIndices> BoneIndices;
	BoneIndices.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
		{
			// TODO: remove hack
			const uint32 UnmappedBoneIndex = FGLTFSkinWeightVertexBufferHack(VertexBuffer).GetBoneIndex(VertexIndex, InfluenceOffset + InfluenceIndex);
			const FBoneIndexType BoneIndex = BoneMap[UnmappedBoneIndex];
			check(BoneIndex <= TNumericLimits<IndexType>::Max());

			BoneIndices[VertexIndex].Index[InfluenceIndex] = static_cast<IndexType>(BoneIndex);
		}
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(BoneIndices);
	JsonAccessor.ComponentType = FGLTFConverterUtility::GetComponentType<IndexType>();
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFBoneWeightBufferConverter::Convert(const FSkinWeightVertexBuffer* VertexBuffer, int32 WeightsGroupIndex)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	const int32 MaxInfluenceCount = VertexBuffer->GetMaxBoneInfluences();
	const int32 InfluenceOffset = WeightsGroupIndex * 4;

	if (VertexCount == 0 || InfluenceOffset < 0 || MaxInfluenceCount <= InfluenceOffset)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	struct VertexBoneWeights
	{
		uint8 Weights[4];
	};

	TArray<VertexBoneWeights> BoneWeights;
	BoneWeights.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
		{
			// TODO: remove hack
			const uint8 BoneWeight = FGLTFSkinWeightVertexBufferHack(VertexBuffer).GetBoneWeight(VertexIndex, InfluenceOffset + InfluenceIndex);
			BoneWeights[VertexIndex].Weights[InfluenceIndex] = BoneWeight;
		}
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(BoneWeights);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::U8;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;
	JsonAccessor.bNormalized = true;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFStaticMeshSectionConverter::Convert(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer)
{
	const uint32 IndexCount = MeshSection->NumTriangles * 3;
	if (IndexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const bool bIs32Bit = IndexBuffer->Is32Bit();
	const uint8 IndexTypeSize = bIs32Bit ? sizeof(uint32) : sizeof(uint16);
	const uint32 FirstIndex = MeshSection->FirstIndex;

	const void* IndexDataRootPtr = bIs32Bit ? static_cast<const void*>(IndexBuffer->AccessStream32()) : static_cast<const void*>(IndexBuffer->AccessStream16());
	const void* IndexDataPtr = static_cast<const uint8*>(IndexDataRootPtr) + FirstIndex * IndexTypeSize;
	const int32 IndexDataSize = IndexCount * IndexTypeSize;

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(IndexDataPtr, IndexDataSize, IndexTypeSize, EGLTFJsonBufferTarget::ElementArrayBuffer);
	JsonAccessor.ComponentType = bIs32Bit ? EGLTFJsonComponentType::U32 : EGLTFJsonComponentType::U16;
	JsonAccessor.Count = IndexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Scalar;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFSkeletalMeshSectionConverter::Convert(const FSkelMeshRenderSection* MeshSection, const FRawStaticIndexBuffer16or32Interface* IndexBuffer)
{
	const uint32 IndexCount = MeshSection->NumTriangles * 3;
	if (IndexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const bool bIs32Bit = IndexBuffer->GetResourceDataSize() == IndexBuffer->Num() * sizeof(uint32);
	const uint8 IndexTypeSize = bIs32Bit ? sizeof(uint32) : sizeof(uint16);
	const uint32 FirstIndex = MeshSection->BaseIndex;

	const void* IndexDataPtr = const_cast<FRawStaticIndexBuffer16or32Interface*>(IndexBuffer)->GetPointerTo(FirstIndex);
	const int32 IndexDataSize = IndexCount * IndexTypeSize;

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.BufferView = Builder.AddBufferView(IndexDataPtr, IndexDataSize, IndexTypeSize, EGLTFJsonBufferTarget::ElementArrayBuffer);
	JsonAccessor.ComponentType = bIs32Bit ? EGLTFJsonComponentType::U32 : EGLTFJsonComponentType::U16;
	JsonAccessor.Count = IndexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Scalar;

	return Builder.AddAccessor(JsonAccessor);
}
