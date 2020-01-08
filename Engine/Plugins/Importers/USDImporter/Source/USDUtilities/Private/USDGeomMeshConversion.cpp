// Copyright Epic Games, Inc. All Rights Reserved.

#if USE_USD_SDK

#include "USDGeomMeshConversion.h"

#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "Engine/StaticMesh.h"
#include "Factories/TextureFactory.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

#include "USDIncludesStart.h"

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
	using namespace pxr;

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdStageRefPtr Stage = UsdMesh.GetPrim().GetStage();
	const pxr::TfToken StageUpAxis = UsdUtils::GetUsdStageAxis( Stage );

	const double TimeCodeValue = TimeCode.GetValue();

	TTuple< TArray< FString >, TArray< int32 > > GeometryMaterials = IUsdPrim::GetGeometryMaterials( TimeCodeValue, UsdMesh.GetPrim() );
	TArray< FString >& MaterialNames = GeometryMaterials.Key;
	TArray< int32 >& FaceMaterialIndices = GeometryMaterials.Value;

	int32 VertexOffset = MeshDescription.Vertices().Num();
	int32 VertexInstanceOffset = MeshDescription.VertexInstances().Num();
	int32 PolygonOffset = MeshDescription.Polygons().Num();
	int32 MaterialIndexOffset = 0; //Materials.Num();

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

				FVector Pos = UsdToUnreal::ConvertVector( StageUpAxis, Point );

				FVertexID AddedVertexId = MeshDescription.CreateVertex();
				MeshDescriptionVertexPositions[ AddedVertexId ] = Pos;
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

		MeshDescription.ReserveNewVertexInstances( FaceCounts.size() * 3 );
		MeshDescription.ReserveNewPolygons( FaceCounts.size() );
		MeshDescription.ReserveNewEdges( FaceCounts.size() * 2 );

		// Vertex color
		TVertexInstanceAttributesRef< FVector4 > MeshDescriptionColors = StaticMeshAttributes.GetVertexInstanceColors();

		UsdGeomPrimvar ColorPrimvar = UsdMesh.GetDisplayColorPrimvar();
		pxr::VtArray< pxr::GfVec3f > UsdColors;

		if ( ColorPrimvar )
		{	
			ColorPrimvar.ComputeFlattened( &UsdColors, TimeCode );
		}

		// Vertex opacity
		UsdGeomPrimvar OpacityPrimvar = UsdMesh.GetDisplayOpacityPrimvar();
		pxr::VtArray< float > UsdOpacities;

		if ( OpacityPrimvar )
		{
			OpacityPrimvar.ComputeFlattened( &UsdOpacities );
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
					const int32 NormalIndex = Normals.size() != FaceIndices.size() ? FaceIndices[CurrentVertexInstanceIndex] : CurrentVertexInstanceIndex;
					check(NormalIndex < Normals.size());
					const GfVec3f& Normal = Normals[NormalIndex];
					FVector TransformedNormal = UsdToUnreal::ConvertVector( StageUpAxis, Normal );

					ensure( !TransformedNormal.IsNearlyZero() );
					MeshDescriptionNormals[AddedVertexInstanceId] = TransformedNormal.GetSafeNormal();
				}

				int32 UVLayerIndex = 0;
				for ( const FUVSet& UVSet : UVSets )
				{
					const int32 ValueIndex = UsdToUnrealImpl::GetPrimValueIndex( UVSet.InterpType, VertexID.GetValue(), CurrentVertexInstanceIndex, PolygonIndex );

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

					const int32 ValueIndex = UsdToUnrealImpl::GetPrimValueIndex( ColorPrimvar.GetInterpolation(), VertexID.GetValue(), CurrentVertexInstanceIndex, PolygonIndex );

					GfVec3f UsdColor( 1.f, 1.f, 1.f );

					if ( !UsdColors.empty() && ensure( UsdColors.size() > ValueIndex ) )
					{
						UsdColor = UsdColors[ ValueIndex ];
					}

					MeshDescriptionColors[ AddedVertexInstanceId ] = ConvertToLinear( UsdColor );
				}

				// Vertex opacity
				{
					const int32 ValueIndex = UsdToUnrealImpl::GetPrimValueIndex( OpacityPrimvar.GetInterpolation(), VertexID.GetValue(), CurrentVertexInstanceIndex, PolygonIndex );

					if ( !UsdOpacities.empty() && ensure( UsdOpacities.size() > ValueIndex ) )
					{
						MeshDescriptionColors[ AddedVertexInstanceId ][3] = UsdOpacities[ ValueIndex ];
					}
				}
			}

			int32 MaterialIndex = 0;
			if (PolygonIndex >= 0 && PolygonIndex < FaceMaterialIndices.Num())
			{
				MaterialIndex = FaceMaterialIndices[PolygonIndex];
				if (MaterialIndex < 0 || MaterialIndex > MaterialNames.Num())
				{
					MaterialIndex = 0;
				}
			}

			int32 RealMaterialIndex = MaterialIndexOffset + MaterialIndex;
			if (!PolygonGroupMapping.Contains(RealMaterialIndex))
			{
				FName ImportedMaterialSlotName;
				if (MaterialIndex >= 0 && MaterialIndex < MaterialNames.Num())
				{
					FString MaterialName = MaterialNames[MaterialIndex];
					ImportedMaterialSlotName = FName(*MaterialName);
					//Materials[RealMaterialIndex].Name = MaterialName;
				}

				FPolygonGroupID ExistingPolygonGroup = FPolygonGroupID::Invalid;
				for (const FPolygonGroupID PolygonGroupID : MeshDescription.PolygonGroups().GetElementIDs())
				{
					if (PolygonGroupImportedMaterialSlotNames[PolygonGroupID] == ImportedMaterialSlotName)
					{
						ExistingPolygonGroup = PolygonGroupID;
						break;
					}
				}
				if (ExistingPolygonGroup == FPolygonGroupID::Invalid)
				{
					ExistingPolygonGroup = MeshDescription.CreatePolygonGroup();
					PolygonGroupImportedMaterialSlotNames[ExistingPolygonGroup] = ImportedMaterialSlotName;
				}
				PolygonGroupMapping.Add(RealMaterialIndex, ExistingPolygonGroup);
			}

			FPolygonGroupID PolygonGroupID = PolygonGroupMapping[RealMaterialIndex];
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

