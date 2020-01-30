// Copyright Epic Games, Inc. All Rights Reserved.

#if USE_USD_SDK

#include "USDSkeletalDataConversion.h"

#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "AnimEncoding.h"
#include "MeshUtilities.h"
#include "Modules/ModuleManager.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"

#include "Factories/FbxSkeletalMeshImportData.h"

#include "USDIncludesStart.h"

#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdSkel/binding.h"
#include "pxr/usd/usdSkel/bindingAPI.h"
#include "pxr/usd/usdSkel/cache.h"
#include "pxr/usd/usdSkel/skeletonQuery.h"
#include "pxr/usd/usdSkel/topology.h"
#include "pxr/usd/usdSkel/utils.h"

#include "USDIncludesEnd.h"

bool UsdToUnreal::ConvertSkeleton(const pxr::UsdSkelSkeletonQuery& SkeletonQuery, FSkeletalMeshImportData& SkelMeshImportData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE( UsdToUnreal::ConvertSkeleton );

	using namespace pxr;

	TArray<FString> JointNames;
	TArray<int32> ParentJointIndices;

	// Retrieve the joint names and parent indices from the skeleton topology
	// GetJointOrder already orders them from parent-to-child
	VtArray<TfToken> JointOrder = SkeletonQuery.GetJointOrder();
	const UsdSkelTopology& SkelTopology = SkeletonQuery.GetTopology();
	for (uint32 Index = 0; Index < SkelTopology.GetNumJoints(); ++Index)
	{
		SdfPath JointPath(JointOrder[Index]);

		FString JointName = UsdToUnreal::ConvertString(JointPath.GetName());
		JointNames.Add(JointName);

		int ParentIndex = SkelTopology.GetParent(Index);
		ParentJointIndices.Add(ParentIndex);
	}

	if (JointNames.Num() == 0 || JointNames.Num() > MAX_BONES)
	{
		return false;
	}

	// Retrieve the bone transforms to be used as the reference pose
	VtArray<GfMatrix4d> UsdBoneTransforms;
	TArray<FTransform> BoneTransforms;

	bool bJointTransformsComputed = SkeletonQuery.ComputeJointLocalTransforms(&UsdBoneTransforms, UsdTimeCode::Default());
	if (bJointTransformsComputed)
	{
		UsdStageWeakPtr Stage = SkeletonQuery.GetSkeleton().GetPrim().GetStage();
		for (uint32 Index = 0; Index < UsdBoneTransforms.size(); ++Index)
		{
			const GfMatrix4d& UsdMatrix = UsdBoneTransforms[Index];
			FTransform BoneTransform = UsdToUnreal::ConvertMatrix( UsdUtils::GetUsdStageAxis( Stage ), UsdMatrix );
			BoneTransforms.Add(BoneTransform);
		}
	}

	if (JointNames.Num() != BoneTransforms.Num())
	{
		return false;
	}

	// Store the retrieved data as bones into the SkeletalMeshImportData
	for (int32 Index = 0; Index < JointNames.Num(); ++Index)
	{
		SkeletalMeshImportData::FBone& Bone = SkelMeshImportData.RefBonesBinary.Add_GetRef(SkeletalMeshImportData::FBone());

		Bone.Name = JointNames[Index];
		Bone.ParentIndex = ParentJointIndices[Index];
		// Increment the number of children each time a bone is referenced as a parent bone; the root has a parent index of -1
		if (Bone.ParentIndex >= 0)
		{
			// The joints are ordered from parent-to-child so the parent will already have been added to the array
			SkeletalMeshImportData::FBone& ParentBone = SkelMeshImportData.RefBonesBinary[Bone.ParentIndex];
			++ParentBone.NumChildren;
		}

		SkeletalMeshImportData::FJointPos& JointMatrix = Bone.BonePos;
		JointMatrix.Transform = BoneTransforms[Index];

		// Not sure if Length and X/Y/Z Size need to be set, there are no equivalents in USD
		JointMatrix.Length = 1.f;
		JointMatrix.XSize = 100.f;
		JointMatrix.YSize = 100.f;
		JointMatrix.ZSize = 100.f;
	}

	return true;
}

