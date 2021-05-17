// Copyright Epic Games, Inc. All Rights Reserved.

#if USE_USD_SDK

#include "USDGeomMeshConversion.h"

#include "UnrealUSDWrapper.h"
#include "USDAssetImportData.h"
#include "USDConversionUtils.h"
#include "USDErrorUtils.h"
#include "USDLog.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

#if WITH_EDITOR
#include "MaterialEditingLibrary.h"
#endif // WITH_EDITOR

#include "USDIncludesStart.h"
	#include "pxr/usd/ar/resolver.h"
	#include "pxr/usd/ar/resolverScopedCache.h"
	#include "pxr/usd/sdf/layer.h"
	#include "pxr/usd/sdf/layerUtils.h"
	#include "pxr/usd/sdf/types.h"
	#include "pxr/usd/usd/editContext.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdGeom/primvarsAPI.h"
	#include "pxr/usd/usdGeom/subset.h"
	#include "pxr/usd/usdGeom/tokens.h"
	#include "pxr/usd/usdShade/material.h"
	#include "pxr/usd/usdShade/materialBindingAPI.h"
	#include "pxr/usd/usdShade/tokens.h"
#include "USDIncludesEnd.h"

#define LOCTEXT_NAMESPACE "USDGeomMeshConversion"

namespace UE
{
	namespace UsdGeomMeshConversion
	{
		namespace Private
		{
			static const FString DisplayColorID = TEXT( "!DisplayColor" );

