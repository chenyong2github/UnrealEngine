// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StaticMeshImporter.h"
#include "USDImporter.h"
#include "USDConversionUtils.h"
#include "RawMesh.h"
#include "MeshUtilities.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "USDAssetImportData.h"
#include "Factories/Factory.h"
#include "MeshDescription.h"
#include "MeshAttributes.h"
#include "IMeshBuilderModule.h"
#include "PackageTools.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "USDIncludesEnd.h"

#define LOCTEXT_NAMESPACE "USDImportPlugin"

struct FMeshDescriptionWrapper
{
	struct FVertexAttributes
	{
		FVertexAttributes(FMeshDescription* MeshDescription) :
			Positions(MeshDescription->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position)),
			Normals(MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal)),
			Tangents(MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent)),
			BinormalSigns(MeshDescription->VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign)),
			Colors(MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color)),
			UVs(MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate))
		{
		}
		TVertexAttributesRef<FVector> Positions;
		TVertexInstanceAttributesRef<FVector> Normals;
		TVertexInstanceAttributesRef<FVector> Tangents;
		TVertexInstanceAttributesRef<float> BinormalSigns;
		TVertexInstanceAttributesRef<FVector4> Colors;
		TVertexInstanceAttributesRef<FVector2D> UVs;
	};

	FMeshDescriptionWrapper(FMeshDescription* MeshDescription) :
		Vertex(MeshDescription),
		EdgeHardnesses(MeshDescription->EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard)),
		EdgeCreaseSharpnesses(MeshDescription->EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness)),
		PolygonGroupImportedMaterialSlotNames(MeshDescription->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName))
	{
	}

	FVertexAttributes Vertex;
	TEdgeAttributesRef<bool> EdgeHardnesses;
	TEdgeAttributesRef<float> EdgeCreaseSharpnesses;
	TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames;
};

struct FUSDImportMaterialInfo
{
	FUSDImportMaterialInfo() :
		UnrealMaterial(nullptr)
	{

	}
	FString Name;
	UMaterialInterface* UnrealMaterial;
};

struct FUSDStaticMeshImportState
{
public:
	FUSDStaticMeshImportState(FUsdImportContext& InImportContext, TArray<FUSDImportMaterialInfo>& InMaterials) :
		ImportContext(InImportContext),
		Materials(InMaterials),
		MeshDescription(nullptr)
	{
	}

	FUsdImportContext& ImportContext;
	TArray<FUSDImportMaterialInfo>& Materials;
	FTransform FinalTransform;
	FMatrix FinalTransformIT;
	FMeshDescription* MeshDescription;
	UUSDImportOptions* ImportOptions;
	UStaticMesh* NewMesh;
	bool bFlip;

private:
	int32 VertexOffset;
	int32 VertexInstanceOffset;
	int32 PolygonOffset;
	int32 MaterialIndexOffset;

public:
	void ProcessStaticUSDGeometry(const pxr::UsdPrim& GeomPrim, int32 LODIndex);
	void ProcessMaterials(int32 LODIndex);

private:
	void AddVertexPositions( FMeshDescriptionWrapper& DestMeshWrapper, const pxr::UsdGeomMesh& Mesh );
	bool AddPolygons( FMeshDescriptionWrapper& DestMeshWrapper, const pxr::UsdGeomMesh& Mesh, const TArray< FString >& MaterialNames, const TArray< int32 >& FaceMaterialIndices );
};

void FUSDStaticMeshImportState::ProcessStaticUSDGeometry(const pxr::UsdPrim& GeomPrim, int32 LODIndex)
{
	pxr::UsdGeomMesh Mesh( GeomPrim );
	if ( !Mesh )
	{
		return;
	}

	FMeshDescriptionWrapper DestMeshWrapper(MeshDescription);
	VertexOffset = MeshDescription->Vertices().Num();
	VertexInstanceOffset = MeshDescription->VertexInstances().Num();
	PolygonOffset = MeshDescription->Polygons().Num();
	MaterialIndexOffset = Materials.Num();

	TTuple< TArray< FString >, TArray< int32 > > GeometryMaterials = IUsdPrim::GetGeometryMaterials( pxr::UsdTimeCode::Default().GetValue(), GeomPrim );

	Materials.AddDefaulted( GeometryMaterials.Key.Num() );

	AddVertexPositions( DestMeshWrapper, Mesh );
	AddPolygons( DestMeshWrapper, Mesh, GeometryMaterials.Key, GeometryMaterials.Value );
}

