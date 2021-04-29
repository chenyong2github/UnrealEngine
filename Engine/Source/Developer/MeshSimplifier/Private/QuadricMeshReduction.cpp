// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "MeshUtilities.h"
#include "MeshBuild.h"
#include "MeshSimplify.h"
#include "SimpVert.h"
#include "OverlappingCorners.h"
#include "Templates/UniquePtr.h"
#include "Features/IModularFeatures.h"
#include "IMeshReductionInterfaces.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "RenderUtils.h"
#include "Engine/StaticMesh.h"

class FQuadricSimplifierMeshReductionModule : public IMeshReductionModule
{
public:
	virtual ~FQuadricSimplifierMeshReductionModule() {}

	// IModuleInterface interface.
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// IMeshReductionModule interface.
	virtual class IMeshReduction* GetStaticMeshReductionInterface() override;
	virtual class IMeshReduction* GetSkeletalMeshReductionInterface() override;
	virtual class IMeshMerging* GetMeshMergingInterface() override;
	virtual class IMeshMerging* GetDistributedMeshMergingInterface() override;	
	virtual FString GetName() override;
};


DEFINE_LOG_CATEGORY_STATIC(LogQuadricSimplifier, Log, All);
IMPLEMENT_MODULE(FQuadricSimplifierMeshReductionModule, QuadricMeshReduction);

void CorrectAttributes( float* Attributes )
{
	FVector& Normal		= *reinterpret_cast< FVector* >( Attributes );
	FVector& TangentX	= *reinterpret_cast< FVector* >( Attributes + 3 );
	FVector& TangentY	= *reinterpret_cast< FVector* >( Attributes + 3 + 3 );
	FLinearColor& Color	= *reinterpret_cast< FLinearColor* >( Attributes + 3 + 3 + 3 );

	Normal.Normalize();
	TangentX -= ( TangentX | Normal ) * Normal;
	TangentX.Normalize();
	TangentY -= ( TangentY | Normal ) * Normal;
	TangentY -= ( TangentY | TangentX ) * TangentX;
	TangentY.Normalize();
	Color = Color.GetClamped();
}

class FQuadricSimplifierMeshReduction : public IMeshReduction
{
public:
	virtual const FString& GetVersionString() const override
	{
		// Correct layout selection depends on the name "QuadricMeshReduction_{foo}"
		// e.g.
		// TArray<FString> SplitVersionString;
		// VersionString.ParseIntoArray(SplitVersionString, TEXT("_"), true);
		// bool bUseQuadricSimplier = SplitVersionString[0].Equals("QuadricMeshReduction");

		static FString Version = TEXT("QuadricMeshReduction_V2.0");
		return Version;
	}

	virtual void ReduceMeshDescription(
		FMeshDescription& OutReducedMesh,
		float& OutMaxDeviation,
		const FMeshDescription& InMesh,
		const FOverlappingCorners& InOverlappingCorners,
		const struct FMeshReductionSettings& ReductionSettings
	) override
	{
		check(&InMesh != &OutReducedMesh);	// can't reduce in-place
		TRACE_CPUPROFILER_EVENT_SCOPE(FQuadricSimplifierMeshReduction::ReduceMeshDescription);
		const uint32 NumTexCoords = MAX_STATIC_TEXCOORDS;
		int32 InMeshNumTexCoords = 1;
		
		TMap<FVertexID, FVertexID> VertexIDRemap;

		bool bWeldVertices = ReductionSettings.WeldingThreshold > 0.0f;
		if (bWeldVertices)
		{
			FStaticMeshOperations::BuildWeldedVertexIDRemap(InMesh, ReductionSettings.WeldingThreshold, VertexIDRemap);
		}

		TArray< TVertSimp< NumTexCoords > >	Verts;
		TArray< uint32 >					Indexes;
		TArray< int32 >						MaterialIndexes;

		TMap< int32, int32 > VertsMap;

		int32 NumFaces = InMesh.Triangles().Num();
		int32 NumWedges = NumFaces * 3;
		const FStaticMeshConstAttributes InMeshAttribute(InMesh);
		TVertexAttributesConstRef<FVector> InVertexPositions = InMeshAttribute.GetVertexPositions();
		TVertexInstanceAttributesConstRef<FVector> InVertexNormals = InMeshAttribute.GetVertexInstanceNormals();
		TVertexInstanceAttributesConstRef<FVector> InVertexTangents = InMeshAttribute.GetVertexInstanceTangents();
		TVertexInstanceAttributesConstRef<float> InVertexBinormalSigns = InMeshAttribute.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesConstRef<FVector4> InVertexColors = InMeshAttribute.GetVertexInstanceColors();
		TVertexInstanceAttributesConstRef<FVector2D> InVertexUVs = InMeshAttribute.GetVertexInstanceUVs();
		TPolygonGroupAttributesConstRef<FName> InPolygonGroupMaterialNames = InMeshAttribute.GetPolygonGroupMaterialSlotNames();

		TPolygonGroupAttributesRef<FName> OutPolygonGroupMaterialNames = OutReducedMesh.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);