			int32 GetPrimValueIndex( const pxr::TfToken& InterpType, const int32 VertexIndex, const int32 VertexInstanceIndex, const int32 PolygonIndex )
			{
				if ( InterpType == pxr::UsdGeomTokens->vertex )
				{
					return VertexIndex;
				}
				else if ( InterpType == pxr::UsdGeomTokens->varying )
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

			int32 GetLODIndexFromName( const std::string& Name )
			{
				const std::string LODString = UnrealIdentifiers::LOD.GetString();

				// True if Name does not start with "LOD"
				if ( Name.rfind( LODString, 0 ) != 0 )
				{
					return INDEX_NONE;
				}

				// After LODString there should be only numbers
				if ( Name.find_first_not_of( "0123456789", LODString.size() ) != std::string::npos )
				{
					return INDEX_NONE;
				}

				const int Base = 10;
				char** EndPtr = nullptr;
				return std::strtol( Name.c_str() + LODString.size(), EndPtr, Base );
			}

			void ConvertStaticMeshLOD(
				int32 LODIndex,
				const FStaticMeshLODResources& LODRenderMesh,
				pxr::UsdGeomMesh& UsdMesh,
				const pxr::VtArray< std::string >& MaterialAssignments,
				const pxr::UsdTimeCode TimeCode,
				pxr::UsdPrim MaterialPrim
			)
			{
				pxr::UsdPrim MeshPrim = UsdMesh.GetPrim();
				pxr::UsdStageRefPtr Stage = MeshPrim.GetStage();
				if ( !Stage )
				{
					return;
				}
				const FUsdStageInfo StageInfo{ Stage };

				// Vertices
				{
					const int32 VertexCount = LODRenderMesh.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();

					// Points
					{
						pxr::UsdAttribute Points = UsdMesh.CreatePointsAttr();
						if ( Points )
						{
							pxr::VtArray< pxr::GfVec3f > PointsArray;
							PointsArray.reserve( VertexCount );

							for ( int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex )
							{
								FVector VertexPosition = LODRenderMesh.VertexBuffers.PositionVertexBuffer.VertexPosition( VertexIndex );
								PointsArray.push_back( UnrealToUsd::ConvertVector( StageInfo, VertexPosition ) );
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
								FVector VertexNormal = LODRenderMesh.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ( VertexIndex );
								Normals.push_back( UnrealToUsd::ConvertVector( StageInfo, VertexNormal ) );
							}

							NormalsAttribute.Set( Normals, TimeCode );
						}
					}

					// UVs
					{
						const int32 TexCoordSourceCount = LODRenderMesh.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();

						for ( int32 TexCoordSourceIndex = 0; TexCoordSourceIndex < TexCoordSourceCount; ++TexCoordSourceIndex )
						{
							pxr::TfToken UsdUVSetName = UsdUtils::GetUVSetName( TexCoordSourceIndex ).Get();

							pxr::UsdGeomPrimvar PrimvarST = UsdMesh.CreatePrimvar( UsdUVSetName, pxr::SdfValueTypeNames->TexCoord2fArray, pxr::UsdGeomTokens->vertex );

							if ( PrimvarST )
							{
								pxr::VtVec2fArray UVs;

								for ( int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex )
								{
									FVector2D TexCoord = LODRenderMesh.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV( VertexIndex, TexCoordSourceIndex );
									TexCoord[ 1 ] = 1.f - TexCoord[ 1 ];

									UVs.push_back( UnrealToUsd::ConvertVector( TexCoord ) );
								}

								PrimvarST.Set( UVs, TimeCode );
							}
						}
					}

					// Vertex colors
					if ( LODRenderMesh.bHasColorVertexData )
					{
						pxr::UsdGeomPrimvar DisplayColorPrimvar = UsdMesh.CreateDisplayColorPrimvar( pxr::UsdGeomTokens->vertex );
						pxr::UsdGeomPrimvar DisplayOpacityPrimvar = UsdMesh.CreateDisplayOpacityPrimvar( pxr::UsdGeomTokens->vertex );

						if ( DisplayColorPrimvar )
						{
							pxr::VtArray< pxr::GfVec3f > DisplayColors;
							DisplayColors.reserve( VertexCount );

							pxr::VtArray< float > DisplayOpacities;
							DisplayOpacities.reserve( VertexCount );

							for ( int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex )
							{
								const FColor& VertexColor = LODRenderMesh.VertexBuffers.ColorVertexBuffer.VertexColor( VertexIndex );

								pxr::GfVec4f Color = UnrealToUsd::ConvertColor( VertexColor );
								DisplayColors.push_back( pxr::GfVec3f( Color[ 0 ], Color[ 1 ], Color[ 2 ] ) );
								DisplayOpacities.push_back( Color[ 3 ] );
							}

							DisplayColorPrimvar.Set( DisplayColors, TimeCode );
							DisplayOpacityPrimvar.Set( DisplayOpacities, TimeCode );
						}
					}
				}

				// Faces
				{
					const int32 FaceCount = LODRenderMesh.GetNumTriangles();

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
							FIndexArrayView Indices = LODRenderMesh.IndexBuffer.GetArrayView();
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

				// Material assignments
				{
					bool bHasUEMaterialAssignements = false;

					pxr::VtArray< std::string > UnrealMaterialsForLOD;
					for ( const FStaticMeshSection& Section : LODRenderMesh.Sections )
					{
						if ( Section.MaterialIndex >= 0 && Section.MaterialIndex < MaterialAssignments.size() )
						{
							UnrealMaterialsForLOD.push_back( MaterialAssignments[ Section.MaterialIndex ] );
							bHasUEMaterialAssignements = true;
						}
						else
						{
							// Keep unrealMaterials with the same number of elements as our MaterialIndices expect
							UnrealMaterialsForLOD.push_back( "" );
						}
					}

					// This LOD has a single material assignment, just add an unrealMaterials attribute to the mesh prim
					if ( bHasUEMaterialAssignements && UnrealMaterialsForLOD.size() == 1 )
					{
						const bool bHasMaterialAttribute = MaterialPrim.HasAttribute( UnrealIdentifiers::MaterialAssignments );
						if ( bHasUEMaterialAssignements )
						{
							if ( pxr::UsdAttribute UEMaterialsAttribute = MaterialPrim.CreateAttribute( UnrealIdentifiers::MaterialAssignment, pxr::SdfValueTypeNames->String ) )
							{
								UEMaterialsAttribute.Set( UnrealMaterialsForLOD[ 0 ] );
							}
						}
						else if ( bHasMaterialAttribute )
						{
							MaterialPrim.GetAttribute( UnrealIdentifiers::MaterialAssignments ).Clear();
						}
					}
					// Multiple material assignments to the same LOD (and so the same mesh prim). Need to create a GeomSubset for each UE mesh section
					else if ( UnrealMaterialsForLOD.size() > 1 )
					{
						// Need to fetch all triangles of a section, and add their indices
						for ( int32 SectionIndex = 0; SectionIndex < LODRenderMesh.Sections.Num(); ++SectionIndex )
						{
							const FStaticMeshSection& Section = LODRenderMesh.Sections[ SectionIndex ];

							// Note that we will continue on even if we have no material assignment, so as to satisfy the "partition" family condition (below)
							std::string SectionMaterial;
							if ( Section.MaterialIndex >= 0 && Section.MaterialIndex < MaterialAssignments.size() )
							{
								SectionMaterial = MaterialAssignments[ Section.MaterialIndex ];
							}

							pxr::UsdPrim GeomSubsetPrim = Stage->DefinePrim(
								MeshPrim.GetPath().AppendPath( pxr::SdfPath( "Section" + std::to_string( SectionIndex ) ) ),
								UnrealToUsd::ConvertToken( TEXT( "GeomSubset" ) ).Get()
							);

							// MaterialPrim may be in another stage, so we may need another GeomSubset there
							pxr::UsdPrim MaterialGeomSubsetPrim = GeomSubsetPrim;
							if ( MaterialPrim.GetStage() != MeshPrim.GetStage() )
							{
								MaterialGeomSubsetPrim = MaterialPrim.GetStage()->OverridePrim(
									MaterialPrim.GetPath().AppendPath( pxr::SdfPath( "Section" + std::to_string( SectionIndex ) ) )
								);
							}

							pxr::UsdGeomSubset GeomSubsetSchema{ GeomSubsetPrim };

							// Element type attribute
							pxr::UsdAttribute ElementTypeAttr = GeomSubsetSchema.CreateElementTypeAttr();
							ElementTypeAttr.Set( pxr::UsdGeomTokens->face, TimeCode );

							// Indices attribute
							const uint32 TriangleCount = Section.NumTriangles;
							const uint32 FirstTriangleIndex = Section.FirstIndex / 3; // FirstIndex is the first *vertex* instance index
							FIndexArrayView VertexInstances = LODRenderMesh.IndexBuffer.GetArrayView();
							pxr::VtArray<int> IndicesAttrValue;
							for ( uint32 TriangleIndex = FirstTriangleIndex; TriangleIndex - FirstTriangleIndex < TriangleCount; ++TriangleIndex )
							{
								// Note that we add VertexInstances in sequence to the usda file for the faceVertexInstances attribute, which
								// also constitutes our triangle order
								IndicesAttrValue.push_back( static_cast< int >( TriangleIndex ) );
							}

							pxr::UsdAttribute IndicesAttr = GeomSubsetSchema.CreateIndicesAttr();
							IndicesAttr.Set( IndicesAttrValue, TimeCode );

							// Family name attribute
							pxr::UsdAttribute FamilyNameAttr = GeomSubsetSchema.CreateFamilyNameAttr();
							FamilyNameAttr.Set( pxr::UsdShadeTokens->materialBind, TimeCode );

							// Family type
							pxr::UsdGeomSubset::SetFamilyType( UsdMesh, pxr::UsdShadeTokens->materialBind, pxr::UsdGeomTokens->partition );

							// unrealMaterial attribute
							if ( pxr::UsdAttribute UEMaterialsAttribute = MaterialGeomSubsetPrim.CreateAttribute( UnrealIdentifiers::MaterialAssignment, pxr::SdfValueTypeNames->String ) )
							{
								UEMaterialsAttribute.Set( UnrealMaterialsForLOD[ SectionIndex ] );
							}
						}
					}
				}
			}

			bool ConvertMeshDescription( const FMeshDescription& MeshDescription, pxr::UsdGeomMesh& UsdMesh, const FMatrix& AdditionalTransform, const pxr::UsdTimeCode TimeCode )
			{
				pxr::UsdPrim MeshPrim = UsdMesh.GetPrim();
				pxr::UsdStageRefPtr Stage = MeshPrim.GetStage();
				if ( !Stage )
				{
					return false;
				}
				const FUsdStageInfo StageInfo{ Stage };

				FStaticMeshConstAttributes Attributes(MeshDescription);
				TVertexAttributesConstRef<FVector> VertexPositions = Attributes.GetVertexPositions();
				TPolygonGroupAttributesConstRef<FName> PolygonGroupImportedMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
				TVertexInstanceAttributesConstRef<FVector> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
				TVertexInstanceAttributesConstRef<FVector4> VertexInstanceColors = Attributes.GetVertexInstanceColors();
				TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();

				const int32 VertexCount = VertexPositions.GetNumElements();
				const int32 VertexInstanceCount = VertexInstanceNormals.GetNumElements();
				const int32 FaceCount = MeshDescription.Polygons().Num();

				// Points
				{
					if ( pxr::UsdAttribute Points = UsdMesh.CreatePointsAttr() )
					{
						pxr::VtArray< pxr::GfVec3f > PointsArray;
						PointsArray.reserve( VertexCount );

						for ( const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs() )
						{
							FVector UEPosition = AdditionalTransform.TransformPosition( VertexPositions[ VertexID ] );
							PointsArray.push_back( UnrealToUsd::ConvertVector( StageInfo, UEPosition ) );
						}

						Points.Set( PointsArray, TimeCode );
					}
				}

				// Normals
				{
					if ( pxr::UsdAttribute NormalsAttribute = UsdMesh.CreateNormalsAttr() )
					{
						pxr::VtArray< pxr::GfVec3f > Normals;
						Normals.reserve( VertexInstanceCount );

						for ( const FVertexInstanceID InstanceID : MeshDescription.VertexInstances().GetElementIDs() )
						{
							FVector UENormal = VertexInstanceNormals[ InstanceID ].GetSafeNormal();
							Normals.push_back( UnrealToUsd::ConvertVector( StageInfo, UENormal ) );
						}

						NormalsAttribute.Set( Normals, TimeCode );
						UsdMesh.SetNormalsInterpolation( pxr::UsdGeomTokens->faceVarying );
					}
				}

				// UVs
				{
					int32 NumUVs = VertexInstanceUVs.GetNumIndices();

					for ( int32 UVIndex = 0; UVIndex < NumUVs; ++UVIndex )
					{
						pxr::TfToken UsdUVSetName = UsdUtils::GetUVSetName( UVIndex ).Get();

						pxr::UsdGeomPrimvar PrimvarST = UsdMesh.CreatePrimvar( UsdUVSetName, pxr::SdfValueTypeNames->TexCoord2fArray, pxr::UsdGeomTokens->vertex );
						if ( PrimvarST )
						{
							pxr::VtVec2fArray UVs;

							for ( const FVertexInstanceID InstanceID : MeshDescription.VertexInstances().GetElementIDs() )
							{
								FVector2D UV = VertexInstanceUVs.Get( InstanceID, UVIndex );
								UV[ 1 ] = 1.f - UV[ 1 ];
								UVs.push_back( UnrealToUsd::ConvertVector( UV ) );
							}

							PrimvarST.Set( UVs, TimeCode );
							PrimvarST.SetInterpolation( pxr::UsdGeomTokens->faceVarying );
						}
					}
				}

				// Vertex colors
				if ( VertexInstanceColors.GetNumElements() > 0 )
				{
					pxr::UsdGeomPrimvar DisplayColorPrimvar = UsdMesh.CreateDisplayColorPrimvar( pxr::UsdGeomTokens->faceVarying );
					pxr::UsdGeomPrimvar DisplayOpacityPrimvar = UsdMesh.CreateDisplayOpacityPrimvar( pxr::UsdGeomTokens->faceVarying );
					if ( DisplayColorPrimvar && DisplayOpacityPrimvar )
					{
						pxr::VtArray< pxr::GfVec3f > DisplayColors;
						DisplayColors.reserve( VertexInstanceCount );

						pxr::VtArray< float > DisplayOpacities;
						DisplayOpacities.reserve( VertexInstanceCount );

						for ( const FVertexInstanceID InstanceID : MeshDescription.VertexInstances().GetElementIDs() )
						{
							pxr::GfVec4f Color = UnrealToUsd::ConvertColor( FLinearColor( VertexInstanceColors[ InstanceID ] ) );
							DisplayColors.push_back( pxr::GfVec3f( Color[ 0 ], Color[ 1 ], Color[ 2 ] ) );
							DisplayOpacities.push_back( Color[ 3 ] );
						}

						DisplayColorPrimvar.Set( DisplayColors, TimeCode );
						DisplayOpacityPrimvar.Set( DisplayOpacities, TimeCode );
					}
				}

				// Faces
				{
					pxr::UsdAttribute FaceCountsAttribute = UsdMesh.CreateFaceVertexCountsAttr();
					pxr::UsdAttribute FaceVertexIndicesAttribute = UsdMesh.GetFaceVertexIndicesAttr();

					pxr::VtArray< int > FaceVertexCounts;
					FaceVertexCounts.reserve( FaceCount );

					pxr::VtArray< int > FaceVertexIndices;

					for ( FPolygonID PolygonID : MeshDescription.Polygons().GetElementIDs() )
					{
						const TArray<FVertexInstanceID>& PolygonVertexInstances = MeshDescription.GetPolygonVertexInstances( PolygonID );
						FaceVertexCounts.push_back( static_cast< int >( PolygonVertexInstances.Num() ) );

						for ( FVertexInstanceID VertexInstanceID : PolygonVertexInstances )
						{
							int32 VertexIndex = MeshDescription.GetVertexInstanceVertex( VertexInstanceID ).GetValue();
							FaceVertexIndices.push_back( static_cast< int >( VertexIndex ) );
						}
					}

					FaceCountsAttribute.Set( FaceVertexCounts, TimeCode );
					FaceVertexIndicesAttribute.Set( FaceVertexIndices, TimeCode );
				}

				return true;
			}
		}
	}
}
namespace UsdGeomMeshImpl = UE::UsdGeomMeshConversion::Private;

bool UsdToUnreal::ConvertGeomMesh( const pxr::UsdTyped& UsdSchema, FMeshDescription& MeshDescription, const pxr::UsdTimeCode TimeCode )
{
	UsdUtils::FUsdPrimMaterialAssignmentInfo MaterialAssignments;
	return ConvertGeomMesh( UsdSchema, MeshDescription, MaterialAssignments, TimeCode );
}

bool UsdToUnreal::ConvertGeomMesh( const pxr::UsdTyped& UsdSchema, FMeshDescription& MeshDescription, const FTransform& AdditionalTransform, const pxr::UsdTimeCode TimeCode )
{
	UsdUtils::FUsdPrimMaterialAssignmentInfo MaterialAssignments;
	return ConvertGeomMesh( UsdSchema, MeshDescription, MaterialAssignments, AdditionalTransform, TimeCode );
}

bool UsdToUnreal::ConvertGeomMesh( const pxr::UsdTyped& UsdSchema, FMeshDescription& MeshDescription, const FTransform& AdditionalTransform, const TMap< FString, TMap< FString, int32 > >& MaterialToPrimvarsUVSetNames, const pxr::UsdTimeCode TimeCode )
{
	UsdUtils::FUsdPrimMaterialAssignmentInfo MaterialAssignments;
	return ConvertGeomMesh( UsdSchema, MeshDescription, MaterialAssignments, AdditionalTransform, MaterialToPrimvarsUVSetNames, TimeCode );
}

bool UsdToUnreal::ConvertGeomMesh( const pxr::UsdTyped& UsdSchema, FMeshDescription& MeshDescription, UsdUtils::FUsdPrimMaterialAssignmentInfo& MaterialAssignments,
	const pxr::UsdTimeCode TimeCode, const pxr::TfToken& RenderContext )
{
	TMap< FString, TMap< FString, int32 > > MaterialToPrimvarsUVSetNames;
	return ConvertGeomMesh( UsdSchema, MeshDescription, MaterialAssignments, FTransform::Identity, MaterialToPrimvarsUVSetNames, TimeCode, RenderContext );
}

bool UsdToUnreal::ConvertGeomMesh( const pxr::UsdTyped& UsdSchema, FMeshDescription& MeshDescription, UsdUtils::FUsdPrimMaterialAssignmentInfo& MaterialAssignments, const FTransform& AdditionalTransform,
	const pxr::UsdTimeCode TimeCode, const pxr::TfToken& RenderContext )
{
	TMap< FString, TMap< FString, int32 > > MaterialToPrimvarsUVSetNames;
	return ConvertGeomMesh( UsdSchema, MeshDescription, MaterialAssignments, AdditionalTransform, MaterialToPrimvarsUVSetNames, TimeCode, RenderContext );
}

bool UsdToUnreal::ConvertGeomMesh( const pxr::UsdTyped& UsdSchema, FMeshDescription& MeshDescription, UsdUtils::FUsdPrimMaterialAssignmentInfo& MaterialAssignments, const FTransform& AdditionalTransform,
	const TMap< FString, TMap< FString, int32 > >& MaterialToPrimvarsUVSetNames, const pxr::UsdTimeCode TimeCode, const pxr::TfToken& RenderContext )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( UsdToUnreal::ConvertGeomMesh );