void FUSDStaticMeshImportState::AddVertexPositions( FMeshDescriptionWrapper& DestMeshWrapper, const pxr::UsdGeomMesh& Mesh )
{
	using namespace pxr;

	pxr::UsdAttribute Points = Mesh.GetPointsAttr();
	if ( Points )
	{
		pxr::VtArray< pxr::GfVec3f > PointsArray;
		Points.Get( &PointsArray, UsdTimeCode::Default().GetValue() );

		for ( int32 LocalPointIndex = 0; LocalPointIndex < PointsArray.size(); ++LocalPointIndex )
		{
			const GfVec3f& Point = PointsArray[ LocalPointIndex ];

			FVector Pos = USDToUnreal::ConvertVector( *ImportContext.Stage, Point );
			Pos = FinalTransform.TransformPosition(Pos);

			FVertexID AddedVertexId = MeshDescription->CreateVertex();
			DestMeshWrapper.Vertex.Positions[AddedVertexId] = Pos;
		}
	}
}

bool FUSDStaticMeshImportState::AddPolygons( FMeshDescriptionWrapper& DestMeshWrapper, const pxr::UsdGeomMesh& Mesh, const TArray< FString >& MaterialNames, const TArray< int32 >& FaceMaterialIndices )
{
	using namespace pxr;

	TMap<int32, FPolygonGroupID> PolygonGroupMapping;
	TArray<FVertexInstanceID> CornerInstanceIDs;
	TArray<FVertexID> CornerVerticesIDs;
	int32 CurrentVertexInstanceIndex = 0;

	bool bFlipThisGeometry = bFlip;

	if ( IUsdPrim::GetGeometryOrientation( Mesh ) == EUsdGeomOrientation::LeftHanded )
	{
		bFlipThisGeometry = !bFlip;
	}

	// Face counts
	UsdAttribute FaceCountsAttribute = Mesh.GetFaceVertexCountsAttr();
	VtArray< int > FaceCounts;

	if ( FaceCountsAttribute )
	{
		FaceCountsAttribute.Get( &FaceCounts, UsdTimeCode::Default().GetValue() );
	}

	// Face indices
	UsdAttribute FaceIndicesAttribute = Mesh.GetFaceVertexIndicesAttr();
	VtArray< int > FaceIndices;

	if ( FaceIndicesAttribute )
	{
		FaceIndicesAttribute.Get( &FaceIndices, UsdTimeCode::Default().GetValue() );
	}

	// Normals
	UsdAttribute NormalsAttribute = Mesh.GetNormalsAttr();
	VtArray< GfVec3f > Normals;

	if ( NormalsAttribute )
	{
		NormalsAttribute.Get( &Normals, UsdTimeCode::Default().GetValue() );
	}

	// UVs
	struct FUVSet
	{
		TOptional< VtIntArray > UVIndices; // UVs might be indexed or they might be flat (one per vertex)
		VtVec2fArray UVs;
	};

	EUsdInterpolationMethod UVInterpolationMethod = EUsdInterpolationMethod::FaceVarying;
	TArray< FUVSet > UVSets;
	{
		static TfToken UVSetName("primvars:st");

		UsdGeomPrimvar STPrimvar = Mesh.GetPrimvar(UVSetName);

		if(STPrimvar)
		{
			FUVSet UVSet;

			if (STPrimvar.GetInterpolation() == UsdGeomTokens->vertex)
			{
				UVInterpolationMethod = EUsdInterpolationMethod::Vertex;

				UVSet.UVIndices.Emplace();

				if ( STPrimvar.GetIndices(&UVSet.UVIndices.GetValue()) && STPrimvar.Get(&UVSet.UVs) )
				{
					UVSets.Add( MoveTemp( UVSet ) );
				}
			}
			else
			{
				if ( STPrimvar.ComputeFlattened(&UVSet.UVs) )
				{
					UVSets.Add( MoveTemp( UVSet ) );
				}
			}
		}
	}

	// When importing multiple mesh pieces to the same static mesh.  Ensure each mesh piece has the same number of Uv's
	{
		int32 ExistingUVCount = DestMeshWrapper.Vertex.UVs.GetNumIndices();
		int32 NumUVs = FMath::Max(UVSets.Num(), ExistingUVCount);
		NumUVs = FMath::Min<int32>(MAX_MESH_TEXTURE_COORDS_MD, NumUVs);
		// At least one UV set must exist.  
		NumUVs = FMath::Max<int32>(1, NumUVs);

		//Make sure all Vertex instance have the correct number of UVs
		DestMeshWrapper.Vertex.UVs.SetNumIndices(NumUVs);
	}

	for ( int32 PolygonIndex = 0; PolygonIndex < FaceCounts.size(); ++PolygonIndex )
	{
		int32 PolygonVertexCount = FaceCounts[PolygonIndex];
		CornerInstanceIDs.Reset();
		CornerInstanceIDs.AddUninitialized(PolygonVertexCount);
		CornerVerticesIDs.Reset();
		CornerVerticesIDs.AddUninitialized(PolygonVertexCount);

		for (int32 CornerIndex = 0; CornerIndex < PolygonVertexCount; ++CornerIndex, ++CurrentVertexInstanceIndex)
		{
			int32 VertexInstanceIndex = VertexInstanceOffset + CurrentVertexInstanceIndex;
			const FVertexInstanceID VertexInstanceID(VertexInstanceIndex);
			CornerInstanceIDs[CornerIndex] = VertexInstanceID;
			const int32 ControlPointIndex = FaceIndices[CurrentVertexInstanceIndex];
			const FVertexID VertexID(VertexOffset + ControlPointIndex);
			const FVector VertexPosition = DestMeshWrapper.Vertex.Positions[VertexID];
			CornerVerticesIDs[CornerIndex] = VertexID;

			FVertexInstanceID AddedVertexInstanceId = MeshDescription->CreateVertexInstance(VertexID);

			if ( Normals.size() > 0 )
			{
				const int32 NormalIndex = Normals.size() != FaceIndices.size() ? FaceIndices[CurrentVertexInstanceIndex] : CurrentVertexInstanceIndex;
				check(NormalIndex < Normals.size());
				const GfVec3f& Normal = Normals[NormalIndex];
				FVector TransformedNormal = FinalTransformIT.TransformVector( USDToUnreal::ConvertVector( *ImportContext.Stage, Normal ) );

				DestMeshWrapper.Vertex.Normals[AddedVertexInstanceId] = TransformedNormal.GetSafeNormal();
			}

			int32 UVLayerIndex = 0;
			for ( const FUVSet& UVSet : UVSets )
			{
				GfVec2f UV;

				if ( UVInterpolationMethod == EUsdInterpolationMethod::Vertex && UVSet.UVIndices.IsSet() )
				{
					int32 NumFaces = FaceCounts.size();
					int32 NumFaceIndices = FaceIndices.size();
					int32 NumUVs = UVSet.UVs.size();
					int32 NumIndices = UVSet.UVIndices->size();

					if ( ensure( UVSet.UVIndices.GetValue().size() > VertexID.GetValue() ) )
					{
						UV = UVSet.UVs[ UVSet.UVIndices.GetValue()[ VertexID.GetValue() ] ];
					}
				}
				else if ( UVSet.UVs.size() > CurrentVertexInstanceIndex )
				{
					UV = UVSet.UVs[ CurrentVertexInstanceIndex ];
				}

				// Flip V for Unreal uv's which match directx
				FVector2D FinalUVVector(UV[0], 1.f - UV[1]);
				DestMeshWrapper.Vertex.UVs.Set(AddedVertexInstanceId, UVLayerIndex, FinalUVVector);

				++UVLayerIndex;
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
				Materials[RealMaterialIndex].Name = MaterialName;
			}

			FPolygonGroupID ExistingPolygonGroup = FPolygonGroupID::Invalid;
			for (const FPolygonGroupID PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs())
			{
				if (DestMeshWrapper.PolygonGroupImportedMaterialSlotNames[PolygonGroupID] == ImportedMaterialSlotName)
				{
					ExistingPolygonGroup = PolygonGroupID;
					break;
				}
			}
			if (ExistingPolygonGroup == FPolygonGroupID::Invalid)
			{
				ExistingPolygonGroup = MeshDescription->CreatePolygonGroup();
				DestMeshWrapper.PolygonGroupImportedMaterialSlotNames[ExistingPolygonGroup] = ImportedMaterialSlotName;
			}
			PolygonGroupMapping.Add(RealMaterialIndex, ExistingPolygonGroup);
		}

		FPolygonGroupID PolygonGroupID = PolygonGroupMapping[RealMaterialIndex];
		// Insert a polygon into the mesh
		const FPolygonID NewPolygonID = MeshDescription->CreatePolygon(PolygonGroupID, CornerInstanceIDs);
		if (bFlipThisGeometry)
		{
			MeshDescription->ReversePolygonFacing(NewPolygonID);
		}
		else
		{
			FMeshPolygon& Polygon = MeshDescription->GetPolygon(NewPolygonID);
			MeshDescription->ComputePolygonTriangulation(NewPolygonID, Polygon.Triangles);
		}
	}

	// Vertex color
	UsdGeomPrimvar ColorPrimvar = Mesh.GetDisplayColorPrimvar();
	if (ColorPrimvar)
	{
		pxr::VtArray<pxr::GfVec3f> USDColors;
		ColorPrimvar.ComputeFlattened(&USDColors);

		TVertexInstanceAttributesRef<FVector4> Colors = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color);

		int32 NumColors = USDColors.size();

		auto ConvertToLinear = []( const pxr::GfVec3f& UsdColor ) -> FLinearColor
		{
			return FLinearColor( FLinearColor( USDToUnreal::ConvertColor( UsdColor ) ).ToFColor( false ) );
		};

		pxr::TfToken USDInterpType = ColorPrimvar.GetInterpolation();
		if(USDInterpType == pxr::UsdGeomTokens->faceVarying && NumColors >= Colors.GetNumElements())
		{
			for(int Index = 0; Index < Colors.GetNumElements(); ++Index)
			{
				Colors[FVertexInstanceID(Index)] = ConvertToLinear(USDColors[Index]);
			}
		}
		else if(USDInterpType == pxr::UsdGeomTokens->vertex && NumColors >= MeshDescription->Vertices().Num())
		{
			for(auto VertexInstID : MeshDescription->VertexInstances().GetElementIDs())
			{
				FVertexID VertexID = MeshDescription->GetVertexInstance(VertexInstID).VertexID;
				Colors[VertexInstID] = ConvertToLinear(USDColors[VertexID.GetValue()]);
			}
		}
		else if (USDInterpType == pxr::UsdGeomTokens->constant && NumColors == 1)
		{
			for(int Index = 0; Index < Colors.GetNumElements(); ++Index)
			{
				Colors[FVertexInstanceID(Index)] = ConvertToLinear(USDColors[0]);
			}
		}
	}
	
	return true;
}