		float SurfaceArea = 0.0f;

		int32 WedgeIndex = 0;
		for (const FTriangleID TriangleID : InMesh.Triangles().GetElementIDs())
		{
			const FPolygonGroupID PolygonGroupID = InMesh.GetTrianglePolygonGroup(TriangleID);
			TArrayView<const FVertexID> VertexIDs = InMesh.GetTriangleVertices(TriangleID);

			FVector CornerPositions[3];
			for (int32 TriVert = 0; TriVert < 3; ++TriVert)
			{
				const FVertexID TmpVertexID = VertexIDs[TriVert];
				const FVertexID VertexID = bWeldVertices ? VertexIDRemap[TmpVertexID] : TmpVertexID;
				CornerPositions[TriVert] = InVertexPositions[VertexID];
			}

			// Don't process degenerate triangles.
			if( PointsEqual(CornerPositions[0], CornerPositions[1]) ||
				PointsEqual(CornerPositions[0], CornerPositions[2]) ||
				PointsEqual(CornerPositions[1], CornerPositions[2]) )
			{
				WedgeIndex += 3;
				continue;
			}

			int32 VertexIndices[3];
			for (int32 TriVert = 0; TriVert < 3; ++TriVert, ++WedgeIndex)
			{
				const FVertexInstanceID VertexInstanceID = InMesh.GetTriangleVertexInstance(TriangleID, TriVert);
				const int32 VertexInstanceValue = VertexInstanceID.GetValue();
				const FVector& VertexPosition = CornerPositions[TriVert];

				TVertSimp< NumTexCoords > NewVert;

				NewVert.Position = CornerPositions[TriVert];
				NewVert.Tangents[0] = InVertexTangents[ VertexInstanceID ];
				NewVert.Normal = InVertexNormals[ VertexInstanceID ];
				NewVert.Tangents[1] = FVector(0.0f);
				if (!NewVert.Normal.IsNearlyZero(SMALL_NUMBER) && !NewVert.Tangents[0].IsNearlyZero(SMALL_NUMBER))
				{
					NewVert.Tangents[1] = FVector::CrossProduct(NewVert.Normal, NewVert.Tangents[0]).GetSafeNormal() * InVertexBinormalSigns[ VertexInstanceID ];
				}

				// Fix bad tangents
				NewVert.Tangents[0] = NewVert.Tangents[0].ContainsNaN() ? FVector::ZeroVector : NewVert.Tangents[0];
				NewVert.Tangents[1] = NewVert.Tangents[1].ContainsNaN() ? FVector::ZeroVector : NewVert.Tangents[1];
				NewVert.Normal = NewVert.Normal.ContainsNaN() ? FVector::ZeroVector : NewVert.Normal;
				NewVert.Color = FLinearColor(InVertexColors[ VertexInstanceID ]);

				for (int32 UVIndex = 0; UVIndex < NumTexCoords; UVIndex++)
				{
					if (UVIndex < InVertexUVs.GetNumChannels())
					{
						NewVert.TexCoords[UVIndex] = InVertexUVs.Get(VertexInstanceID, UVIndex);
						InMeshNumTexCoords = FMath::Max(UVIndex + 1, InMeshNumTexCoords);
					}
					else
					{
						NewVert.TexCoords[UVIndex] = FVector2D::ZeroVector;
					}
				}

				// Make sure this vertex is valid from the start
				NewVert.Correct();
					

				//Never add duplicated vertex instance
				//Use WedgeIndex since OverlappingCorners has been built based on that
				const TArray<int32>& DupVerts = InOverlappingCorners.FindIfOverlapping(WedgeIndex);

				int32 Index = INDEX_NONE;
				for (int32 k = 0; k < DupVerts.Num(); k++)
				{
					if (DupVerts[k] >= WedgeIndex)
					{
						// the verts beyond me haven't been placed yet, so these duplicates are not relevant
						break;
					}

					int32* Location = VertsMap.Find(DupVerts[k]);
					if (Location)
					{
						TVertSimp< NumTexCoords >& FoundVert = Verts[*Location];

						if (NewVert.Equals(FoundVert))
						{
							Index = *Location;
							break;
						}
					}
				}
				if (Index == INDEX_NONE)
				{
					Index = Verts.Add(NewVert);
					VertsMap.Add(WedgeIndex, Index);
				}
				VertexIndices[TriVert] = Index;
			}
				
			// Reject degenerate triangles.
			if (VertexIndices[0] == VertexIndices[1] ||
				VertexIndices[1] == VertexIndices[2] ||
				VertexIndices[0] == VertexIndices[2])
			{
				continue;
			}

			{
				FVector Edge01 = CornerPositions[1] - CornerPositions[0];
				FVector Edge12 = CornerPositions[2] - CornerPositions[1];
				FVector Edge20 = CornerPositions[0] - CornerPositions[2];

				float TriArea = 0.5f * ( Edge01 ^ Edge20 ).Size();
				SurfaceArea += TriArea;
			}

			Indexes.Add(VertexIndices[0]);
			Indexes.Add(VertexIndices[1]);
			Indexes.Add(VertexIndices[2]);

			MaterialIndexes.Add( PolygonGroupID.GetValue() );
		}