	using namespace pxr;

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdGeomMesh UsdMesh( UsdSchema );
	pxr::UsdPrim UsdPrim = UsdMesh.GetPrim();
	if ( !UsdMesh || !UsdPrim )
	{
		return false;
	}

	pxr::UsdStageRefPtr Stage = UsdMesh.GetPrim().GetStage();
	const FUsdStageInfo StageInfo( Stage );

	const double TimeCodeValue = TimeCode.GetValue();

	// Material assignments
	const bool bProvideMaterialIndices = true;
	UsdUtils::FUsdPrimMaterialAssignmentInfo LocalInfo = UsdUtils::GetPrimMaterialAssignments( UsdPrim, TimeCode, bProvideMaterialIndices, RenderContext );
	MaterialAssignments.Slots.Append( LocalInfo.Slots ); // We always want to keep individual slots, even when collapsing

	TArray< UsdUtils::FUsdPrimMaterialSlot >& LocalMaterialSlots = LocalInfo.Slots;
	TArray< int32 >& FaceMaterialIndices = LocalInfo.MaterialIndices;

	const int32 VertexOffset = MeshDescription.Vertices().Num();
	const int32 VertexInstanceOffset = MeshDescription.VertexInstances().Num();
	const int32 PolygonOffset = MeshDescription.Polygons().Num();
	const int32 MaterialIndexOffset = MaterialAssignments.Slots.Num();

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

	uint32 NumSkippedPolygons = 0;
	uint32 NumPolygons = 0;

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
			NumPolygons = FaceCounts.size();
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
			int32 UVSetIndexUE; // The user may only have 'uv4' and 'uv5', so we can't just use array indices to find the target UV channel
			TOptional< VtIntArray > UVIndices; // UVs might be indexed or they might be flat (one per vertex)
			VtVec2fArray UVs;

			pxr::TfToken InterpType = UsdGeomTokens->faceVarying;
		};

		TArray< FUVSet > UVSets;

		TArray< TUsdStore< UsdGeomPrimvar > > PrimvarsByUVIndex = UsdUtils::GetUVSetPrimvars( UsdMesh, MaterialToPrimvarsUVSetNames );

