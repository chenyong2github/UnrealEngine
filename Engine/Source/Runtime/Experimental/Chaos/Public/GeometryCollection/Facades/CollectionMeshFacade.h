// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/ManagedArrayAccessor.h"
#include "GeometryCollection/ManagedArrayCollection.h"


namespace GeometryCollection::Facades
{

	class CHAOS_API FMeshFacade
	{
	public:
		FMeshFacade(FManagedArrayCollection& InCollection);

		/**
		 * returns true if all the necessary attributes are present
		 * if not then the API can be used to create
		 */
		bool IsValid() const;

		/**
		 * Add the necessary attributes if they are missing
		 */
		void DefineSchema();

		TManagedArrayAccessor<FVector3f> Vertex;
		TManagedArrayAccessor<FVector3f> TangentU;
		TManagedArrayAccessor<FVector3f> TangentV;
		TManagedArrayAccessor<FVector3f> Normal;
		TManagedArrayAccessor<TArray<FVector2f>> UVs;
		TManagedArrayAccessor<FLinearColor> Color;
		TManagedArrayAccessor<int32> BoneMap;
		TManagedArrayAccessor<int32> VertexStart;
		TManagedArrayAccessor<int32> VertexCount;

		TManagedArrayAccessor<FIntVector> Indices;
		TManagedArrayAccessor<bool> Visible;
		TManagedArrayAccessor<int32> MaterialIndex;
		TManagedArrayAccessor<int32> MaterialID;
		TManagedArrayAccessor<int32> FaceStart;
		TManagedArrayAccessor<int32> FaceCount;
	};

}