namespace UsdGeomMeshConversionImpl
{
	UMaterialExpression* ParseInputTexture( pxr::UsdShadeInput& ShadeInput, UMaterial& Material )
	{
		UMaterialExpression* Result = nullptr;

		FScopedUsdAllocs UsdAllocs;

		pxr::UsdShadeConnectableAPI Source;
		pxr::TfToken SourceName;
		pxr::UsdShadeAttributeType AttributeType;

		if ( ShadeInput.GetConnectedSource( &Source, &SourceName, &AttributeType ) )
		{
			//if ( SourceName == pxr::TfToken( "UsdUVTexture" ) /*pxr::UsdImagingTokens->UsdUVTexture*/ )
			{
				pxr::UsdShadeInput FileInput = Source.GetInput( pxr::TfToken("file") );

				if ( FileInput )
				{
					pxr::SdfAssetPath TexturePath;
					FileInput.Get< pxr::SdfAssetPath >( &TexturePath );

					FScopedUnrealAllocs UnrealAllocs;

					FString TextureFileName = UsdToUnreal::ConvertString( TexturePath.GetResolvedPath() );
					TextureFileName.RemoveFromStart( TEXT("\\\\?\\") );

					FPaths::NormalizeFilename( TextureFileName );

					bool bOutCancelled = false;
					UTextureFactory* TextureFactory = NewObject< UTextureFactory >();
					TextureFactory->SuppressImportOverwriteDialog();

					UTexture2D* Texture2D = Cast< UTexture2D >( TextureFactory->FactoryCreateFile( UTexture2D::StaticClass(), GetTransientPackage(), NAME_None, RF_Transient, TextureFileName, TEXT(""), GWarn, bOutCancelled ) );

					if ( Texture2D )
					{
						UMaterialExpressionTextureSample* TextureSample = Cast< UMaterialExpressionTextureSample >( UMaterialEditingLibrary::CreateMaterialExpression( &Material, UMaterialExpressionTextureSample::StaticClass() ) );
						TextureSample->Texture = Texture2D;
						TextureSample->SamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture( Texture2D );

						Result = TextureSample;
					}
				}
			}
		}

		return Result;
	}
}