		int32 HighestAddedUVChannel = 0;
		for ( int32 UVChannelIndex = 0; UVChannelIndex < PrimvarsByUVIndex.Num(); ++UVChannelIndex )
		{
			if ( !PrimvarsByUVIndex.IsValidIndex( UVChannelIndex ) )
			{
				break;
			}

			UsdGeomPrimvar& Primvar = PrimvarsByUVIndex[UVChannelIndex].Get();
			if ( !Primvar )
			{
				// The user may have name their UV sets 'uv4' and 'uv5', in which case we have no UV sets below 4, so just skip them
				continue;
			}

			FUVSet UVSet;
			UVSet.InterpType = Primvar.GetInterpolation();
			UVSet.UVSetIndexUE = UVChannelIndex;

			if ( Primvar.IsIndexed() )
			{
				UVSet.UVIndices.Emplace();

				if ( Primvar.GetIndices( &UVSet.UVIndices.GetValue(), TimeCode ) && Primvar.Get( &UVSet.UVs, TimeCode ) )
				{
					if ( UVSet.UVs.size() > 0 )
					{
						UVSets.Add( MoveTemp( UVSet ) );
						HighestAddedUVChannel = UVSet.UVSetIndexUE;
					}
				}
			}
			else
			{
				if ( Primvar.Get( &UVSet.UVs ) )
				{
					if ( UVSet.UVs.size() > 0 )
					{
						UVSets.Add( MoveTemp( UVSet ) );
						HighestAddedUVChannel = UVSet.UVSetIndexUE;
					}
				}
			}
		}

		// When importing multiple mesh pieces to the same static mesh.  Ensure each mesh piece has the same number of Uv's
		{
			int32 ExistingUVCount = MeshDescriptionUVs.GetNumIndices();
			int32 NumUVs = FMath::Max( HighestAddedUVChannel + 1, ExistingUVCount);
			NumUVs = FMath::Min<int32>(MAX_MESH_TEXTURE_COORDS_MD, NumUVs);
			// At least one UV set must exist.
			NumUVs = FMath::Max<int32>(1, NumUVs);

			//Make sure all Vertex instance have the correct number of UVs
			MeshDescriptionUVs.SetNumIndices(NumUVs);
		}

		TVertexInstanceAttributesRef< FVector > MeshDescriptionNormals = StaticMeshAttributes.GetVertexInstanceNormals();

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

		TPolygonGroupAttributesRef<FName> MaterialSlotNames = StaticMeshAttributes.GetPolygonGroupMaterialSlotNames();
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
					const int32 NormalIndex = UsdGeomMeshImpl::GetPrimValueIndex( NormalsInterpType, ControlPointIndex, CurrentVertexInstanceIndex, PolygonIndex );

					if ( NormalIndex < Normals.size() )
					{
						const GfVec3f& Normal = Normals[NormalIndex];
						FVector TransformedNormal = AdditionalTransform.TransformVector( UsdToUnreal::ConvertVector( StageInfo, Normal ) ).GetSafeNormal();

						MeshDescriptionNormals[AddedVertexInstanceId] = TransformedNormal.GetSafeNormal();
					}
				}

				for ( const FUVSet& UVSet : UVSets )
				{
					const int32 ValueIndex = UsdGeomMeshImpl::GetPrimValueIndex( UVSet.InterpType, ControlPointIndex, CurrentVertexInstanceIndex, PolygonIndex );

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
					MeshDescriptionUVs.Set( AddedVertexInstanceId, UVSet.UVSetIndexUE, FinalUVVector );
				}

				// Vertex color
				{
					const int32 ValueIndex = UsdGeomMeshImpl::GetPrimValueIndex( ColorInterpolation, ControlPointIndex, CurrentVertexInstanceIndex, PolygonIndex );

					GfVec3f UsdColor( 1.f, 1.f, 1.f );

					if ( !UsdColors.empty() && ensure( UsdColors.size() > ValueIndex ) )
					{
						UsdColor = UsdColors[ ValueIndex ];
					}

					MeshDescriptionColors[ AddedVertexInstanceId ] = UsdToUnreal::ConvertColor( UsdColor );
				}

				// Vertex opacity
				{
					const int32 ValueIndex = UsdGeomMeshImpl::GetPrimValueIndex( OpacityInterpolation, ControlPointIndex, CurrentVertexInstanceIndex, PolygonIndex );

					if ( !UsdOpacities.empty() && ensure( UsdOpacities.size() > ValueIndex ) )
					{
						MeshDescriptionColors[ AddedVertexInstanceId ][3] = UsdOpacities[ ValueIndex ];
					}
				}
			}

			// This polygon was using the same vertex instance more than once and we removed too many
			// vertex indices, so now we're forced to skip the whole polygon. We'll show a warning about it though
			if ( CornerVerticesIDs.Num() < 3 )
			{
				++NumSkippedPolygons;
				continue;
			}

			// Polygon groups
			int32 LocalMaterialIndex = 0;
			if ( FaceMaterialIndices.IsValidIndex( PolygonIndex ) )
			{
				LocalMaterialIndex = FaceMaterialIndices[ PolygonIndex ];
				if ( !LocalMaterialSlots.IsValidIndex( LocalMaterialIndex ) )
				{
					LocalMaterialIndex = 0;
				}
			}

			const int32 CombinedMaterialIndex = MaterialIndexOffset + LocalMaterialIndex;

			if ( !PolygonGroupMapping.Contains( CombinedMaterialIndex ) )
			{
				FPolygonGroupID NewPolygonGroup = MeshDescription.CreatePolygonGroup();
				PolygonGroupMapping.Add( CombinedMaterialIndex, NewPolygonGroup );

				// This is important for runtime, where the material slots are matched to LOD sections based on their material slot name
				MaterialSlotNames[ NewPolygonGroup ] = *LexToString( NewPolygonGroup.GetValue() );
			}

			// Insert a polygon into the mesh
			FPolygonGroupID PolygonGroupID = PolygonGroupMapping[ CombinedMaterialIndex ];
			const FPolygonID NewPolygonID = MeshDescription.CreatePolygon( PolygonGroupID, CornerInstanceIDs );

			if ( bFlipThisGeometry )
			{
				MeshDescription.ReversePolygonFacing( NewPolygonID );
			}
		}
	}

	if ( NumPolygons > 0 && NumSkippedPolygons > 0 )
	{
		UE_LOG( LogUsd, Warning, TEXT( "Skipped %d out of %d faces when parsing the mesh for prim '%s', as those faces contained too many repeated vertex indices" ),
			NumSkippedPolygons,
			NumPolygons,
			*UsdToUnreal::ConvertPath( UsdPrim.GetPath() )
		);
	}

	return true;
}

bool UsdToUnreal::ConvertDisplayColor( const UsdUtils::FDisplayColorMaterial& DisplayColorDescription, UMaterialInstanceConstant& Material )
{
	FUsdLogManager::LogMessage( EMessageSeverity::Warning, LOCTEXT( "DeprecatedConvertDisplayColor", "Converting existing instances with UsdToUnreal::ConvertDisplayColor is deprecated in favor of just calling UsdUtils::CreateDisplayColorMaterialInstanceConstant instead, and may be removed in a future release." ) );

#if WITH_EDITOR
	FString ParentPath = DisplayColorDescription.bHasOpacity
		? TEXT("Material'/USDImporter/Materials/DisplayColorAndOpacity.DisplayColorAndOpacity'")
		: TEXT("Material'/USDImporter/Materials/DisplayColor.DisplayColor'");

	if ( UMaterialInterface* ParentMaterial = Cast< UMaterialInterface >( FSoftObjectPath( ParentPath ).TryLoad() ) )
	{
		UMaterialEditingLibrary::SetMaterialInstanceParent( &Material, ParentMaterial );
	}

	if ( DisplayColorDescription.bIsDoubleSided )
	{
		Material.BasePropertyOverrides.bOverride_TwoSided = true;
		Material.BasePropertyOverrides.TwoSided = DisplayColorDescription.bIsDoubleSided;
		Material.PostEditChange();
	}
#endif // WITH_EDITOR

	return true;
}

