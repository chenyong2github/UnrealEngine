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
#include "Factories/Factory.h"
#include "Factories/MaterialImportHelpers.h"
#include "MeshDescription.h"
#include "MeshDescriptionOperations.h"
#include "MeshAttributes.h"
#include "IMeshBuilderModule.h"
#include "PackageTools.h"
#include "StaticMeshAttributes.h"

#include "USDAssetImportData.h"
#include "USDGeomMeshConversion.h"
#include "USDImportOptions.h"
#include "USDTypesConversion.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "USDIncludesEnd.h"

#define LOCTEXT_NAMESPACE "USDImportPlugin"

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
};

void FUSDStaticMeshImportState::ProcessStaticUSDGeometry(const pxr::UsdPrim& GeomPrim, int32 LODIndex)
{
	pxr::UsdGeomMesh Mesh( GeomPrim );
	if ( !Mesh )
	{
		return;
	}

	UsdToUnreal::ConvertGeomMesh( Mesh, *MeshDescription );
}

void FUSDStaticMeshImportState::ProcessMaterials(int32 LODIndex)
{
	const FString BasePackageName = FPackageName::GetLongPackagePath(NewMesh->GetOutermost()->GetName());

	FStaticMeshAttributes Attributes(*MeshDescription);
	TArray<FStaticMaterial> MaterialToAdd;
	for (const FPolygonGroupID PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs())
	{
		const FName& ImportedMaterialSlotName = Attributes.GetPolygonGroupMaterialSlotNames()[PolygonGroupID];
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

		if ( !Materials.IsValidIndex( MaterialIndex ) )
		{
			Materials.AddDefaulted( Materials.Num() - MaterialIndex + 1 );
			Materials[ MaterialIndex ].Name = ImportedMaterialSlotNameString;
		}

		UMaterialInterface* Material = nullptr;
		if (Materials.IsValidIndex(MaterialIndex))
		{
			Material = Materials[MaterialIndex].UnrealMaterial;
			if (Material == nullptr)
			{
				FString MaterialFullName = Materials[MaterialIndex].Name;

				// Only keep material name without prim path when searching for a UMaterial
				FString MaterialPath;
				FString MaterialName;
				if ( MaterialFullName.Split( FString( TEXT("/") ), &MaterialPath, &MaterialName, ESearchCase::IgnoreCase, ESearchDir::FromEnd ) )
				{
					MaterialFullName = MaterialName;
				}

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
			if ( FMeshDescriptionOperations::HasVertexColor( *MeshDescription ) )
			{
				FSoftObjectPath VertexColorMaterialPath( TEXT("Material'/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial'") );
				Material = Cast< UMaterialInterface >( VertexColorMaterialPath.TryLoad() );
			}
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

	FTransform PrimToWorld = ImportContext.bApplyWorldTransformToGeometry ? UsdToUnreal::ConvertMatrix(ImportContext.Stage.Get(), IUsdPrim::GetLocalTransform( Prim )) : FTransform::Identity;

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
	UStaticMesh* NewMesh = UsdUtils::FindOrCreateObject<UStaticMesh>(ImportContext.Parent, ImportContext.ObjectName, ImportContext.ImportObjectFlags);
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