void FUSDStaticMeshImportState::ProcessMaterials(int32 LODIndex)
{
	const FString BasePackageName = FPackageName::GetLongPackagePath(NewMesh->GetOutermost()->GetName());

	FMeshDescriptionWrapper DestMeshWrapper(MeshDescription);
	TArray<FStaticMaterial> MaterialToAdd;
	for (const FPolygonGroupID PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs())
	{
		const FName& ImportedMaterialSlotName = DestMeshWrapper.PolygonGroupImportedMaterialSlotNames[PolygonGroupID];
		const FString ImportedMaterialSlotNameString = ImportedMaterialSlotName.ToString();
		const FName MaterialSlotName = ImportedMaterialSlotName;
		int32 MaterialIndex = INDEX_NONE;
		for (int32 MeshMaterialIndex = 0; MeshMaterialIndex < Materials.Num(); ++MeshMaterialIndex)
		{
			FUSDImportMaterialInfo& MeshMaterial = Materials[MeshMaterialIndex];
			if (MeshMaterial.Name.Equals(ImportedMaterialSlotNameString))
			{
				MaterialIndex = MeshMaterialIndex;
				break;
			}
		}
		if (MaterialIndex == INDEX_NONE)
		{
			MaterialIndex = PolygonGroupID.GetValue();
		}

		UMaterialInterface* Material = nullptr;
		if (Materials.IsValidIndex(MaterialIndex))
		{
			Material = Materials[MaterialIndex].UnrealMaterial;
			if (Material == nullptr)
			{
				const FString& MaterialFullName = Materials[MaterialIndex].Name;
				FString MaterialBasePackageName = BasePackageName;
				MaterialBasePackageName += TEXT("/");
				MaterialBasePackageName += MaterialFullName;
				MaterialBasePackageName = UPackageTools::SanitizePackageName(MaterialBasePackageName);

				// The material could already exist in the project
				//FName ObjectPath = *(MaterialBasePackageName + TEXT(".") + MaterialFullName);

				FText Error;
				Material = UMaterialImportHelpers::FindExistingMaterialFromSearchLocation(MaterialFullName, MaterialBasePackageName, ImportOptions->MaterialSearchLocation, Error);
				if (Material)
				{
					Materials[MaterialIndex].UnrealMaterial = Material;
				}
			}
		}

		if (Material == nullptr)
		{
			FSoftObjectPath VertexColorMaterialPath( TEXT("Material'/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial'") );
			Material = Cast< UMaterialInterface >( VertexColorMaterialPath.TryLoad() );
		}

		FStaticMaterial StaticMaterial(Material, MaterialSlotName, ImportedMaterialSlotName);
		if (LODIndex > 0)
		{
			MaterialToAdd.Add(StaticMaterial);
		}
		else
		{
			NewMesh->StaticMaterials.Add(StaticMaterial);
		}
	}
	if (LODIndex > 0)
	{
		//Insert the new materials in the static mesh
		// TODO
	}
}