UMaterialInstanceDynamic* UsdUtils::CreateDisplayColorMaterialInstanceDynamic( const UsdUtils::FDisplayColorMaterial& DisplayColorDescription )
{
	FString ParentPath;
	if ( DisplayColorDescription.bHasOpacity )
	{
		if ( DisplayColorDescription.bIsDoubleSided )
		{
			ParentPath = TEXT( "Material'/USDImporter/Materials/DisplayColorAndOpacityDoubleSided.DisplayColorAndOpacityDoubleSided'" );
		}
		else
		{
			ParentPath = TEXT( "Material'/USDImporter/Materials/DisplayColorAndOpacity.DisplayColorAndOpacity'" );
		}
	}
	else
	{
		if ( DisplayColorDescription.bIsDoubleSided )
		{
			ParentPath = TEXT( "Material'/USDImporter/Materials/DisplayColorDoubleSided.DisplayColorDoubleSided'" );
		}
		else
		{
			ParentPath = TEXT( "Material'/USDImporter/Materials/DisplayColor.DisplayColor'" );
		}
	}

	if ( UMaterialInterface* ParentMaterial = Cast< UMaterialInterface >( FSoftObjectPath( ParentPath ).TryLoad() ) )
	{
		if ( UMaterialInstanceDynamic* NewMaterial = UMaterialInstanceDynamic::Create( ParentMaterial, GetTransientPackage() ) )
		{
			return NewMaterial;
		}
	}

	return nullptr;
}

UMaterialInstanceConstant* UsdUtils::CreateDisplayColorMaterialInstanceConstant( const FDisplayColorMaterial& DisplayColorDescription )
{
#if WITH_EDITOR
	FString ParentPath;
	if ( DisplayColorDescription.bHasOpacity )
	{
		if ( DisplayColorDescription.bIsDoubleSided )
		{
			ParentPath = TEXT( "Material'/USDImporter/Materials/DisplayColorAndOpacityDoubleSided.DisplayColorAndOpacityDoubleSided'" );
		}
		else
		{
			ParentPath = TEXT( "Material'/USDImporter/Materials/DisplayColorAndOpacity.DisplayColorAndOpacity'" );
		}
	}
	else
	{
		if ( DisplayColorDescription.bIsDoubleSided )
		{
			ParentPath = TEXT( "Material'/USDImporter/Materials/DisplayColorDoubleSided.DisplayColorDoubleSided'" );
		}
		else
		{
			ParentPath = TEXT( "Material'/USDImporter/Materials/DisplayColor.DisplayColor'" );
		}
	}

	if ( UMaterialInterface* ParentMaterial = Cast< UMaterialInterface >( FSoftObjectPath( ParentPath ).TryLoad() ) )
	{
		if ( UMaterialInstanceConstant* MaterialInstance = NewObject< UMaterialInstanceConstant >( GetTransientPackage(), NAME_None, RF_NoFlags ) )
		{
			UMaterialEditingLibrary::SetMaterialInstanceParent( MaterialInstance, ParentMaterial );
			return MaterialInstance;
		}
	}
#endif // WITH_EDITOR
	return nullptr;
}