bool UsdToUnreal::ConvertMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterial& Material )
{
	pxr::UsdShadeShader SurfaceShader = UsdShadeMaterial.ComputeSurfaceSource();
	
	if ( !SurfaceShader )
	{
		return false;
	}

	bool bHasMaterialInfo = false;

	auto ParseFloatInput = [ &Material, &SurfaceShader ]( const ANSICHAR* InputName, float DefaultValue ) -> UMaterialExpression*
	{
		pxr::UsdShadeInput Input = SurfaceShader.GetInput( pxr::TfToken(InputName) );
		UMaterialExpression* InputExpression = nullptr;

		if ( Input )
		{
			if ( !Input.HasConnectedSource() )
			{
				float InputValue = DefaultValue;
				Input.Get< float >( &InputValue );

				if ( !FMath::IsNearlyEqual( InputValue, DefaultValue ) )
				{
					UMaterialExpressionConstant* ExpressionConstant = Cast< UMaterialExpressionConstant >( UMaterialEditingLibrary::CreateMaterialExpression( &Material, UMaterialExpressionConstant::StaticClass() ) );
					ExpressionConstant->R = InputValue;

					InputExpression = ExpressionConstant;
				}
			}
			else
			{
				InputExpression = UsdGeomMeshConversionImpl::ParseInputTexture( Input, Material );
			}
		}

		return InputExpression;
	};

	// Base color
	{
		pxr::UsdShadeInput DiffuseInput = SurfaceShader.GetInput( pxr::TfToken("diffuseColor") );

		if ( DiffuseInput )
		{
			UMaterialExpression* BaseColorExpression = nullptr;

			if ( !DiffuseInput.HasConnectedSource() )
			{
				pxr::GfVec3f UsdDiffuseColor;
				DiffuseInput.Get< pxr::GfVec3f >( &UsdDiffuseColor );

				FLinearColor DiffuseColor = UsdToUnreal::ConvertColor( UsdDiffuseColor );

				UMaterialExpressionConstant4Vector* Constant4VectorExpression = Cast< UMaterialExpressionConstant4Vector >( UMaterialEditingLibrary::CreateMaterialExpression( &Material, UMaterialExpressionConstant4Vector::StaticClass() ) );
				Constant4VectorExpression->Constant = DiffuseColor;

				BaseColorExpression = Constant4VectorExpression;
			}
			else
			{
				BaseColorExpression = UsdGeomMeshConversionImpl::ParseInputTexture( DiffuseInput, Material );
			}

			if ( BaseColorExpression )
			{
				Material.BaseColor.Expression = BaseColorExpression;
				bHasMaterialInfo = true;
			}
		}
	}
	
	// Metallic
	{
		UMaterialExpression* MetallicExpression = ParseFloatInput( "metallic", 0.f );

		if ( MetallicExpression )
		{
			Material.Metallic.Expression = MetallicExpression;
			bHasMaterialInfo = true;
		}
	}

	// Roughness
	{
		UMaterialExpression* RoughnessExpression = ParseFloatInput( "roughness", 1.f );

		if ( RoughnessExpression )
		{
			Material.Roughness.Expression = RoughnessExpression;
			bHasMaterialInfo = true;
		}
	}

	// Opacity
	{
		UMaterialExpression* OpacityExpression = ParseFloatInput( "opacity", 1.f );;

		if ( OpacityExpression )
		{
			Material.Opacity.Expression = OpacityExpression;
			Material.BlendMode = BLEND_Translucent;
			bHasMaterialInfo = true;
		}
	}

	return bHasMaterialInfo;
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