		uint32 NumVerts = Verts.Num();
		uint32 NumIndexes = Indexes.Num();
		uint32 NumTris = NumIndexes / 3;

#if 0
		static_assert(NumTexCoords == 8, "NumTexCoords changed, fix AttributeWeights");
		const uint32 NumAttributes = (sizeof(TVertSimp< NumTexCoords >) - sizeof(FVector)) / sizeof(float);
		float AttributeWeights[] =
		{
			16.0f, 16.0f, 16.0f,	// Normal
			0.1f, 0.1f, 0.1f,		// Tangent[0]
			0.1f, 0.1f, 0.1f,		// Tangent[1]
			0.1f, 0.1f, 0.1f, 0.1f,	// Color
			0.5f, 0.5f,				// TexCoord[0]
			0.5f, 0.5f,				// TexCoord[1]
			0.5f, 0.5f,				// TexCoord[2]
			0.5f, 0.5f,				// TexCoord[3]
			0.5f, 0.5f,				// TexCoord[4]
			0.5f, 0.5f,				// TexCoord[5]
			0.5f, 0.5f,				// TexCoord[6]
			0.5f, 0.5f,				// TexCoord[7]
		};
		float* ColorWeights = AttributeWeights + 3 + 3 + 3;
		float* TexCoordWeights = ColorWeights + 4;

		// Re-scale the weights for UV channels that exceed the expected 0-1 range.
		// Otherwise garbage on the UVs will dominate the simplification quadric.
		{
			float XLength[MAX_STATIC_TEXCOORDS] = { 0 };
			float YLength[MAX_STATIC_TEXCOORDS] = { 0 };
			{
				for (int32 TexCoordId = 0; TexCoordId < NumTexCoords; ++TexCoordId)
				{
					float XMax = -FLT_MAX;
					float YMax = -FLT_MAX;
					float XMin = FLT_MAX;
					float YMin = FLT_MAX;
					for (const TVertSimp< NumTexCoords >& SimpVert : Verts)
					{
						const FVector2D& UVs = SimpVert.TexCoords[TexCoordId];
						XMax = FMath::Max(XMax, UVs.X);
						XMin = FMath::Min(XMin, UVs.X);

						YMax = FMath::Max(YMax, UVs.Y);
						YMin = FMath::Min(YMin, UVs.Y);
					}

					XLength[TexCoordId] =  ( XMax > XMin ) ? XMax - XMin : 0.f;
					YLength[TexCoordId] =  ( YMax > YMin ) ? YMax - YMin : 0.f;
				}
			}

			for (int32 TexCoordId = 0; TexCoordId < NumTexCoords; ++TexCoordId)
			{

				if (XLength[TexCoordId] > 1.f)
				{
					TexCoordWeights[2 * TexCoordId + 0] /= XLength[TexCoordId];
				}
				if (YLength[TexCoordId] > 1.f)
				{
					TexCoordWeights[2 * TexCoordId + 1] /= YLength[TexCoordId];
				}
			}
		}

		// Zero out weights that aren't used
		{
			//TODO Check if we have vertex color

			for (int32 TexCoordIndex = 0; TexCoordIndex < NumTexCoords; TexCoordIndex++)
			{
				if (TexCoordIndex >= InVertexUVs.GetNumChannels())
				{
					TexCoordWeights[2 * TexCoordIndex + 0] = 0.0f;
					TexCoordWeights[2 * TexCoordIndex + 1] = 0.0f;
				}
			}
		}

		TMeshSimplifier< TVertSimp< NumTexCoords >, NumAttributes >* MeshSimp = new TMeshSimplifier< TVertSimp< NumTexCoords >, NumAttributes >(Verts.GetData(), NumVerts, Indexes.GetData(), NumIndexes);

		MeshSimp->SetAttributeWeights(AttributeWeights);
		MeshSimp->SetEdgeWeight( 256.0f );
		//MeshSimp->SetBoundaryLocked();
		MeshSimp->InitCosts();

		//We need a minimum of 2 triangles, to see the object on both side. If we use one, we will end up with zero triangle when we will remove a shared edge
		int32 AbsoluteMinTris = 2;
		int32 TargetNumTriangles = (ReductionSettings.TerminationCriterion != EStaticMeshReductionTerimationCriterion::Vertices) ? FMath::Max(AbsoluteMinTris, FMath::CeilToInt(NumTris * ReductionSettings.PercentTriangles)) : AbsoluteMinTris;
		int32 TargetNumVertices = (ReductionSettings.TerminationCriterion != EStaticMeshReductionTerimationCriterion::Triangles) ? FMath::CeilToInt(NumVerts * ReductionSettings.PercentVertices) : 0;
		
		float MaxErrorSqr = MeshSimp->SimplifyMesh(MAX_FLT, TargetNumTriangles, TargetNumVertices);

		MeshSimp->OutputMesh(Verts.GetData(), Indexes.GetData(), &NumVerts, &NumIndexes);
		MeshSimp->CompactFaceData( MaterialIndexes );
		NumTris = NumIndexes / 3;
		delete MeshSimp;

		OutMaxDeviation = FMath::Sqrt(MaxErrorSqr) / 8.0f;
#else
		uint32 TargetNumTris = FMath::CeilToInt( NumTris * ReductionSettings.PercentTriangles );
		uint32 TargetNumVerts = FMath::CeilToInt( NumVerts * ReductionSettings.PercentVertices );

		// We need a minimum of 2 triangles, to see the object on both side. If we use one, we will end up with zero triangle when we will remove a shared edge
		TargetNumTris = FMath::Max( TargetNumTris, 2u );

		if( TargetNumVerts < NumVerts || TargetNumTris < NumTris )
		{
			using VertType = TVertSimp< NumTexCoords >;

			float TriangleSize = FMath::Sqrt( SurfaceArea / NumTris );
	
			FFloat32 CurrentSize( FMath::Max( TriangleSize, THRESH_POINTS_ARE_SAME ) );
			FFloat32 DesiredSize( 0.25f );
			FFloat32 Scale( 1.0f );

			// Lossless scaling by only changing the float exponent.
			int32 Exponent = FMath::Clamp( (int)DesiredSize.Components.Exponent - (int)CurrentSize.Components.Exponent, -126, 127 );
			Scale.Components.Exponent = Exponent + 127;	//ExpBias
			float PositionScale = Scale.FloatValue;


			const uint32 NumAttributes = ( sizeof( VertType ) - sizeof( FVector ) ) / sizeof(float);
			float AttributeWeights[ NumAttributes ] =
			{
				1.0f, 1.0f, 1.0f,		// Normal
				0.006f, 0.006f, 0.006f,	// Tangent[0]
				0.006f, 0.006f, 0.006f	// Tangent[1]
			};
			float* ColorWeights = AttributeWeights + 9;
			float* UVWeights = ColorWeights + 4;

			bool bHasColors = true;

			// Set weights if they are used
			if( bHasColors )
			{
				ColorWeights[0] = 0.0625f;
				ColorWeights[1] = 0.0625f;
				ColorWeights[2] = 0.0625f;
				ColorWeights[3] = 0.0625f;
			}

			float UVWeight = 1.0f / ( 32.0f * InVertexUVs.GetNumChannels() );
			for( int32 UVIndex = 0; UVIndex < InVertexUVs.GetNumChannels(); UVIndex++ )
			{
				// Normalize UVWeights using min/max UV range.

				float MinUV = +FLT_MAX;
				float MaxUV = -FLT_MAX;

				for( int32 VertexIndex = 0; VertexIndex < Verts.Num(); VertexIndex++ )
				{
					MinUV = FMath::Min( MinUV, Verts[ VertexIndex ].TexCoords[ UVIndex ].X );
					MinUV = FMath::Min( MinUV, Verts[ VertexIndex ].TexCoords[ UVIndex ].Y );
					MaxUV = FMath::Max( MaxUV, Verts[ VertexIndex ].TexCoords[ UVIndex ].X );
					MaxUV = FMath::Max( MaxUV, Verts[ VertexIndex ].TexCoords[ UVIndex ].Y );
				}

				UVWeights[ 2 * UVIndex + 0 ] = UVWeight / FMath::Max( 1.0f, MaxUV - MinUV );
				UVWeights[ 2 * UVIndex + 1 ] = UVWeight / FMath::Max( 1.0f, MaxUV - MinUV );
			}

			for( auto& Vert : Verts )
			{
				Vert.Position *= PositionScale;
			}

			FMeshSimplifier Simplifier( (float*)Verts.GetData(), Verts.Num(), Indexes.GetData(), Indexes.Num(), MaterialIndexes.GetData(), NumAttributes );

			Simplifier.SetAttributeWeights( AttributeWeights );
			Simplifier.SetCorrectAttributes( CorrectAttributes );
			Simplifier.SetEdgeWeight( 4.0f );

			float MaxErrorSqr = Simplifier.Simplify( TargetNumVerts, TargetNumTris );

			if( Simplifier.GetRemainingNumVerts() == 0 || Simplifier.GetRemainingNumTris() == 0 )
			{
				// Reduced to nothing so just return the orignial.
				OutReducedMesh = InMesh;
				OutMaxDeviation = 0.0f;
				return;
			}
		
			Simplifier.Compact();
	
			Verts.SetNum( Simplifier.GetRemainingNumVerts() );
			Indexes.SetNum( Simplifier.GetRemainingNumTris() * 3 );
			MaterialIndexes.SetNum( Simplifier.GetRemainingNumTris() );

			NumVerts = Simplifier.GetRemainingNumVerts();
			NumTris = Simplifier.GetRemainingNumTris();
			NumIndexes = NumTris * 3;

			float InvScale = 1.0f / PositionScale;
			for( auto& Vert : Verts )
			{
				Vert.Position *= InvScale;
			}

			OutMaxDeviation = FMath::Sqrt( MaxErrorSqr ) * InvScale;
		}
		else
		{
			// Rare but could happen with rounding or only 2 triangles.
			OutMaxDeviation = 0.0f;
		}
#endif

		{
			//Empty the destination mesh
			OutReducedMesh.Empty();

			//Fill the PolygonGroups from the InMesh
			for (const FPolygonGroupID PolygonGroupID : InMesh.PolygonGroups().GetElementIDs())
			{
				OutReducedMesh.CreatePolygonGroupWithID(PolygonGroupID);
				OutPolygonGroupMaterialNames[PolygonGroupID] = InPolygonGroupMaterialNames[PolygonGroupID];
			}

			TVertexAttributesRef<FVector> OutVertexPositions = OutReducedMesh.GetVertexPositions();

			//Fill the vertex array
			for (int32 VertexIndex = 0; VertexIndex < (int32)NumVerts; ++VertexIndex)
			{
				FVertexID AddedVertexId = OutReducedMesh.CreateVertex();
				OutVertexPositions[AddedVertexId] = Verts[VertexIndex].Position;
				check(AddedVertexId.GetValue() == VertexIndex);
			}

			TMap<int32, FPolygonGroupID> PolygonGroupMapping;

			FStaticMeshAttributes Attributes(OutReducedMesh);
			TVertexInstanceAttributesRef<FVector> OutVertexNormals = Attributes.GetVertexInstanceNormals();
			TVertexInstanceAttributesRef<FVector> OutVertexTangents = Attributes.GetVertexInstanceTangents();
			TVertexInstanceAttributesRef<float> OutVertexBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
			TVertexInstanceAttributesRef<FVector4> OutVertexColors = Attributes.GetVertexInstanceColors();
			TVertexInstanceAttributesRef<FVector2D> OutVertexUVs = Attributes.GetVertexInstanceUVs();

			//Specify the number of texture coords in this mesh description
			OutVertexUVs.SetNumChannels(InMeshNumTexCoords);

			//Vertex instances and Polygons
			for (int32 TriangleIndex = 0; TriangleIndex < (int32)NumTris; TriangleIndex++)
			{
				FVertexInstanceID CornerInstanceIDs[3];

				FVertexID CornerVerticesIDs[3];
				for (int32 CornerIndex = 0; CornerIndex < 3; CornerIndex++)
				{
					int32 VertexInstanceIndex = TriangleIndex * 3 + CornerIndex;
					const FVertexInstanceID VertexInstanceID(VertexInstanceIndex);
					CornerInstanceIDs[CornerIndex] = VertexInstanceID;
					int32 ControlPointIndex = Indexes[VertexInstanceIndex];
					const FVertexID VertexID(ControlPointIndex);
					//FVector VertexPosition = OutReducedMesh.GetVertex(VertexID).VertexPosition;
					CornerVerticesIDs[CornerIndex] = VertexID;
					FVertexInstanceID AddedVertexInstanceId = OutReducedMesh.CreateVertexInstance(VertexID);
					//Make sure the Added vertex instance ID is matching the expected vertex instance ID
					check(AddedVertexInstanceId == VertexInstanceID);
					check(AddedVertexInstanceId.GetValue() == VertexInstanceIndex);

					//NTBs information
					OutVertexTangents[AddedVertexInstanceId] = Verts[Indexes[VertexInstanceIndex]].Tangents[0];
					OutVertexBinormalSigns[AddedVertexInstanceId] = GetBasisDeterminantSign(Verts[Indexes[VertexInstanceIndex]].Tangents[0].GetSafeNormal(), Verts[Indexes[VertexInstanceIndex]].Tangents[1].GetSafeNormal(), Verts[Indexes[VertexInstanceIndex]].Normal.GetSafeNormal());
					OutVertexNormals[AddedVertexInstanceId] = Verts[Indexes[VertexInstanceIndex]].Normal;

					//Vertex Color
					OutVertexColors[AddedVertexInstanceId] = Verts[Indexes[VertexInstanceIndex]].Color;

					//Texture coord
					for (int32 TexCoordIndex = 0; TexCoordIndex < InMeshNumTexCoords; TexCoordIndex++)
					{
						OutVertexUVs.Set(AddedVertexInstanceId, TexCoordIndex, Verts[Indexes[VertexInstanceIndex]].TexCoords[TexCoordIndex]);
					}
				}
				
				// material index
				int32 MaterialIndex = MaterialIndexes[TriangleIndex];
				FPolygonGroupID MaterialPolygonGroupID = INDEX_NONE;
				if (!PolygonGroupMapping.Contains(MaterialIndex))
				{
					FPolygonGroupID PolygonGroupID(MaterialIndex);
					check(InMesh.PolygonGroups().IsValid(PolygonGroupID));
					MaterialPolygonGroupID = OutReducedMesh.PolygonGroups().Num() > MaterialIndex ? PolygonGroupID : OutReducedMesh.CreatePolygonGroup();

					// Copy all attributes from the base polygon group to the new polygon group
					InMesh.PolygonGroupAttributes().ForEach(
						[&OutReducedMesh, PolygonGroupID, MaterialPolygonGroupID](const FName Name, const auto ArrayRef)
						{
							for (int32 Index = 0; Index < ArrayRef.GetNumChannels(); ++Index)
							{
								// Only copy shared attribute values, since input mesh description can differ from output mesh description
								const auto& Value = ArrayRef.Get(PolygonGroupID, Index);
								if (OutReducedMesh.PolygonGroupAttributes().HasAttribute(Name))
								{
									OutReducedMesh.PolygonGroupAttributes().SetAttribute(MaterialPolygonGroupID, Name, Index, Value);
								}
							}
						}
					);
					PolygonGroupMapping.Add(MaterialIndex, MaterialPolygonGroupID);
				}
				else
				{
					MaterialPolygonGroupID = PolygonGroupMapping[MaterialIndex];
				}

				// Insert a polygon into the mesh
				TArray<FEdgeID> NewEdgeIDs;
				const FTriangleID NewTriangleID = OutReducedMesh.CreateTriangle(MaterialPolygonGroupID, CornerInstanceIDs, &NewEdgeIDs);
				for (const FEdgeID& NewEdgeID : NewEdgeIDs)
				{
					// @todo: set NewEdgeID edge hardness?
				}
			}
			Verts.Empty();
			Indexes.Empty();

			//Remove the unused polygon group (reduce can remove all polygons from a group)
			TArray<FPolygonGroupID> ToDeletePolygonGroupIDs;
			for (const FPolygonGroupID PolygonGroupID : OutReducedMesh.PolygonGroups().GetElementIDs())
			{
				if (OutReducedMesh.GetPolygonGroupPolygonIDs(PolygonGroupID).Num() == 0)
				{
					ToDeletePolygonGroupIDs.Add(PolygonGroupID);
				}
			}
			for (const FPolygonGroupID& PolygonGroupID : ToDeletePolygonGroupIDs)
			{
				OutReducedMesh.DeletePolygonGroup(PolygonGroupID);
			}
		}
	}

	virtual bool ReduceSkeletalMesh(
		USkeletalMesh* SkeletalMesh,
		int32 LODIndex,
		const class ITargetPlatform* TargetPlatform
		) override
	{
		return false;
	}

	virtual bool IsSupported() const override
	{
		return true;
	}

	/**
	*	Returns true if mesh reduction is active. Active mean there will be a reduction of the vertices or triangle number
	*/
	virtual bool IsReductionActive(const struct FMeshReductionSettings &ReductionSettings) const
	{
		float Threshold_One = (1.0f - KINDA_SMALL_NUMBER);
		switch (ReductionSettings.TerminationCriterion)
		{
			case EStaticMeshReductionTerimationCriterion::Triangles:
			{
				return ReductionSettings.PercentTriangles < Threshold_One;
			}
			break;
			case EStaticMeshReductionTerimationCriterion::Vertices:
			{
				return ReductionSettings.PercentVertices < Threshold_One;
			}
			break;
			case EStaticMeshReductionTerimationCriterion::Any:
			{
				return ReductionSettings.PercentTriangles < Threshold_One || ReductionSettings.PercentVertices < Threshold_One;
			}
			break;
		}
		return false;
	}

	virtual bool IsReductionActive(const FSkeletalMeshOptimizationSettings &ReductionSettings) const
	{
		return false;
	}

	virtual bool IsReductionActive(const struct FSkeletalMeshOptimizationSettings &ReductionSettings, uint32 NumVertices, uint32 NumTriangles) const
	{
		return false;
	}

	virtual ~FQuadricSimplifierMeshReduction() {}

	static FQuadricSimplifierMeshReduction* Create()
	{
		return new FQuadricSimplifierMeshReduction;
	}
};

TUniquePtr<FQuadricSimplifierMeshReduction> GQuadricSimplifierMeshReduction;

void FQuadricSimplifierMeshReductionModule::StartupModule()
{
	GQuadricSimplifierMeshReduction.Reset(FQuadricSimplifierMeshReduction::Create());
	IModularFeatures::Get().RegisterModularFeature(IMeshReductionModule::GetModularFeatureName(), this);
}

void FQuadricSimplifierMeshReductionModule::ShutdownModule()
{
	GQuadricSimplifierMeshReduction = nullptr;
	IModularFeatures::Get().UnregisterModularFeature(IMeshReductionModule::GetModularFeatureName(), this);
}

IMeshReduction* FQuadricSimplifierMeshReductionModule::GetStaticMeshReductionInterface()
{
	return GQuadricSimplifierMeshReduction.Get();
}

IMeshReduction* FQuadricSimplifierMeshReductionModule::GetSkeletalMeshReductionInterface()
{
	return nullptr;
}

IMeshMerging* FQuadricSimplifierMeshReductionModule::GetMeshMergingInterface()
{
	return nullptr;
}

class IMeshMerging* FQuadricSimplifierMeshReductionModule::GetDistributedMeshMergingInterface()
{
	return nullptr;
}

FString FQuadricSimplifierMeshReductionModule::GetName()
{
	return FString("QuadricMeshReduction");	
}