UsdUtils::FUsdPrimMaterialAssignmentInfo UsdUtils::GetPrimMaterialAssignments( const pxr::UsdPrim& UsdPrim, const pxr::UsdTimeCode TimeCode, bool bProvideMaterialIndices, const pxr::TfToken& RenderContext )
{
	if ( !UsdPrim )
	{
		return {};
	}

	auto FetchFirstUEMaterialFromAttribute = []( const pxr::UsdPrim& UsdPrim, const pxr::UsdTimeCode TimeCode ) -> TOptional<FString>
	{
		FString ValidPackagePath;
		if ( pxr::UsdAttribute MaterialAttribute = UsdPrim.GetAttribute( UnrealIdentifiers::MaterialAssignment ) )
		{
			std::string UEMaterial;
			if ( MaterialAttribute.Get( &UEMaterial, TimeCode ) && UEMaterial.size() > 0)
			{
				ValidPackagePath = UsdToUnreal::ConvertString( UEMaterial );
			}
		}
		else if ( pxr::UsdAttribute MaterialsAttribute = UsdPrim.GetAttribute( UnrealIdentifiers::MaterialAssignments ) )
		{
			pxr::VtStringArray UEMaterials;
			MaterialsAttribute.Get( &UEMaterials, TimeCode );

			if ( UEMaterials.size() > 0 && UEMaterials[ 0 ].size() > 0 )
			{
				ValidPackagePath = UsdToUnreal::ConvertString( UEMaterials[ 0 ] );

				UE_LOG( LogUsd, Warning, TEXT( "String array attribute 'unrealMaterials' is deprecated: Use the singular string 'unrealMaterial' attribute" ) );
				if ( UEMaterials.size() > 1 )
				{
					UE_LOG( LogUsd, Warning, TEXT( "Found more than one Unreal material assigned to Mesh '%s'. The first material ('%s') will be chosen, and the rest ignored." ),
						*UsdToUnreal::ConvertPath( UsdPrim.GetPath() ), *ValidPackagePath );
				}
			}
		}

		if ( !ValidPackagePath.IsEmpty() )
		{
			// We can't TryLoad() this right now as we may be in an Async thread, so settle for checking with the asset registry module
			FSoftObjectPath SoftObjectPath{ ValidPackagePath };
			if ( SoftObjectPath.IsValid() )
			{
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>( "AssetRegistry" );
				FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath( *ValidPackagePath );
				if ( AssetData.IsValid() && AssetData.GetClass()->IsChildOf( UMaterialInterface::StaticClass() ) )
				{
					return ValidPackagePath;
				}
			}

			UE_LOG( LogUsd, Warning, TEXT( "Could not find a valid material at path '%s', targetted by prim '%s's unrealMaterial attribute. Material assignment will fallback to USD materials and display color data." ),
				*ValidPackagePath, *UsdToUnreal::ConvertPath( UsdPrim.GetPath() ) );
		}

		return {};
	};

	auto FetchMaterialByComputingBoundMaterial = [ &RenderContext ]( const pxr::UsdPrim& UsdPrim ) -> TOptional<FString>
	{
		pxr::UsdShadeMaterialBindingAPI BindingAPI( UsdPrim );
		pxr::UsdShadeMaterial ShadeMaterial = BindingAPI.ComputeBoundMaterial();
		if ( !ShadeMaterial )
		{
			return {};
		}

		// Ignore this material if UsdToUnreal::ConvertMaterial would as well
		pxr::UsdShadeShader SurfaceShader = ShadeMaterial.ComputeSurfaceSource( RenderContext );
		if ( !SurfaceShader )
		{
			return {};
		}

		pxr::UsdPrim ShadeMaterialPrim = ShadeMaterial.GetPrim();
		if ( ShadeMaterialPrim )
		{
			pxr::SdfPath Path = ShadeMaterialPrim.GetPath();
			std::string ShadingEngineName = ( ShadeMaterialPrim ? ShadeMaterialPrim.GetPrim() : UsdPrim.GetPrim() ).GetPrimPath().GetString();
			if(ShadingEngineName.size() > 0 )
			{
				return UsdToUnreal::ConvertString( ShadingEngineName );
			}
		}

		return {};
	};

	auto FetchMaterialByMaterialRelationship = [ &RenderContext ]( const pxr::UsdPrim& UsdPrim ) -> TOptional<FString>
	{
		if ( pxr::UsdRelationship Relationship = UsdPrim.GetRelationship( pxr::UsdShadeTokens->materialBinding ) )
		{
			pxr::SdfPathVector Targets;
			Relationship.GetTargets( &Targets );

			if ( Targets.size() > 0 )
			{
				const pxr::SdfPath& TargetMaterialPrimPath = Targets[0];
				pxr::UsdPrim MaterialPrim = UsdPrim.GetStage()->GetPrimAtPath( TargetMaterialPrimPath );
				pxr::UsdShadeMaterial UsdShadeMaterial{ MaterialPrim };
				if ( !UsdShadeMaterial )
				{
					FUsdLogManager::LogMessage(
						EMessageSeverity::Warning,
						FText::Format( LOCTEXT( "IgnoringMaterialInvalid", "Ignoring material '{0}' bound to prim '{1}' as it does not possess the UsdShadeMaterial schema" ),
							FText::FromString( UsdToUnreal::ConvertPath( TargetMaterialPrimPath ) ),
							FText::FromString( UsdToUnreal::ConvertPath( UsdPrim.GetPath() ) )
						)
					);
					return {};
				}

				// Ignore this material if UsdToUnreal::ConvertMaterial would as well
				pxr::UsdShadeShader SurfaceShader = UsdShadeMaterial.ComputeSurfaceSource( RenderContext );
				if ( !SurfaceShader )
				{
					FUsdLogManager::LogMessage(
						EMessageSeverity::Warning,
						FText::Format( LOCTEXT( "IgnoringMaterialSurface", "Ignoring material '{0}' bound to prim '{1}' as it contains no valid surface shader source" ),
							FText::FromString( UsdToUnreal::ConvertPath( TargetMaterialPrimPath ) ),
							FText::FromString( UsdToUnreal::ConvertPath( UsdPrim.GetPath() ) )
						)
					);
					return {};
				}

				FString MaterialPrimPath = UsdToUnreal::ConvertPath( TargetMaterialPrimPath );
				if ( Targets.size() > 1 )
				{
					FUsdLogManager::LogMessage(
						EMessageSeverity::Warning,
						FText::Format( LOCTEXT( "MoreThanOneMaterialBinding", "Found more than on material:binding targets on prim '{0}'. The first material ('{1}') will be used, and the rest ignored." ),
							FText::FromString( UsdToUnreal::ConvertPath( UsdPrim.GetPath() ) ),
							FText::FromString( MaterialPrimPath )
						)
					);
				}

				return MaterialPrimPath;
			}
		}

		return {};
	};

	FUsdPrimMaterialAssignmentInfo Result;

	uint64 NumFaces = 0;
	{
		pxr::UsdGeomMesh Mesh = pxr::UsdGeomMesh( UsdPrim );
		pxr::UsdAttribute FaceCounts = Mesh.GetFaceVertexCountsAttr();
		if ( !Mesh || !FaceCounts )
		{
			return Result;
		}

		pxr::VtArray<int> FaceVertexCounts;
		FaceCounts.Get( &FaceVertexCounts, TimeCode );
		NumFaces = FaceVertexCounts.size();
		if ( NumFaces < 1 )
		{
			return Result;
		}

		if ( bProvideMaterialIndices )
		{
			Result.MaterialIndices.SetNumZeroed( NumFaces );
		}
	}

	// Priority 1: unrealMaterial attribute directly on the prim
	if ( TOptional<FString> UnrealMaterial = FetchFirstUEMaterialFromAttribute( UsdPrim, TimeCode ) )
	{
		FUsdPrimMaterialSlot& Slot = Result.Slots.Emplace_GetRef();
		Slot.MaterialSource = UnrealMaterial.GetValue();
		Slot.AssignmentType = UsdUtils::EPrimAssignmentType::UnrealMaterial;

		return Result;
	}

	// Priority 2: material binding directly on the prim
	if ( TOptional<FString> BoundMaterial = FetchMaterialByComputingBoundMaterial( UsdPrim ) )
	{
		FUsdPrimMaterialSlot& Slot = Result.Slots.Emplace_GetRef();
		Slot.MaterialSource = BoundMaterial.GetValue();
		Slot.AssignmentType = UsdUtils::EPrimAssignmentType::MaterialPrim;

		return Result;
	}

	// Priority 3: material:binding relationship directly on the prim (not sure why this is a separate step, but it came from IUsdPrim::GetGeometryMaterials. I bumped it in priority as the GeomSubsets do the same)
	if ( TOptional<FString> TargetMaterial = FetchMaterialByMaterialRelationship( UsdPrim ) )
	{
		FUsdPrimMaterialSlot& Slot = Result.Slots.Emplace_GetRef();
		Slot.MaterialSource = TargetMaterial.GetValue();
		Slot.AssignmentType = UsdUtils::EPrimAssignmentType::MaterialPrim;

		return Result;
	}

	TOptional<FDisplayColorMaterial> DisplayColor = ExtractDisplayColorMaterial( pxr::UsdGeomMesh( UsdPrim ), TimeCode );

	// Priority 4: GeomSubset partitions
	std::vector<pxr::UsdGeomSubset> GeomSubsets = pxr::UsdShadeMaterialBindingAPI( UsdPrim ).GetMaterialBindSubsets();
	if ( GeomSubsets.size() > 0 )
	{
		// We need to do this even if we won't provide indices because we may create an additional slot for unassigned polygons
		pxr::VtIntArray UnassignedIndices;
		std::string ReasonWhyNotPartition;
		bool ValidPartition = pxr::UsdGeomSubset::ValidateSubsets( GeomSubsets, NumFaces, pxr::UsdGeomTokens->partition, &ReasonWhyNotPartition );
		if ( !ValidPartition )
		{
			UE_LOG( LogUsd, Warning, TEXT( "Found an invalid GeomSubsets partition in prim '%s': %s" ),
				*UsdToUnreal::ConvertPath( UsdPrim.GetPath() ), *UsdToUnreal::ConvertString( ReasonWhyNotPartition ));
			UnassignedIndices = pxr::UsdGeomSubset::GetUnassignedIndices( GeomSubsets, NumFaces );
		}

		for ( uint32 GeomSubsetIndex = 0; GeomSubsetIndex < GeomSubsets.size(); ++GeomSubsetIndex )
		{
			const pxr::UsdGeomSubset& GeomSubset = GeomSubsets[ GeomSubsetIndex ];
			bool bHasAssignment = false;

			// Priority 4.1: unrealMaterial partitions
			if ( TOptional<FString> UnrealMaterial = FetchFirstUEMaterialFromAttribute( GeomSubset.GetPrim(), TimeCode ) )
			{
				FUsdPrimMaterialSlot& Slot = Result.Slots.Emplace_GetRef();
				Slot.MaterialSource = UnrealMaterial.GetValue();
				Slot.AssignmentType = UsdUtils::EPrimAssignmentType::UnrealMaterial;
				bHasAssignment = true;
			}

			// Priority 4.2: computing bound material
			if ( !bHasAssignment )
			{
				if ( TOptional<FString> BoundMaterial = FetchMaterialByComputingBoundMaterial( GeomSubset.GetPrim() ) )
				{
					FUsdPrimMaterialSlot& Slot = Result.Slots.Emplace_GetRef();
					Slot.MaterialSource = BoundMaterial.GetValue();
					Slot.AssignmentType = UsdUtils::EPrimAssignmentType::MaterialPrim;
					bHasAssignment = true;
				}
			}

			// Priority 4.3: material:binding relationship
			if ( !bHasAssignment )
			{
				if ( TOptional<FString> TargetMaterial = FetchMaterialByMaterialRelationship( GeomSubset.GetPrim() ) )
				{
					FUsdPrimMaterialSlot& Slot = Result.Slots.Emplace_GetRef();
					Slot.MaterialSource = TargetMaterial.GetValue();
					Slot.AssignmentType = UsdUtils::EPrimAssignmentType::MaterialPrim;
					bHasAssignment = true;
				}
			}

			// Priority 4.4: Create a section anyway so it becomes its own slot. Assign displayColor if we have one
			if ( !bHasAssignment )
			{
				FUsdPrimMaterialSlot& Slot = Result.Slots.Emplace_GetRef();
				if ( DisplayColor )
				{
					Slot.MaterialSource = DisplayColor.GetValue().ToString();
					Slot.AssignmentType = UsdUtils::EPrimAssignmentType::DisplayColor;
				}
				bHasAssignment = true;
			}

			pxr::VtIntArray PolygonIndicesInSubset;
			GeomSubset.GetIndicesAttr().Get( &PolygonIndicesInSubset, TimeCode );

			if ( bProvideMaterialIndices )
			{
				int32 LastAssignmentIndex = Result.Slots.Num() - 1;
				for ( int PolygonIndex : PolygonIndicesInSubset )
				{
					Result.MaterialIndices[ PolygonIndex ] = LastAssignmentIndex;
				}
			}
		}

		// Extra slot for unassigned polygons
		if ( UnassignedIndices.size() > 0 )
		{
			FUsdPrimMaterialSlot& Slot = Result.Slots.Emplace_GetRef();
			if ( DisplayColor )
			{
				Slot.MaterialSource = DisplayColor.GetValue().ToString();
				Slot.AssignmentType = UsdUtils::EPrimAssignmentType::DisplayColor;
			}

			if ( bProvideMaterialIndices )
			{
				int32 LastAssignmentIndex = Result.Slots.Num() - 1;
				for ( int PolygonIndex : UnassignedIndices )
				{
					Result.MaterialIndices[ PolygonIndex ] = LastAssignmentIndex;
				}
			}
		}

		return Result;
	}

	// Priority 5: vertex color material using displayColor/displayOpacity information for the entire mesh
	if ( GeomSubsets.size() == 0 && DisplayColor )
	{
		FUsdPrimMaterialSlot& Slot = Result.Slots.Emplace_GetRef();
		Slot.MaterialSource = DisplayColor.GetValue().ToString();
		Slot.AssignmentType = UsdUtils::EPrimAssignmentType::DisplayColor;

		return Result;
	}

	// Priority 6: Make sure there is always at least one slot, even if empty
	if ( Result.Slots.Num() < 1 )
	{
		Result.Slots.Emplace();
	}
	return Result;
}

