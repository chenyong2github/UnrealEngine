// Copyright Epic Games, Inc. All Rights Reserved.

#include "Json/GLTFJsonMesh.h"

void FGLTFJsonAttributes::WriteObject(IGLTFJsonWriter& Writer) const
{
	if (Position != INDEX_NONE) Writer.Write(TEXT("POSITION"), Position);
	if (Color0 != INDEX_NONE) Writer.Write(TEXT("COLOR_0"), Color0);
	if (Normal != INDEX_NONE) Writer.Write(TEXT("NORMAL"), Normal);
	if (Tangent != INDEX_NONE) Writer.Write(TEXT("TANGENT"), Tangent);

	for (int32 Index = 0; Index < TexCoords.Num(); ++Index)
	{
		const FGLTFJsonAccessorIndex TexCoord = TexCoords[Index];
		if (TexCoord != INDEX_NONE) Writer.Write(TEXT("TEXCOORD_") + FString::FromInt(Index), TexCoord);
	}

	for (int32 Index = 0; Index < Joints.Num(); ++Index)
	{
		const FGLTFJsonAccessorIndex Joint = Joints[Index];
		if (Joint != INDEX_NONE) Writer.Write(TEXT("JOINTS_") + FString::FromInt(Index), Joint);
	}

	for (int32 Index = 0; Index < Weights.Num(); ++Index)
	{
		const FGLTFJsonAccessorIndex Weight = Weights[Index];
		if (Weight != INDEX_NONE) Writer.Write(TEXT("WEIGHTS_") + FString::FromInt(Index), Weight);
	}
}

void FGLTFJsonPrimitive::WriteObject(IGLTFJsonWriter& Writer) const
{
	Writer.Write(TEXT("attributes"), Attributes);

	if (Indices != INDEX_NONE)
	{
		Writer.Write(TEXT("indices"), Indices);
	}

	if (Material != INDEX_NONE)
	{
		Writer.Write(TEXT("material"), Material);
	}

	if (Mode != EGLTFJsonPrimitiveMode::Triangles)
	{
		Writer.Write(TEXT("mode"), Mode);
	}

	if (KhrMaterialVariantMappings.Num() > 0)
	{
		Writer.StartExtensions();

		Writer.StartExtension(EGLTFJsonExtension::KHR_MaterialsVariants);
		Writer.Write(TEXT("mappings"), KhrMaterialVariantMappings);
		Writer.EndExtension();

		Writer.EndExtensions();
	}
}

void FGLTFJsonMesh::WriteObject(IGLTFJsonWriter& Writer) const
{
	if (!Name.IsEmpty())
	{
		Writer.Write(TEXT("name"), Name);
	}

	Writer.Write(TEXT("primitives"), Primitives);
}