UStaticMesh* FUSDStaticMeshImporter::ImportStaticMesh(FUsdImportContext& ImportContext, const FUsdAssetPrimToImport& PrimToImport)
{
	const pxr::UsdPrim& Prim = *PrimToImport.Prim;

	FTransform PrimToWorld = ImportContext.bApplyWorldTransformToGeometry ? USDToUnreal::ConvertMatrix(ImportContext.Stage.Get(), IUsdPrim::GetLocalTransform( Prim )) : FTransform::Identity;

	FTransform FinalTransform = PrimToWorld;
	if (ImportContext.ImportOptions->Scale != 1.0f)
	{
		FVector Scale3D = FinalTransform.GetScale3D() * ImportContext.ImportOptions->Scale;
		FinalTransform.SetScale3D(Scale3D);
	}
	FMatrix FinalTransformIT = FinalTransform.ToInverseMatrixWithScale().GetTransposed();
	bool bFlip = FinalTransform.GetDeterminant() < 0.0f;


	int32 NumLODs = PrimToImport.NumLODs;

	UUSDImportOptions* ImportOptions = nullptr;
	UStaticMesh* NewMesh = USDUtils::FindOrCreateObject<UStaticMesh>(ImportContext.Parent, ImportContext.ObjectName, ImportContext.ImportObjectFlags);
	check(NewMesh);
	UUSDAssetImportData* ImportData = Cast<UUSDAssetImportData>(NewMesh->AssetImportData);
	if (!ImportData)
	{
		ImportData = NewObject<UUSDAssetImportData>(NewMesh);
		ImportData->ImportOptions = DuplicateObject<UUSDImportOptions>(ImportContext.ImportOptions, ImportData);
		NewMesh->AssetImportData = ImportData;
	}
	else if (!ImportData->ImportOptions)
	{
		ImportData->ImportOptions = DuplicateObject<UUSDImportOptions>(ImportContext.ImportOptions, ImportData);
	}
	ImportOptions = CastChecked<UUSDAssetImportData>(NewMesh->AssetImportData)->ImportOptions;
	check(ImportOptions);

	FString CurrentFilename = UFactory::GetCurrentFilename();
	if (!CurrentFilename.IsEmpty())
	{
		NewMesh->AssetImportData->Update(UFactory::GetCurrentFilename());
	}

	NewMesh->StaticMaterials.Empty();

	TArray<FUSDImportMaterialInfo> Materials;
	FUSDStaticMeshImportState State(ImportContext, Materials);
	State.FinalTransform = FinalTransform;
	State.FinalTransformIT = FinalTransformIT;
	State.bFlip = bFlip;
	State.ImportOptions = ImportOptions;
	State.NewMesh = NewMesh;

	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		if (NewMesh->GetNumSourceModels() < LODIndex + 1)
		{
			// Add one LOD 
			NewMesh->AddSourceModel();

			if (NewMesh->GetNumSourceModels() < LODIndex + 1)
			{
				LODIndex = NewMesh->GetNumSourceModels() - 1;
			}
		}

		TArray< TUsdStore< pxr::UsdPrim > > PrimsWithGeometry;
		for (const TUsdStore< pxr::UsdPrim >& MeshPrim : PrimToImport.MeshPrims)
		{
			if (IUsdPrim::GetNumLODs( *MeshPrim ) > LODIndex)
			{
				// If the mesh has LOD children at this index then use that as the geom prim
				IUsdPrim::SetActiveLODIndex( *MeshPrim, LODIndex );

				ImportContext.PrimResolver->FindMeshChildren(ImportContext, *MeshPrim, false, PrimsWithGeometry);
			}
			else if (LODIndex == 0)
			{
				// If a mesh has no lods then it should only contribute to the base LOD
				PrimsWithGeometry.Add(MeshPrim);
			}
		}

		//Create private asset in the same package as the StaticMesh, and make sure reference are set to avoid GC
		State.MeshDescription = NewMesh->CreateMeshDescription(LODIndex);
		check(State.MeshDescription != nullptr);
		NewMesh->RegisterMeshAttributes(*State.MeshDescription);

		bool bRecomputeNormals = false;

		for (const TUsdStore< pxr::UsdPrim >& GeomPrim : PrimsWithGeometry)
		{
			// If we dont have a geom prim this might not be an error so dont message it.  The geom prim may not contribute to the LOD for whatever reason
			if (GeomPrim.Get())
			{
				pxr::UsdGeomMesh USDMesh( *GeomPrim );

				if (USDMesh)
				{
					if ( pxr::UsdAttribute NormalAttri = USDMesh.GetNormalsAttr() )
					{
						if ( !NormalAttri.HasValue() )
						{
							bRecomputeNormals = true;
						}
					}

					State.ProcessStaticUSDGeometry(*GeomPrim, LODIndex);
				}
				else
				{
					ImportContext.AddErrorMessage(EMessageSeverity::Error, FText::Format(LOCTEXT("StaticMeshesMustBeTriangulated", "{0} is not a triangle mesh. Static meshes must be triangulated to import"), FText::FromString(ImportContext.ObjectName)));

					if(NewMesh)
					{
						NewMesh->ClearFlags(RF_Standalone);
						NewMesh = nullptr;
					}
					break;
				}
			}
		}

		if (!NewMesh)
		{
			break;
		}

		if (!NewMesh->IsSourceModelValid(LODIndex))
		{
			// Add one LOD 
			NewMesh->AddSourceModel();
		}

		State.ProcessMaterials(LODIndex);

		NewMesh->CommitMeshDescription(LODIndex);

		FStaticMeshSourceModel& SrcModel = NewMesh->GetSourceModel(LODIndex);
		SrcModel.BuildSettings.bGenerateLightmapUVs = false;
		SrcModel.BuildSettings.bRecomputeNormals = bRecomputeNormals;
		SrcModel.BuildSettings.bRecomputeTangents = true;
		SrcModel.BuildSettings.bBuildAdjacencyBuffer = false;

		NewMesh->ImportVersion = EImportStaticMeshVersion::LastVersion;
	}

	if(NewMesh)
	{
		NewMesh->ImportVersion = EImportStaticMeshVersion::LastVersion;

		NewMesh->CreateBodySetup();

		NewMesh->SetLightingGuid();

		NewMesh->PostEditChange();
	}

	return NewMesh;
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