bool UnrealToUsd::ConvertStaticMesh( const UStaticMesh* StaticMesh, pxr::UsdPrim& UsdPrim, const pxr::UsdTimeCode TimeCode, UE::FUsdStage* StageForMaterialAssignments )
{
	FScopedUsdAllocs UsdAllocs;

	pxr::UsdStageRefPtr Stage = UsdPrim.GetStage();
	if ( !Stage )
	{
		return false;
	}

	const FUsdStageInfo StageInfo( Stage );

	int32 NumLODs = StaticMesh->GetNumLODs();
	if ( NumLODs < 1 )
	{
		return false;
	}

	pxr::UsdVariantSets VariantSets = UsdPrim.GetVariantSets();
	if ( NumLODs > 1 && VariantSets.HasVariantSet( UnrealIdentifiers::LOD ) )
	{
		UE_LOG( LogUsd, Error, TEXT("Failed to export higher LODs for mesh '%s', as the target prim already has a variant set named '%s'!"), *StaticMesh->GetName(), *UsdToUnreal::ConvertToken( UnrealIdentifiers::LOD ) );
		NumLODs = 1;
	}

	bool bExportMultipleLODs = NumLODs > 1;

	pxr::SdfPath ParentPrimPath = UsdPrim.GetPath();
	std::string LowestLODAdded = "";

	// Collect all material assignments, referenced by the sections' material indices
	bool bHasMaterialAssignments = false;
	pxr::VtArray< std::string > MaterialAssignments;
	for(const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials())
	{
		FString AssignedMaterialPathName;
		if ( UMaterialInterface* Material = StaticMaterial.MaterialInterface )
		{
			if ( Material->GetOutermost() != GetTransientPackage() )
			{
				AssignedMaterialPathName = Material->GetPathName();
				bHasMaterialAssignments = true;
			}
		}

		MaterialAssignments.push_back( UnrealToUsd::ConvertString( *AssignedMaterialPathName ).Get() );
	}
	if ( !bHasMaterialAssignments )
	{
		// Prevent creation of the unrealMaterials attribute in case we don't have any assignments at all
		MaterialAssignments.clear();
	}

	for ( int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex )
	{
		const FStaticMeshLODResources& RenderMesh = StaticMesh->GetLODForExport( LODIndex );

		// Verify the integrity of the static mesh.
		if ( RenderMesh.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices() == 0 )
		{
			continue;
		}

		if ( RenderMesh.Sections.Num() == 0 )
		{
			continue;
		}

		// LOD0, LOD1, etc
		std::string VariantName = UnrealIdentifiers::LOD.GetString() + UnrealToUsd::ConvertString( *LexToString( LODIndex ) ).Get();
		if ( LowestLODAdded.size() == 0 )
		{
			LowestLODAdded = VariantName;
		}

		pxr::SdfPath LODPrimPath = ParentPrimPath.AppendPath(pxr::SdfPath(VariantName));

		// Enable the variant edit context, if we are creating variant LODs
		TOptional< pxr::UsdEditContext > EditContext;
		if ( bExportMultipleLODs )
		{
			pxr::UsdVariantSet VariantSet = VariantSets.GetVariantSet( UnrealIdentifiers::LOD );

			if ( !VariantSet.AddVariant( VariantName ) )
			{
				continue;
			}

			VariantSet.SetVariantSelection( VariantName );
			EditContext.Emplace( VariantSet.GetVariantEditContext() );
		}

		// Author material bindings on the dedicated stage if we have one
		pxr::UsdStageRefPtr MaterialStage;
		if ( StageForMaterialAssignments )
		{
			MaterialStage = *StageForMaterialAssignments;
		}
		else
		{
			MaterialStage = Stage;
		}

		pxr::UsdGeomMesh TargetMesh;
		pxr::UsdPrim MaterialPrim = UsdPrim;
		if ( bExportMultipleLODs )
		{
			// Add the mesh data to a child prim with the Mesh schema
			pxr::UsdPrim UsdLODPrim = Stage->DefinePrim( LODPrimPath, UnrealToUsd::ConvertToken( TEXT("Mesh") ).Get() );
			TargetMesh = pxr::UsdGeomMesh{ UsdLODPrim };

			MaterialPrim = MaterialStage->OverridePrim( LODPrimPath );
		}
		else
		{
			// Make sure the parent prim has the Mesh schema and add the mesh data directly to it
			UsdPrim = Stage->DefinePrim( UsdPrim.GetPath(), UnrealToUsd::ConvertToken( TEXT("Mesh") ).Get() );
			TargetMesh = pxr::UsdGeomMesh{ UsdPrim };

			MaterialPrim = MaterialStage->OverridePrim( UsdPrim.GetPath() );
		}

		UsdGeomMeshImpl::ConvertStaticMeshLOD( LODIndex, RenderMesh, TargetMesh, MaterialAssignments, TimeCode, MaterialPrim );
	}

	// Reset variant set to start with the lowest lod selected
	if ( bExportMultipleLODs )
	{
		VariantSets.GetVariantSet(UnrealIdentifiers::LOD).SetVariantSelection(LowestLODAdded);
	}

	return true;
}

bool UnrealToUsd::ConvertMeshDescriptions( const TArray<FMeshDescription>& LODIndexToMeshDescription, pxr::UsdPrim& UsdPrim, const FMatrix& AdditionalTransform, const pxr::UsdTimeCode TimeCode )
{
	FScopedUsdAllocs UsdAllocs;

	pxr::UsdStageRefPtr Stage = UsdPrim.GetStage();
	if ( !Stage )
	{
		return false;
	}

	const FUsdStageInfo StageInfo( Stage );

	int32 NumLODs = LODIndexToMeshDescription.Num();
	if ( NumLODs < 1 )
	{
		return false;
	}

	pxr::UsdVariantSets VariantSets = UsdPrim.GetVariantSets();
	if ( NumLODs > 1 && VariantSets.HasVariantSet( UnrealIdentifiers::LOD ) )
	{
		UE_LOG( LogUsd, Error, TEXT( "Failed to convert higher mesh description LODs for prim '%s', as the target prim already has a variant set named '%s'!" ),
			*UsdToUnreal::ConvertPath( UsdPrim.GetPath() ),
			*UsdToUnreal::ConvertToken( UnrealIdentifiers::LOD )
		);
		NumLODs = 1;
	}

	bool bExportMultipleLODs = NumLODs > 1;

	pxr::SdfPath ParentPrimPath = UsdPrim.GetPath();
	std::string LowestLODAdded = "";

	for ( int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex )
	{
		const FMeshDescription& MeshDescription = LODIndexToMeshDescription[ LODIndex ];

		// LOD0, LOD1, etc
		std::string VariantName = UnrealIdentifiers::LOD.GetString() + UnrealToUsd::ConvertString( *LexToString( LODIndex ) ).Get();
		if ( LowestLODAdded.size() == 0 )
		{
			LowestLODAdded = VariantName;
		}

		pxr::SdfPath LODPrimPath = ParentPrimPath.AppendPath( pxr::SdfPath( VariantName ) );

		// Enable the variant edit context, if we are creating variant LODs
		TOptional< pxr::UsdEditContext > EditContext;
		if ( bExportMultipleLODs )
		{
			pxr::UsdVariantSet VariantSet = VariantSets.GetVariantSet( UnrealIdentifiers::LOD );
			if ( !VariantSet.AddVariant( VariantName ) )
			{
				continue;
			}

			VariantSet.SetVariantSelection( VariantName );
			EditContext.Emplace( VariantSet.GetVariantEditContext() );
		}

		pxr::UsdGeomMesh TargetMesh;
		if ( bExportMultipleLODs )
		{
			// Add the mesh data to a child prim with the Mesh schema
			pxr::UsdPrim UsdLODPrim = Stage->DefinePrim( LODPrimPath, UnrealToUsd::ConvertToken( TEXT( "Mesh" ) ).Get() );
			TargetMesh = pxr::UsdGeomMesh{ UsdLODPrim };
		}
		else
		{
			// Make sure the parent prim has the Mesh schema and add the mesh data directly to it
			UsdPrim = Stage->DefinePrim( UsdPrim.GetPath(), UnrealToUsd::ConvertToken( TEXT( "Mesh" ) ).Get() );
			TargetMesh = pxr::UsdGeomMesh{ UsdPrim };
		}

		if ( !UsdGeomMeshImpl::ConvertMeshDescription( MeshDescription, TargetMesh, AdditionalTransform, TimeCode ) )
		{
			return false;
		}
	}

	// Reset variant set to start with the lowest lod selected
	if ( bExportMultipleLODs )
	{
		VariantSets.GetVariantSet( UnrealIdentifiers::LOD ).SetVariantSelection( LowestLODAdded );
	}

	return true;
}