bool UsdToUnreal::ConvertSkinnedMesh(const pxr::UsdSkelSkinningQuery& SkinningQuery, FSkeletalMeshImportData& SkelMeshImportData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE( UsdToUnreal::ConvertSkinnedMesh );

	using namespace pxr;

	const UsdPrim& SkinningPrim = SkinningQuery.GetPrim();
	UsdSkelBindingAPI SkelBinding(SkinningPrim);

	// Retrieve the binding transform attribute setting the transform to identity if it doesn't exist
	// This transform is applied to the vertices of the mesh 
	GfMatrix4d GeomBindingTransform(1);
	UsdAttribute GeomBindingAttribute = SkelBinding.GetGeomBindTransformAttr();
	if (GeomBindingAttribute)
	{
		GeomBindingAttribute.Get(&GeomBindingTransform, UsdTimeCode::Default());
	}

	const pxr::TfToken StageUpAxis = UsdUtils::GetUsdStageAxis( SkinningPrim.GetStage() );

	FTransform GeomTransform = UsdToUnreal::ConvertMatrix(StageUpAxis, GeomBindingTransform);

	// Ref. FFbxImporter::FillSkelMeshImporterFromFbx
	UsdGeomMesh UsdMesh = UsdGeomMesh(SkinningPrim);

	// Retrieve the mesh points (vertices) from USD and append it to the SkeletalMeshImportData Points
	uint32 NumPoints = 0;
	uint32 NumExistingPoints = SkelMeshImportData.Points.Num();

	UsdAttribute PointsAttr = UsdMesh.GetPointsAttr();
	if (PointsAttr)
	{
		VtArray<GfVec3f> UsdPoints;
		PointsAttr.Get(&UsdPoints, UsdTimeCode::Default());

		NumPoints = UsdPoints.size();
		SkelMeshImportData.Points.AddUninitialized(NumPoints);

		for (uint32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
		{
			const GfVec3f& Point = UsdPoints[PointIndex];

			// Convert the USD vertex to Unreal and apply the GeomBindTransform to it
			FVector Pos = UsdToUnreal::ConvertVector(StageUpAxis, Point);
			Pos = GeomTransform.TransformPosition(Pos);

			SkelMeshImportData.Points[PointIndex + NumExistingPoints] = Pos;
		}
	}

	if (NumPoints == 0)
	{
		return false;
	}

	// Convert the face data into SkeletalMeshImportData

	// Face counts
	VtArray<int> FaceCounts;
	UsdAttribute FaceCountsAttribute = UsdMesh.GetFaceVertexCountsAttr();
	if (FaceCountsAttribute)
	{
		FaceCountsAttribute.Get(&FaceCounts, UsdTimeCode::Default());
	}

	// Face indices
	VtArray<int> OriginalFaceIndices;
	UsdAttribute FaceIndicesAttribute = UsdMesh.GetFaceVertexIndicesAttr();
	if (FaceIndicesAttribute)
	{
		FaceIndicesAttribute.Get(&OriginalFaceIndices, UsdTimeCode::Default());
	}

	// Normals
	VtArray<GfVec3f> Normals;
	UsdAttribute NormalsAttribute = UsdMesh.GetNormalsAttr();
	if (NormalsAttribute)
	{
		NormalsAttribute.Get(&Normals, UsdTimeCode::Default());
	}

	uint32 NumExistingFaces = SkelMeshImportData.Faces.Num();
	uint32 NumExistingWedges = SkelMeshImportData.Wedges.Num();

	uint32 NumFaces = FaceCounts.size();
	SkelMeshImportData.Faces.Reserve( NumFaces * 2 );

	// Retrieve prim materials
	TTuple< TArray< FString >, TArray< int32 > > GeometryMaterials = IUsdPrim::GetGeometryMaterials( UsdTimeCode::Default().GetValue(), SkinningPrim );
	TArray< FString >& MaterialNames = GeometryMaterials.Key;
	TArray< int32 >& FaceMaterialIndices = GeometryMaterials.Value;

	int32 MaterialIndexOffset = SkelMeshImportData.Materials.Num();

	TMap<FString, int32> ExistingMaterialNames;
	for (int32 Index = 0; Index < SkelMeshImportData.Materials.Num(); ++Index)
	{
		ExistingMaterialNames.Add(SkelMeshImportData.Materials[Index].MaterialImportName, Index);
	}

	// Retrieve vertex colors
	UsdGeomPrimvar ColorPrimvar = UsdMesh.GetDisplayColorPrimvar();
	TArray<FColor> FaceColors;

	bool bIsConstantColor = false;
	if (ColorPrimvar)
	{
		pxr::VtArray<pxr::GfVec3f> UsdColors;
		ColorPrimvar.ComputeFlattened(&UsdColors);

		uint32 NumColors = UsdColors.size();
		FaceColors.Reserve(NumColors);

		pxr::TfToken USDInterpType = ColorPrimvar.GetInterpolation();

		auto ConvertToColor = []( const pxr::GfVec3f& UsdColor ) -> FColor
		{
			return FLinearColor( FLinearColor( UsdToUnreal::ConvertColor( UsdColor ) ).ToFColor( false ) ).ToFColor(true);
		};

		if(/*USDInterpType == pxr::UsdGeomTokens->vertex &&*/ NumColors >= NumFaces)
		{
			FaceColors.Reserve( NumColors );
			for (uint32 Index = 0; Index < NumColors; ++Index)
			{
				FaceColors.Add(ConvertToColor(UsdColors[Index]));
			}
		}
		else if (/*USDInterpType == pxr::UsdGeomTokens->constant &&*/ NumColors == 1)
		{
			FaceColors.Add(ConvertToColor(UsdColors[0]));
			bIsConstantColor = true;
		}
		SkelMeshImportData.bHasVertexColors = true;
	}

	// Retrieve vertex opacity
	UsdGeomPrimvar OpacityPrimvar = UsdMesh.GetDisplayOpacityPrimvar();
	if ( OpacityPrimvar )
	{
		pxr::VtArray< float > UsdOpacities;
		OpacityPrimvar.ComputeFlattened( &UsdOpacities );

		const uint32 NumOpacities = UsdOpacities.size();

		pxr::TfToken UsdInterpType = OpacityPrimvar.GetInterpolation();
		if ( /*UsdInterpType == pxr::UsdGeomTokens->vertex &&*/ NumOpacities >= NumFaces )
		{
			for (uint32 Index = 0; Index < NumOpacities; ++Index)
			{
				if ( !FaceColors.IsValidIndex( Index ) )
				{
					FaceColors.Add( FColor::White );
				}

				FaceColors[ Index ].A = UsdOpacities[ Index ];
			}
		}
		else if ( /*UsdInterpType == pxr::UsdGeomTokens->constant &&*/ NumOpacities == 1 )
		{
			if ( FaceColors.Num() < 1 )
			{
				FaceColors.Add( FColor::White );
			}

			FaceColors[ 0 ].A = UsdOpacities[ 0 ];
		}
	}

	SkelMeshImportData.NumTexCoords = 0;

	bool bReverseOrder = IUsdPrim::GetGeometryOrientation(UsdMesh) == EUsdGeomOrientation::LeftHanded;

	struct FUVSet
	{
		TOptional< VtIntArray > UVIndices; // UVs might be indexed or they might be flat (one per vertex)
		VtVec2fArray UVs;

		EUsdInterpolationMethod InterpolationMethod = EUsdInterpolationMethod::FaceVarying;
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

			if ( PrimvarST.GetInterpolation() == UsdGeomTokens->vertex )
			{
				UVSet.InterpolationMethod = EUsdInterpolationMethod::Vertex;
			}
			else if ( PrimvarST.GetInterpolation() == UsdGeomTokens->faceVarying )
			{
				UVSet.InterpolationMethod = EUsdInterpolationMethod::FaceVarying;
			}
			else if (  PrimvarST.GetInterpolation() == UsdGeomTokens->uniform )
			{
				UVSet.InterpolationMethod = EUsdInterpolationMethod::Uniform;
			}
			else if ( PrimvarST.GetInterpolation() == UsdGeomTokens->constant )
			{
				UVSet.InterpolationMethod = EUsdInterpolationMethod::Constant;
			}

			if ( PrimvarST.IsIndexed() )
			{
				UVSet.UVIndices.Emplace();

				if ( PrimvarST.GetIndices( &UVSet.UVIndices.GetValue() ) && PrimvarST.Get( &UVSet.UVs ) )
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

	// SkeletalMeshImportData uses triangle faces so quads will have to be split into triangles
	SkeletalMeshImportData::FVertex TmpWedges[3];

	for (uint32 PolygonIndex = NumExistingFaces, LocalIndex = 0; PolygonIndex < NumExistingFaces + NumFaces; ++PolygonIndex, ++LocalIndex)
	{
		const uint32 NumOriginalFaceVertices = FaceCounts[LocalIndex];
		const uint32 NumFinalFaceVertices = 3;

		// Face must be processed as triangle
		bool bIsQuad = ( NumOriginalFaceVertices == 4 );

		uint32 NumTriangles = bIsQuad ? 2 : 1;

		for ( uint32 TriangleIndex = 0; TriangleIndex < NumTriangles; ++TriangleIndex )
		{
			int32 TriangleFaceIndex = SkelMeshImportData.Faces.AddUninitialized();

			SkeletalMeshImportData::FTriangle& Triangle = SkelMeshImportData.Faces[ TriangleFaceIndex ];

			// Set the face smoothing by default. It could be any number, but not zero
			Triangle.SmoothingGroups = 255;

			// #ueent_todo: Convert normals to TangentZ (TangentX/TangentY are tangents/binormals)

			// Manage materials
			int32 LocalMaterialIndex = 0;
			if (LocalIndex >= 0 && LocalIndex < (uint32) FaceMaterialIndices.Num())
			{
				LocalMaterialIndex = FaceMaterialIndices[LocalIndex];
				if (LocalMaterialIndex < 0 || LocalMaterialIndex > MaterialNames.Num())
				{
					LocalMaterialIndex = 0;
				}
			}

			int32 RealMaterialIndex = 0;
			if (LocalMaterialIndex < MaterialNames.Num())
			{
				FString MaterialName = MaterialNames[LocalMaterialIndex];
				if (!ExistingMaterialNames.Contains(MaterialName))
				{
					// If new material, add it to the list
					SkeletalMeshImportData::FMaterial NewMaterial;

					NewMaterial.MaterialImportName = MaterialName;
					RealMaterialIndex = SkelMeshImportData.Materials.Add(NewMaterial);
					ExistingMaterialNames.Add(MaterialName, RealMaterialIndex);
				}
				else
				{
					RealMaterialIndex = ExistingMaterialNames[MaterialName];
				}
			}

			Triangle.MatIndex = RealMaterialIndex;
			SkelMeshImportData.MaxMaterialIndex = FMath::Max<uint32>(SkelMeshImportData.MaxMaterialIndex, Triangle.MatIndex);

			Triangle.AuxMatIndex = 0;

			// Manage vertex colors
			FColor FaceColor = SkelMeshImportData.bHasVertexColors ? FaceColors[bIsConstantColor ? 0 : LocalIndex] : FColor::White;

			// Fill the wedge data and complete the triangle setup with the wedge indices
			SkelMeshImportData.Wedges.Reserve( SkelMeshImportData.Wedges.Num() + NumFinalFaceVertices );

			for ( uint32 CornerIndex = 0; CornerIndex < NumFinalFaceVertices; ++CornerIndex )
			{
				uint32 OriginalCornerIndex = ( ( TriangleIndex * ( NumOriginalFaceVertices - 2 ) ) + CornerIndex ) % NumOriginalFaceVertices;
				uint32 OriginalVertexInstanceIndex = ( LocalIndex * NumOriginalFaceVertices ) + OriginalCornerIndex;
				int32 OriginalVertexIndex = OriginalFaceIndices[ OriginalVertexInstanceIndex ];

				int32 FinalCornerIndex = bReverseOrder ? NumFinalFaceVertices - 1 - CornerIndex : CornerIndex;

				TmpWedges[ FinalCornerIndex ].MatIndex = Triangle.MatIndex;
				TmpWedges[ FinalCornerIndex ].VertexIndex = NumExistingPoints + OriginalVertexIndex;
			
				TmpWedges[ FinalCornerIndex ].Color = FaceColor;

				int32 UVLayerIndex = 0;
				for ( const FUVSet& UVSet : UVSets )
				{
					int32 ValueIndex = 0;

					if ( UVSet.InterpolationMethod == EUsdInterpolationMethod::Vertex )
					{
						ValueIndex = OriginalVertexIndex;
					}
					else if ( UVSet.InterpolationMethod == EUsdInterpolationMethod::FaceVarying )
					{
						ValueIndex = OriginalVertexInstanceIndex;
					}
					else if ( UVSet.InterpolationMethod == EUsdInterpolationMethod::Uniform )
					{
						ValueIndex = PolygonIndex;
					}
					else if ( UVSet.InterpolationMethod == EUsdInterpolationMethod::Constant )
					{
						ValueIndex = 0;
					}

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
					TmpWedges[ FinalCornerIndex ].UVs[ UVLayerIndex ] = FinalUVVector;

					++UVLayerIndex;
				}

				Triangle.TangentX[ FinalCornerIndex ] = FVector::ZeroVector;
				Triangle.TangentY[ FinalCornerIndex ] = FVector::ZeroVector;
				Triangle.TangentZ[ FinalCornerIndex ] = FVector::ZeroVector;

				{
					uint32 WedgeIndex = SkelMeshImportData.Wedges.AddUninitialized();
					SkelMeshImportData.Wedges[ WedgeIndex ].VertexIndex = TmpWedges[ FinalCornerIndex ].VertexIndex;
					SkelMeshImportData.Wedges[ WedgeIndex ].MatIndex = TmpWedges[ FinalCornerIndex ].MatIndex;
					SkelMeshImportData.Wedges[ WedgeIndex ].Color = TmpWedges[ FinalCornerIndex ].Color;
					SkelMeshImportData.Wedges[ WedgeIndex ].Reserved = 0;
					FMemory::Memcpy( SkelMeshImportData.Wedges[ WedgeIndex ].UVs, TmpWedges[ FinalCornerIndex ].UVs, sizeof(FVector2D) * MAX_TEXCOORDS );

					Triangle.WedgeIndex[ FinalCornerIndex ] = WedgeIndex;
				}
			}
		}
	}

	// Convert joint influences into the SkeletalMeshImportData

	// ComputeJointInfluences returns the influences per bone that applies to all the points of the mesh
	// ComputeVaryingJointInfluences returns the joint influences for each points, expanding the influences to all points if the mesh is rigidly deformed
	VtArray<int> JointIndices;
	VtArray<float> JointWeights;
	SkinningQuery.ComputeVaryingJointInfluences(NumPoints, &JointIndices, &JointWeights);

	// Recompute the joint influences if it's above the limit
	uint32 NumInfluencesPerComponent = SkinningQuery.GetNumInfluencesPerComponent();
	if (NumInfluencesPerComponent > MAX_INFLUENCES_PER_STREAM)
	{
		UsdSkelResizeInfluences(&JointIndices, NumInfluencesPerComponent, MAX_INFLUENCES_PER_STREAM);
		UsdSkelResizeInfluences(&JointWeights, NumInfluencesPerComponent, MAX_INFLUENCES_PER_STREAM);
		NumInfluencesPerComponent = MAX_INFLUENCES_PER_STREAM;
	}

	uint32 JointIndex = 0;
	SkelMeshImportData.Influences.Reserve(NumPoints);
	for (uint32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
	{
		// The JointIndices/JointWeights contain the influences data for NumPoints * NumInfluencesPerComponent
		for (uint32 InfluenceIndex = 0; InfluenceIndex < NumInfluencesPerComponent; ++InfluenceIndex, ++JointIndex)
		{
			// BoneWeight could be 0 if the actual number of influences were less than NumInfluencesPerComponent for a given point so just ignore it
			float BoneWeight = JointWeights[JointIndex];
			if (BoneWeight != 0.f)
			{
				SkelMeshImportData.Influences.AddUninitialized();
				SkelMeshImportData.Influences.Last().BoneIndex = JointIndices[JointIndex];
				SkelMeshImportData.Influences.Last().Weight = BoneWeight;
				SkelMeshImportData.Influences.Last().VertexIndex = NumExistingPoints + PointIndex;
			}
		}
	}

	return true;
}

USkeletalMesh* UsdToUnreal::GetSkeletalMeshFromImportData(FSkeletalMeshImportData& SkelMeshImportData, EObjectFlags ObjectFlags)
{
	TRACE_CPUPROFILER_EVENT_SCOPE( UsdToUnreal::GetSkeletalMeshFromImportData );

	// A SkeletalMesh could be retrieved for re-use and updated for animations
	// For now, create a new USkeletalMesh
	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, ObjectFlags | EObjectFlags::RF_Public);

	// One-to-one mapping from import to raw. Needed for BuildSkeletalMesh
	SkelMeshImportData.PointToRawMap.AddUninitialized(SkelMeshImportData.Points.Num());
	for (int32 PointIndex = 0; PointIndex < SkelMeshImportData.Points.Num(); ++PointIndex)
	{
		SkelMeshImportData.PointToRawMap[PointIndex] = PointIndex;
	}

	// Create initial bounding box based on expanded version of reference pose for meshes without physics assets
	FBox BoundingBox(SkelMeshImportData.Points.GetData(), SkelMeshImportData.Points.Num());
	FBox Temp = BoundingBox;
	FVector MidMesh = 0.5f*(Temp.Min + Temp.Max);
	BoundingBox.Min = Temp.Min + 1.0f*(Temp.Min - MidMesh);
	BoundingBox.Max = Temp.Max + 1.0f*(Temp.Max - MidMesh);
	BoundingBox.Min[2] = Temp.Min[2] + 0.1f*(Temp.Min[2] - MidMesh[2]);
	const FVector BoundingBoxSize = BoundingBox.GetSize();

	if (SkelMeshImportData.Points.Num() > 2 && BoundingBoxSize.X < THRESH_POINTS_ARE_SAME && BoundingBoxSize.Y < THRESH_POINTS_ARE_SAME && BoundingBoxSize.Z < THRESH_POINTS_ARE_SAME)
	{
		return nullptr;
	}

	SkeletalMesh->PreEditChange(nullptr);

	FSkeletalMeshModel *ImportedResource = SkeletalMesh->GetImportedModel();
	ImportedResource->LODModels.Empty();
	ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());

	FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels[0];

	// Process materials from import data
	ProcessImportMeshMaterials(SkeletalMesh->Materials, SkelMeshImportData);

	// Process reference skeleton from import data
	int32 SkeletalDepth = 0;
	if (!ProcessImportMeshSkeleton(SkeletalMesh->Skeleton, SkeletalMesh->RefSkeleton, SkeletalDepth, SkelMeshImportData))
	{
		return nullptr;
	}

	// Process bones influence (normalization and optimization); this is not strictly needed for SkeletalMesh to work
	ProcessImportMeshInfluences(SkelMeshImportData);

	// Serialize the import data when needed
	//LODModel.RawSkeletalMeshBulkData.SaveRawMesh(SkelMeshImportData);

	SkeletalMesh->ResetLODInfo();
	FSkeletalMeshLODInfo& NewLODInfo = SkeletalMesh->AddLODInfo();
	NewLODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
	NewLODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
	NewLODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
	NewLODInfo.LODHysteresis = 0.02f;

	SkeletalMesh->SetImportedBounds(FBoxSphereBounds(BoundingBox));

	// Store whether or not this mesh has vertex colors
	SkeletalMesh->bHasVertexColors = SkelMeshImportData.bHasVertexColors;
	SkeletalMesh->VertexColorGuid = SkeletalMesh->bHasVertexColors ? FGuid::NewGuid() : FGuid();

	// Pass the number of texture coordinate sets to the LODModel.  Ensure there is at least one UV coord
	LODModel.NumTexCoords = FMath::Max<uint32>(1, SkelMeshImportData.NumTexCoords);

	// Create the render data
	{
		TArray<FVector> LODPoints;
		TArray<SkeletalMeshImportData::FMeshWedge> LODWedges;
		TArray<SkeletalMeshImportData::FMeshFace> LODFaces;
		TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
		TArray<int32> LODPointToRawMap;
		SkelMeshImportData.CopyLODImportData(LODPoints,LODWedges,LODFaces,LODInfluences,LODPointToRawMap);

		IMeshUtilities::MeshBuildOptions BuildOptions;
		// #ueent_todo: Normals and tangents shouldn't need to be recomputed when they are retrieved from USD
		//BuildOptions.bComputeNormals = !SkelMeshImportData.bHasNormals;
		//BuildOptions.bComputeTangents = !SkelMeshImportData.bHasTangents;

		IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");

		TArray<FText> WarningMessages;
		TArray<FName> WarningNames;

		bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(ImportedResource->LODModels[0], SkeletalMesh->RefSkeleton, LODInfluences, LODWedges, LODFaces, LODPoints, LODPointToRawMap, BuildOptions, &WarningMessages, &WarningNames);
		if( !bBuildSuccess )
		{
			SkeletalMesh->MarkPendingKill();
			return nullptr;
		}

		SkeletalMesh->CalculateInvRefMatrices();
		SkeletalMesh->PostEditChange();
	}

	if (SkeletalMesh->RefSkeleton.GetRawBoneNum() == 0)
	{
		SkeletalMesh->MarkPendingKill();
		return nullptr;
	}

	// Generate a Skeleton and associate it to the SkeletalMesh
	{
		USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, ObjectFlags | EObjectFlags::RF_Public );

		Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);

		SkeletalMesh->Skeleton = Skeleton;

		// Generate physics asset if needed
	}

	return SkeletalMesh;
}

#endif // #if USE_USD_SDK
