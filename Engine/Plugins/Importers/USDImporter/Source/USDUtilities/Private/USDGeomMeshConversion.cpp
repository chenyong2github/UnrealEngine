// Copyright Epic Games, Inc. All Rights Reserved.

#if USE_USD_SDK

#include "USDGeomMeshConversion.h"

#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "Engine/StaticMesh.h"

#include "MaterialEditingLibrary.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

#include "USDIncludesStart.h"

#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/resolverScopedCache.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/layerUtils.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/subset.h"
#include "pxr/usd/usdShade/material.h"

#include "USDIncludesEnd.h"

namespace UsdToUnrealImpl
{
	int32 GetPrimValueIndex( const pxr::TfToken& InterpType, const int32 VertexIndex, const int32 VertexInstanceIndex, const int32 PolygonIndex )
	{
		if ( InterpType == pxr::UsdGeomTokens->vertex )
		{
			return VertexIndex;
		}
		else if ( InterpType == pxr::UsdGeomTokens->faceVarying )
		{
			return VertexInstanceIndex;
		}
		else if ( InterpType == pxr::UsdGeomTokens->uniform )
		{
			return PolygonIndex;
		}
		else /* if ( InterpType == pxr::UsdGeomTokens->constant ) */
		{
			return 0; // return index 0 for constant or any other unsupported cases
		}
	}
}

bool UsdToUnreal::ConvertGeomMesh( const pxr::UsdGeomMesh& UsdMesh, FMeshDescription& MeshDescription, const pxr::UsdTimeCode TimeCode )
{
	return ConvertGeomMesh( UsdMesh, MeshDescription, FTransform::Identity, TimeCode );
}

bool UsdToUnreal::ConvertGeomMesh( const pxr::UsdGeomMesh& UsdMesh, FMeshDescription& MeshDescription, const FTransform& AdditionalTransform, const pxr::UsdTimeCode TimeCode )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( UsdToUnreal::ConvertGeomMesh );

	using namespace pxr;

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdStageRefPtr Stage = UsdMesh.GetPrim().GetStage();
	const UsdToUnreal::FUsdStageInfo StageInfo( Stage );

	const double TimeCodeValue = TimeCode.GetValue();

	TTuple< TArray< FString >, TArray< int32 > > GeometryMaterials = IUsdPrim::GetGeometryMaterials( TimeCodeValue, UsdMesh.GetPrim() );
	TArray< FString >& MaterialNames = GeometryMaterials.Key;
	TArray< int32 >& FaceMaterialIndices = GeometryMaterials.Value;

	const int32 VertexOffset = MeshDescription.Vertices().Num();
	const int32 VertexInstanceOffset = MeshDescription.VertexInstances().Num();
	const int32 PolygonOffset = MeshDescription.Polygons().Num();
	const int32 MaterialIndexOffset = MeshDescription.PolygonGroups().Num();

	FStaticMeshAttributes StaticMeshAttributes( MeshDescription );

	// Vertex positions
	TVertexAttributesRef< FVector > MeshDescriptionVertexPositions = StaticMeshAttributes.GetVertexPositions();
	{
		pxr::UsdAttribute Points = UsdMesh.GetPointsAttr();
		if ( Points )
		{
			pxr::VtArray< pxr::GfVec3f > PointsArray;
			Points.Get( &PointsArray, TimeCodeValue );

			MeshDescription.ReserveNewVertices( PointsArray.size() );

			for ( int32 LocalPointIndex = 0; LocalPointIndex < PointsArray.size(); ++LocalPointIndex )
			{
				const GfVec3f& Point = PointsArray[ LocalPointIndex ];

				FVector Position = AdditionalTransform.TransformPosition( UsdToUnreal::ConvertVector( StageInfo, Point ) );

				FVertexID AddedVertexId = MeshDescription.CreateVertex();
				MeshDescriptionVertexPositions[ AddedVertexId ] = Position;
			}
		}
	}
	
	// Polygons
	{
		TMap<int32, FPolygonGroupID> PolygonGroupMapping;
		TArray<FVertexInstanceID> CornerInstanceIDs;
		TArray<FVertexID> CornerVerticesIDs;
		int32 CurrentVertexInstanceIndex = 0;

		bool bFlipThisGeometry = false;

		if ( IUsdPrim::GetGeometryOrientation( UsdMesh ) == EUsdGeomOrientation::LeftHanded )
		{
			bFlipThisGeometry = !bFlipThisGeometry;
		}

		// Face counts
		UsdAttribute FaceCountsAttribute = UsdMesh.GetFaceVertexCountsAttr();
		VtArray< int > FaceCounts;

		if ( FaceCountsAttribute )
		{
			FaceCountsAttribute.Get( &FaceCounts, TimeCodeValue );
		}

		// Face indices
		UsdAttribute FaceIndicesAttribute = UsdMesh.GetFaceVertexIndicesAttr();
		VtArray< int > FaceIndices;

		if ( FaceIndicesAttribute )
		{
			FaceIndicesAttribute.Get( &FaceIndices, TimeCodeValue );
		}

		// Normals
		UsdAttribute NormalsAttribute = UsdMesh.GetNormalsAttr();
		VtArray< GfVec3f > Normals;

		if ( NormalsAttribute )
		{
			NormalsAttribute.Get( &Normals, TimeCodeValue );
		}

		pxr::TfToken NormalsInterpType = UsdMesh.GetNormalsInterpolation();

		// UVs
		TVertexInstanceAttributesRef< FVector2D > MeshDescriptionUVs = StaticMeshAttributes.GetVertexInstanceUVs();

		struct FUVSet
		{
			TOptional< VtIntArray > UVIndices; // UVs might be indexed or they might be flat (one per vertex)
			VtVec2fArray UVs;

			pxr::TfToken InterpType = UsdGeomTokens->faceVarying;
		};

		TArray< FUVSet > UVSets;

		int32 UVChannelIndex = 0;
		while ( true )
		{
			pxr::TfToken UsdUVSetName = UsdUtils::GetUVSetName( UVChannelIndex ).Get();
			UsdGeomPrimvar PrimvarST = UsdMesh.GetPrimvar( UsdUVSetName );

			if ( PrimvarST )
			{
				FUVSet UVSet;
				UVSet.InterpType = PrimvarST.GetInterpolation();

				if ( PrimvarST.IsIndexed() )
				{
					UVSet.UVIndices.Emplace();

					if ( PrimvarST.GetIndices( &UVSet.UVIndices.GetValue(), TimeCode ) && PrimvarST.Get( &UVSet.UVs, TimeCode ) )
					{
						if ( UVSet.UVs.size() > 0 )
						{
							UVSets.Add( MoveTemp( UVSet ) );
						}
					}
				}
				else
				{
					if ( PrimvarST.Get( &UVSet.UVs ) )
					{
						if ( UVSet.UVs.size() > 0 )
						{
							UVSets.Add( MoveTemp( UVSet ) );
						}
					}
				}
			}
			else
			{
				break;
			}

			++UVChannelIndex;
		}

		// When importing multiple mesh pieces to the same static mesh.  Ensure each mesh piece has the same number of Uv's
		{
			int32 ExistingUVCount = MeshDescriptionUVs.GetNumIndices();
			int32 NumUVs = FMath::Max(UVSets.Num(), ExistingUVCount);
			NumUVs = FMath::Min<int32>(MAX_MESH_TEXTURE_COORDS_MD, NumUVs);
			// At least one UV set must exist.  
			NumUVs = FMath::Max<int32>(1, NumUVs);

			//Make sure all Vertex instance have the correct number of UVs
			MeshDescriptionUVs.SetNumIndices(NumUVs);
		}

		TVertexInstanceAttributesRef< FVector > MeshDescriptionNormals = StaticMeshAttributes.GetVertexInstanceNormals();
		TPolygonGroupAttributesRef< FName > PolygonGroupImportedMaterialSlotNames = StaticMeshAttributes.GetPolygonGroupMaterialSlotNames();
		TPolygonGroupAttributesRef< FName > PolygonGroupUsdPrimPaths = MeshDescription.PolygonGroupAttributes().GetAttributesRef< FName >( "UsdPrimPath" );

		MeshDescription.ReserveNewVertexInstances( FaceCounts.size() * 3 );
		MeshDescription.ReserveNewPolygons( FaceCounts.size() );
		MeshDescription.ReserveNewEdges( FaceCounts.size() * 2 );

		// Vertex color
		TVertexInstanceAttributesRef< FVector4 > MeshDescriptionColors = StaticMeshAttributes.GetVertexInstanceColors();

		UsdGeomPrimvar ColorPrimvar = UsdMesh.GetDisplayColorPrimvar();
		pxr::TfToken ColorInterpolation = UsdGeomTokens->constant;
		pxr::VtArray< pxr::GfVec3f > UsdColors;

		if ( ColorPrimvar )
		{
			ColorPrimvar.ComputeFlattened( &UsdColors, TimeCode );
			ColorInterpolation = ColorPrimvar.GetInterpolation();
		}

		// Vertex opacity
		UsdGeomPrimvar OpacityPrimvar = UsdMesh.GetDisplayOpacityPrimvar();
		pxr::TfToken OpacityInterpolation = UsdGeomTokens->constant;
		pxr::VtArray< float > UsdOpacities;

		if ( OpacityPrimvar )
		{
			OpacityPrimvar.ComputeFlattened( &UsdOpacities );
			OpacityInterpolation = OpacityPrimvar.GetInterpolation();
		}

		for ( int32 PolygonIndex = 0; PolygonIndex < FaceCounts.size(); ++PolygonIndex )
		{
			int32 PolygonVertexCount = FaceCounts[PolygonIndex];
			CornerInstanceIDs.Reset( PolygonVertexCount );
			CornerVerticesIDs.Reset( PolygonVertexCount );

			for (int32 CornerIndex = 0; CornerIndex < PolygonVertexCount; ++CornerIndex, ++CurrentVertexInstanceIndex)
			{
				int32 VertexInstanceIndex = VertexInstanceOffset + CurrentVertexInstanceIndex;
				const FVertexInstanceID VertexInstanceID(VertexInstanceIndex);
				const int32 ControlPointIndex = FaceIndices[CurrentVertexInstanceIndex];
				const FVertexID VertexID(VertexOffset + ControlPointIndex);
				const FVector VertexPosition = MeshDescriptionVertexPositions[VertexID];

				// Make sure a face doesn't use the same vertex twice as MeshDescription doesn't like that
				if ( CornerVerticesIDs.Contains( VertexID ) )
				{
					continue;
				}

				CornerVerticesIDs.Add( VertexID );

				FVertexInstanceID AddedVertexInstanceId = MeshDescription.CreateVertexInstance(VertexID);
				CornerInstanceIDs.Add( AddedVertexInstanceId );

				if ( Normals.size() > 0 )
				{
					const int32 NormalIndex = UsdToUnrealImpl::GetPrimValueIndex( NormalsInterpType, ControlPointIndex, CurrentVertexInstanceIndex, PolygonIndex );

					if ( NormalIndex < Normals.size() )
					{
						const GfVec3f& Normal = Normals[NormalIndex];
						FVector TransformedNormal = AdditionalTransform.TransformVector( UsdToUnreal::ConvertVector( StageInfo, Normal ) ).GetSafeNormal();

						MeshDescriptionNormals[AddedVertexInstanceId] = TransformedNormal.GetSafeNormal();
					}
				}

				int32 UVLayerIndex = 0;
				for ( const FUVSet& UVSet : UVSets )
				{
					const int32 ValueIndex = UsdToUnrealImpl::GetPrimValueIndex( UVSet.InterpType, ControlPointIndex, CurrentVertexInstanceIndex, PolygonIndex );

					GfVec2f UV( 0.f, 0.f );

					if ( UVSet.UVIndices.IsSet() )
					{
						if ( ensure( UVSet.UVIndices.GetValue().size() > ValueIndex ) )
						{
							UV = UVSet.UVs[ UVSet.UVIndices.GetValue()[ ValueIndex ] ];
						}
					}
					else if ( ensure( UVSet.UVs.size() > ValueIndex ) )
					{
						UV = UVSet.UVs[ ValueIndex ];
					}

					// Flip V for Unreal uv's which match directx
					FVector2D FinalUVVector( UV[0], 1.f - UV[1] );
					MeshDescriptionUVs.Set( AddedVertexInstanceId, UVLayerIndex, FinalUVVector );

					++UVLayerIndex;
				}

				// Vertex color
				{
					auto ConvertToLinear = []( const pxr::GfVec3f& UsdColor ) -> FLinearColor
					{
						return FLinearColor( FLinearColor( UsdToUnreal::ConvertColor( UsdColor ) ).ToFColor( false ) );
					};

					const int32 ValueIndex = UsdToUnrealImpl::GetPrimValueIndex( ColorInterpolation, ControlPointIndex, CurrentVertexInstanceIndex, PolygonIndex );

					GfVec3f UsdColor( 1.f, 1.f, 1.f );

					if ( !UsdColors.empty() && ensure( UsdColors.size() > ValueIndex ) )
					{
						UsdColor = UsdColors[ ValueIndex ];
					}

					MeshDescriptionColors[ AddedVertexInstanceId ] = ConvertToLinear( UsdColor );
				}

				// Vertex opacity
				{
					const int32 ValueIndex = UsdToUnrealImpl::GetPrimValueIndex( OpacityInterpolation, ControlPointIndex, CurrentVertexInstanceIndex, PolygonIndex );

					if ( !UsdOpacities.empty() && ensure( UsdOpacities.size() > ValueIndex ) )
					{
						MeshDescriptionColors[ AddedVertexInstanceId ][3] = UsdOpacities[ ValueIndex ];
					}
				}
			}

			// Polygon groups
			int32 MaterialIndex = 0;
			if ( FaceMaterialIndices.IsValidIndex( PolygonIndex ) )
			{
				MaterialIndex = FaceMaterialIndices[PolygonIndex];
				if (MaterialIndex < 0 || MaterialIndex > MaterialNames.Num())
				{
					MaterialIndex = 0;
				}
			}

			const int32 RealMaterialIndex = MaterialIndexOffset + MaterialIndex;

			if ( !PolygonGroupMapping.Contains( RealMaterialIndex ) )
			{
				FName ImportedMaterialSlotName = *LexToString( RealMaterialIndex );
				if ( MaterialNames.IsValidIndex( MaterialIndex ) )
				{
					FString MaterialName = MaterialNames[MaterialIndex];
					ImportedMaterialSlotName = FName(*MaterialName);
				}

				FPolygonGroupID NewPolygonGroup = MeshDescription.CreatePolygonGroup();
				PolygonGroupImportedMaterialSlotNames[ NewPolygonGroup ] = ImportedMaterialSlotName;

				if ( PolygonGroupUsdPrimPaths.IsValid() )
				{
					PolygonGroupUsdPrimPaths[ NewPolygonGroup ] = FName( UsdToUnreal::ConvertPath( UsdMesh.GetPath() ) );
				}

				PolygonGroupMapping.Add( RealMaterialIndex, NewPolygonGroup );
			}

			FPolygonGroupID PolygonGroupID = PolygonGroupMapping[ RealMaterialIndex ];
			// Insert a polygon into the mesh
			const FPolygonID NewPolygonID = MeshDescription.CreatePolygon( PolygonGroupID, CornerInstanceIDs );
			if ( bFlipThisGeometry )
			{
				MeshDescription.ReversePolygonFacing( NewPolygonID );
			}
		}
	}

	return true;
}

bool UsdToUnreal::ConvertDisplayColor( const pxr::UsdGeomMesh& UsdMesh, UMaterialInstanceConstant& MaterialInstance, const pxr::UsdTimeCode TimeCode )
{
	FScopedUsdAllocs UsdAllocs;

	pxr::VtArray< pxr::GfVec3f > UsdDisplayColors = UsdUtils::GetUsdValue< pxr::VtArray< pxr::GfVec3f > >( UsdMesh.GetDisplayColorAttr(), TimeCode );

	if ( !UsdDisplayColors.empty() )
	{
		pxr::VtArray< float > UsdOpacities = UsdUtils::GetUsdValue< pxr::VtArray< float > >( UsdMesh.GetDisplayOpacityAttr(), TimeCode );

		bool bHasTransparency = false;

		for ( float Opacity : UsdOpacities )
		{
			bHasTransparency = !FMath::IsNearlyEqual( Opacity, 1.f );

			if ( bHasTransparency )
			{
				break;
			}
		}

		{
			FScopedUnrealAllocs UnrealAllocs;

			FSoftObjectPath MaterialPath;

			if ( bHasTransparency )
			{
				MaterialPath = FSoftObjectPath( TEXT("Material'/USDImporter/Materials/DisplayColorAndOpacity.DisplayColorAndOpacity'") );
			}
			else
			{
				MaterialPath = FSoftObjectPath( TEXT("Material'/USDImporter/Materials/DisplayColor.DisplayColor'") );
			}

			UMaterialInterface* DisplayColorAndOpacityMaterial = Cast< UMaterialInterface >( MaterialPath.TryLoad() );
			UMaterialEditingLibrary::SetMaterialInstanceParent( &MaterialInstance, DisplayColorAndOpacityMaterial );

			/*if ( UsdMesh.GetDoubleSidedAttr().IsDefined() )
			{
				MaterialInstance.BasePropertyOverrides.bOverride_TwoSided = true;
				MaterialInstance.BasePropertyOverrides.TwoSided = UsdUtils::GetUsdValue< bool >( UsdMesh.GetDoubleSidedAttr(), TimeCode );
			}*/
		}

		return true;
	}
	else
	{
		return false;
	}
}

bool UnrealToUsd::ConvertStaticMesh( const UStaticMesh* StaticMesh, pxr::UsdGeomMesh& UsdMesh, const pxr::UsdTimeCode TimeCode )
{
	FScopedUsdAllocs UsdAllocs;

	pxr::UsdStageRefPtr Stage = UsdMesh.GetPrim().GetStage();

	if ( !Stage )
	{
		return false;
	}

	const pxr::TfToken StageUpAxis = UsdUtils::GetUsdStageAxis( Stage );

	const FStaticMeshLODResources& RenderMesh = StaticMesh->GetLODForExport( 0 /*ExportLOD*/ );

	// Verify the integrity of the static mesh.
	if ( RenderMesh.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices() == 0 )
	{
		return false;
	}

	if ( RenderMesh.Sections.Num() == 0 )
	{
		return false;
	}

	// Vertices
	{
		const int32 VertexCount = RenderMesh.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();

		// Points
		{
			pxr::UsdAttribute Points = UsdMesh.CreatePointsAttr();
			if ( Points )
			{
				pxr::VtArray< pxr::GfVec3f > PointsArray;
				PointsArray.reserve( VertexCount );

				for ( int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex )
				{
					FVector VertexPosition = RenderMesh.VertexBuffers.PositionVertexBuffer.VertexPosition( VertexIndex );
					PointsArray.push_back( UnrealToUsd::ConvertVector( StageUpAxis, VertexPosition ) );
				}

				Points.Set( PointsArray, TimeCode );
			}
		}

		// Normals
		{
			pxr::UsdAttribute NormalsAttribute = UsdMesh.CreateNormalsAttr();
			if ( NormalsAttribute )
			{
				pxr::VtArray< pxr::GfVec3f > Normals;
				Normals.reserve( VertexCount );

				for ( int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex )
				{
					FVector VertexNormal = RenderMesh.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ( VertexIndex );
					Normals.push_back( UnrealToUsd::ConvertVector( StageUpAxis, VertexNormal ) );
				}

				NormalsAttribute.Set( Normals, TimeCode );
			}
		}

		// UVs
		{
			const int32 TexCoordSourceCount = RenderMesh.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();

			for ( int32 TexCoordSourceIndex = 0; TexCoordSourceIndex < TexCoordSourceCount; ++TexCoordSourceIndex )
			{
				pxr::TfToken UsdUVSetName = UsdUtils::GetUVSetName( TexCoordSourceIndex ).Get();

				pxr::UsdGeomPrimvar PrimvarST = UsdMesh.CreatePrimvar( UsdUVSetName, pxr::SdfValueTypeNames->Float2Array, pxr::UsdGeomTokens->vertex );

				if ( PrimvarST )
				{
					pxr::VtVec2fArray UVs;

					for ( int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex )
					{
						FVector2D TexCoord = RenderMesh.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV( VertexIndex, TexCoordSourceIndex );
						TexCoord[1] = 1.f - TexCoord[1];

						UVs.push_back( UnrealToUsd::ConvertVector( TexCoord ) );
					}

					PrimvarST.Set( UVs, TimeCode );
				}
			}
		}
	}
	
	// Faces
	{
		const int32 FaceCount = RenderMesh.GetNumTriangles();

		// Face Vertex Counts
		{
			pxr::UsdAttribute FaceCountsAttribute = UsdMesh.CreateFaceVertexCountsAttr();

			if ( FaceCountsAttribute )
			{
				pxr::VtArray< int > FaceVertexCounts;
				FaceVertexCounts.reserve( FaceCount );

				for ( int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex )
				{
					FaceVertexCounts.push_back( 3 );
				}

				FaceCountsAttribute.Set( FaceVertexCounts, TimeCode );
			}
		}

		// Face Vertex Indices
		{
			pxr::UsdAttribute FaceVertexIndicesAttribute = UsdMesh.GetFaceVertexIndicesAttr();

			if ( FaceVertexIndicesAttribute )
			{
				FIndexArrayView Indices = RenderMesh.IndexBuffer.GetArrayView();
				ensure( Indices.Num() == FaceCount * 3 );

				pxr::VtArray< int > FaceVertexIndices;
				FaceVertexIndices.reserve( FaceCount * 3 );

				for ( int32 Index = 0; Index < FaceCount * 3; ++Index )
				{
					FaceVertexIndices.push_back( Indices[ Index ] );
				}
				
				FaceVertexIndicesAttribute.Set( FaceVertexIndices, TimeCode );
			}
		}
	}

	// TODO: Material Mappings, Vertex Colors, LODs...

	return true;
}

#endif // #if USE_USD_SDK