FString UsdUtils::FDisplayColorMaterial::ToString()
{
	return FString::Printf( TEXT("%s_%d_%d"), *UsdGeomMeshImpl::DisplayColorID, bHasOpacity, bIsDoubleSided );
}

TOptional<UsdUtils::FDisplayColorMaterial> UsdUtils::FDisplayColorMaterial::FromString( const FString& DisplayColorString )
{
	TArray<FString> Tokens;
	DisplayColorString.ParseIntoArray( Tokens, TEXT( "_" ) );

	if ( Tokens.Num() != 3 || Tokens[ 0 ] != UsdGeomMeshImpl::DisplayColorID )
	{
		return {};
	}

	UsdUtils::FDisplayColorMaterial Result;
	Result.bHasOpacity = static_cast< bool >( FCString::Atoi( *Tokens[ 1 ] ) );
	Result.bIsDoubleSided = static_cast< bool >( FCString::Atoi( *Tokens[ 2 ] ) );
	return Result;
}

TOptional<UsdUtils::FDisplayColorMaterial> UsdUtils::ExtractDisplayColorMaterial( const pxr::UsdGeomMesh& UsdMesh, const pxr::UsdTimeCode TimeCode )
{
	if ( !UsdMesh )
	{
		return {};
	}

	if ( !UsdMesh.GetDisplayOpacityAttr().IsDefined() && !UsdMesh.GetDisplayColorAttr().IsDefined() )
	{
		return {};
	}

	UsdUtils::FDisplayColorMaterial Desc;

	// Opacity
	pxr::VtArray< float > UsdOpacities = UsdUtils::GetUsdValue< pxr::VtArray< float > >( UsdMesh.GetDisplayOpacityAttr(), TimeCode );
	for ( float Opacity : UsdOpacities )
	{
		Desc.bHasOpacity = !FMath::IsNearlyEqual( Opacity, 1.f );
		if ( Desc.bHasOpacity )
		{
			break;
		}
	}

	// Double-sided
	if ( UsdMesh.GetDoubleSidedAttr().IsDefined() )
	{
		Desc.bIsDoubleSided = UsdUtils::GetUsdValue< bool >( UsdMesh.GetDoubleSidedAttr(), TimeCode );
	}

	return Desc;
}

bool UsdUtils::IsGeomMeshALOD( const pxr::UsdPrim& UsdMeshPrim )
{
	pxr::UsdGeomMesh UsdMesh{ UsdMeshPrim };
	if ( !UsdMesh )
	{
		return false;
	}

	FScopedUsdAllocs Allocs;

	pxr::UsdPrim ParentPrim = UsdMeshPrim.GetParent();
	if ( !ParentPrim )
	{
		return false;
	}

	const std::string LODString = UnrealIdentifiers::LOD.GetString();

	pxr::UsdVariantSets VariantSets = ParentPrim.GetVariantSets();
	if ( !VariantSets.HasVariantSet( LODString ) )
	{
		return false;
	}

	std::string Selection = VariantSets.GetVariantSet( LODString ).GetVariantSelection();
	int32 LODIndex = UsdGeomMeshImpl::GetLODIndexFromName( Selection );
	if ( LODIndex == INDEX_NONE )
	{
		return false;
	}

	return true;
}

int32 UsdUtils::GetNumberOfLODVariants( const pxr::UsdPrim& Prim )
{
	FScopedUsdAllocs Allocs;

	const std::string LODString = UnrealIdentifiers::LOD.GetString();

	pxr::UsdVariantSets VariantSets = Prim.GetVariantSets();
	if ( !VariantSets.HasVariantSet( LODString ) )
	{
		return 1;
	}

	return VariantSets.GetVariantSet( LODString ).GetVariantNames().size();
}

bool UsdUtils::IterateLODMeshes( const pxr::UsdPrim& ParentPrim, TFunction<bool( const pxr::UsdGeomMesh & LODMesh, int32 LODIndex )> Func )
{
	if ( !ParentPrim )
	{
		return false;
	}

	FScopedUsdAllocs Allocs;

	const std::string LODString = UnrealIdentifiers::LOD.GetString();

	pxr::UsdVariantSets VariantSets = ParentPrim.GetVariantSets();
	if ( !VariantSets.HasVariantSet( LODString ) )
	{
		return false;
	}

	pxr::UsdVariantSet LODVariantSet = VariantSets.GetVariantSet( LODString );
	const std::string OriginalVariant = LODVariantSet.GetVariantSelection();

	pxr::UsdStageRefPtr Stage = ParentPrim.GetStage();
	pxr::UsdEditContext( Stage, Stage->GetRootLayer() );

	bool bHasValidVariant = false;
	for ( const std::string& LODVariantName : VariantSets.GetVariantSet( LODString ).GetVariantNames() )
	{
		int32 LODIndex = UsdGeomMeshImpl::GetLODIndexFromName( LODVariantName );
		if ( LODIndex == INDEX_NONE )
		{
			continue;
		}

		LODVariantSet.SetVariantSelection( LODVariantName );

		pxr::UsdGeomMesh LODMesh;
		pxr::TfToken TargetChildNameToken{ LODVariantName };

		// Search for our LOD child mesh
		pxr::UsdPrimSiblingRange PrimRange = ParentPrim.GetChildren();
		for ( pxr::UsdPrimSiblingRange::iterator PrimRangeIt = PrimRange.begin(); PrimRangeIt != PrimRange.end(); ++PrimRangeIt )
		{
			const pxr::UsdPrim& Child = *PrimRangeIt;
			if ( pxr::UsdGeomMesh ChildMesh{ Child } )
			{
				if ( Child.GetName() == TargetChildNameToken )
				{
					LODMesh = ChildMesh;
					// Don't break here so we can show warnings if the user has other prims here (that we may end up ignoring)
					// USD doesn't allow name collisions anyway, so there won't be any other prim named TargetChildNameToken
				}
				else
				{
					UE_LOG(LogUsd, Warning, TEXT("Unexpected prim '%s' inside LOD variant '%s'. For automatic parsing of LODs, each LOD variant should contain only a single Mesh prim named the same as the variant!"),
						*UsdToUnreal::ConvertPath( Child.GetPath() ),
						*UsdToUnreal::ConvertString( LODVariantName )
					);
				}
			}
		}
		if ( !LODMesh )
		{
			continue;
		}

		bHasValidVariant = true;

		bool bContinue = Func(LODMesh, LODIndex);
		if ( !bContinue )
		{
			break;
		}
	}

	LODVariantSet.SetVariantSelection( OriginalVariant );
	return bHasValidVariant;
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK