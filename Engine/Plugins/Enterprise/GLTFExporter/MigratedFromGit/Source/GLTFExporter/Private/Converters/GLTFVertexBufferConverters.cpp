// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFVertexBufferConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Builders/GLTFConvertBuilder.h"

FGLTFJsonAccessorIndex FGLTFPositionVertexBufferConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FPositionVertexBuffer* VertexBuffer)
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
		Positions[VertexIndex] = FGLTFConverterUtility::ConvertPosition(VertexBuffer->VertexPosition(VertexIndex));
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
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(Positions, Name);
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

FGLTFJsonAccessorIndex FGLTFColorVertexBufferConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FColorVertexBuffer* VertexBuffer)
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
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(Colors, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::U8;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;
	JsonAccessor.bNormalized = true;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFNormalVertexBufferConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer)
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
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(Normals, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec3;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFTangentVertexBufferConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer)
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
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(Tangents, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFUVVertexBufferConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex)
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
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(UVs, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec2;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFBoneIndexVertexBufferConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FSkinWeightVertexBuffer* VertexBuffer)
{
	return FGLTFJsonAccessorIndex(INDEX_NONE);
}

FGLTFJsonAccessorIndex FGLTFBoneWeightVertexBufferConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FSkinWeightVertexBuffer* VertexBuffer)
{
	return FGLTFJsonAccessorIndex(INDEX_NONE);
}
