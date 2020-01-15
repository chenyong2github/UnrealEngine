// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace HairAttribute
{
	namespace Vertex
	{
		extern HAIRSTRANDSCORE_API const FName Color;			// FVector
		extern HAIRSTRANDSCORE_API const FName Position;		// FVector
		extern HAIRSTRANDSCORE_API const FName Width;			// float
	}

	namespace Strand
	{
		extern HAIRSTRANDSCORE_API const FName Color;			// FVector
		extern HAIRSTRANDSCORE_API const FName GroupID;			// int
		extern HAIRSTRANDSCORE_API const FName Guide;			// bool
		extern HAIRSTRANDSCORE_API const FName ID;				// int
		extern HAIRSTRANDSCORE_API const FName RootUV;			// FVector2D
		extern HAIRSTRANDSCORE_API const FName VertexCount;		// int
		extern HAIRSTRANDSCORE_API const FName Width;			// float
	}

	namespace Groom
	{
		extern HAIRSTRANDSCORE_API const FName Color;			// FVector
		extern HAIRSTRANDSCORE_API const FName MajorVersion;	// int
		extern HAIRSTRANDSCORE_API const FName MinorVersion;	// int
		extern HAIRSTRANDSCORE_API const FName Tool;			// FName
		extern HAIRSTRANDSCORE_API const FName Properties;		// FName
		extern HAIRSTRANDSCORE_API const FName Width;			// float
	}
}
