// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/TransferBoneWeights.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "DynamicMesh/DynamicBoneAttribute.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Async/ParallelFor.h"
#include "Util/ProgressCancel.h"
#include "BoneIndices.h"
#include "BoneWeights.h"
#include "IndexTypes.h"
#include "TransformTypes.h"
#include "Solvers/Internal/QuadraticProgramming.h"
#include "Solvers/LaplacianMatrixAssembly.h"

using namespace UE::AnimationCore;
using namespace UE::Geometry;

namespace TransferBoneWeightsLocals 
{
	/**
	 * Given a triangle and point on a triangle (via barycentric coordinates), compute the bone weights for the point.
	 * 
	 * @param OutWeights Interpolated weights for a vertex with Bary barycentric coordinates
	 * @param TriVertices The vertices of a triangle containing the point we are interpolating the weights for
	 * @param Bary Barycentric coordinates of the point
	 * @param Attribute Attribute containing bone weights of the mesh that TriVertices belong to
	 * @param SourceIndexToBone Optional map from bone index to bone name for the source mesh
	 * @param TargetBoneToIndex OPtional map from bone name to bone index for the target mesh
	 * @param bNormalizeToOne If true, OutWeights will be normalized to sum to 1.
	 */
    void InterpolateBoneWeights(FBoneWeights& OutWeights,
								const FIndex3i& TriVertices,
								const FVector3f& Bary,
								const FDynamicMeshVertexSkinWeightsAttribute* Attribute,
								const TArray<FName>* SourceIndexToBone = nullptr,
								const TMap<FName, FBoneIndexType>* TargetBoneToIndex = nullptr,
								bool bNormalizeToOne = true)
	{
		FBoneWeights Weight1, Weight2, Weight3;
		Attribute->GetValue(TriVertices[0], Weight1);
		Attribute->GetValue(TriVertices[1], Weight2);
		Attribute->GetValue(TriVertices[2], Weight3);

		FBoneWeightsSettings BlendSettings;
		BlendSettings.SetNormalizeType(bNormalizeToOne ? EBoneWeightNormalizeType::Always : EBoneWeightNormalizeType::None);
		BlendSettings.SetBlendZeroInfluence(true);
		OutWeights = FBoneWeights::Blend(Weight1, Weight2, Weight3, Bary[0], Bary[1], Bary[2], BlendSettings);

		// Check if we need to remap the indices
		if (SourceIndexToBone && TargetBoneToIndex) 
		{
			FBoneWeightsSettings BoneSettings;
			BoneSettings.SetNormalizeType(EBoneWeightNormalizeType::None);

			FBoneWeights MappedWeights;

			for (int WeightIdx = 0; WeightIdx < OutWeights.Num(); ++WeightIdx)
			{
				const FBoneWeight& BoneWeight = OutWeights[WeightIdx];

				FBoneIndexType FromIdx = BoneWeight.GetBoneIndex();
				uint16 FromWeight = BoneWeight.GetRawWeight();

				checkSlow(FromIdx < SourceIndexToBone->Num());
				if (FromIdx < SourceIndexToBone->Num())
				{
					FName BoneName = (*SourceIndexToBone)[FromIdx];
					if (TargetBoneToIndex->Contains(BoneName))
					{
						FBoneIndexType ToIdx = (*TargetBoneToIndex)[BoneName];
						FBoneWeight MappedBoneWeight(ToIdx, FromWeight);
						MappedWeights.SetBoneWeight(MappedBoneWeight, BoneSettings);
					}
					else 
					{	
						UE_LOG(LogGeometry, Error, TEXT("FTransferBoneWeights: Bone name %s does not exist in the target mesh."), *BoneName.ToString());
					}
				}
			}


			if (MappedWeights.Num() == 0)
			{
				// If no bone mappings were found, add a single entry for the root bone
				MappedWeights.SetBoneWeight(FBoneWeight(0, 1.0f), FBoneWeightsSettings());
			}
			else if (OutWeights.Num() != MappedWeights.Num() && bNormalizeToOne)
			{	
				// In case some of the bones were not mapped we need to renormalize
				MappedWeights.Renormalize(FBoneWeightsSettings());
			}

			OutWeights = MappedWeights;
		}
	}

	
	FDynamicMeshVertexSkinWeightsAttribute* GetOrCreateSkinWeightsAttribute(FDynamicMesh3& InMesh, const FName& InProfileName)
	{
		checkSlow(InMesh.HasAttributes());
		FDynamicMeshVertexSkinWeightsAttribute* Attribute = InMesh.Attributes()->GetSkinWeightsAttribute(InProfileName);
		if (Attribute == nullptr)
		{
			Attribute = new FDynamicMeshVertexSkinWeightsAttribute(&InMesh);
			InMesh.Attributes()->AttachSkinWeightsAttribute(InProfileName, Attribute);
		}
		return Attribute;
	}
}

FTransferBoneWeights::FTransferBoneWeights(const FDynamicMesh3* InSourceMesh, 
								   const FName& InSourceProfileName,
								   const FDynamicMeshAABBTree3* InSourceBVH)
:
SourceMesh(InSourceMesh),
SourceProfileName(InSourceProfileName),
SourceBVH(InSourceBVH)
{
	// If the BVH for the source mesh was not specified then create one
	if (SourceBVH == nullptr)
	{
		InternalSourceBVH = MakeUnique<FDynamicMeshAABBTree3>(SourceMesh);
	}
}

FTransferBoneWeights::~FTransferBoneWeights() 
{
}

bool FTransferBoneWeights::Cancelled()
{
	return (Progress == nullptr) ? false : Progress->Cancelled();
}

EOperationValidationResult FTransferBoneWeights::Validate()
{	
	if (SourceMesh == nullptr) 
	{
		return EOperationValidationResult::Failed_UnknownReason;
	}

	// Either BVH was passed by the caller or was created internally in the constructor
	if (SourceBVH == nullptr && InternalSourceBVH.IsValid() == false) 
	{
		return EOperationValidationResult::Failed_UnknownReason;
	}

	if (SourceMesh->HasAttributes() == false) 
	{
		return EOperationValidationResult::Failed_UnknownReason;
	}
	
	if (SourceMesh->Attributes()->GetSkinWeightsAttribute(SourceProfileName) == nullptr) 
	{
		return EOperationValidationResult::Failed_UnknownReason;
	}

	if (bIgnoreBoneAttributes == false && SourceMesh->Attributes()->HasBones() == false) 
	{
		return EOperationValidationResult::Failed_UnknownReason;
	}

	if (NormalThreshold >= 0 && !SourceMesh->Attributes()->PrimaryNormals())
	{
		return EOperationValidationResult::Failed_UnknownReason;
	}

	return EOperationValidationResult::Ok;
}

bool FTransferBoneWeights::TransferWeightsToMesh(FDynamicMesh3& InOutTargetMesh, const FName& InTargetProfileName)
{	
	if (Validate() != EOperationValidationResult::Ok) 
	{
		return false;
	}

	if (!InOutTargetMesh.HasAttributes())
	{
		InOutTargetMesh.EnableAttributes(); 
	}

	if (!bIgnoreBoneAttributes && !InOutTargetMesh.Attributes()->HasBones())
	{
		return false; // the target mesh must have bone attributes
	}

	if (NormalThreshold >= 0 && !InOutTargetMesh.Attributes()->PrimaryNormals())
	{
		return false; // the target mesh must have normal attributes if we are comparing source and target normals
	}
	
	FDynamicMeshVertexSkinWeightsAttribute* TargetSkinWeights = TransferBoneWeightsLocals::GetOrCreateSkinWeightsAttribute(InOutTargetMesh, InTargetProfileName);
	checkSlow(TargetSkinWeights);
	
	// Map the bone name to its index for the target mesh.
	// Will be null if either the target and the source skeletons are the same or the caller forced the attributes to be ignored
	TUniquePtr<TMap<FName, uint16>> TargetBoneToIndex;
	if (!bIgnoreBoneAttributes)
	{	
		const TArray<FName>& SourceBoneNames = SourceMesh->Attributes()->GetBoneNames()->GetAttribValues();
		const TArray<FName>& TargetBoneNames = InOutTargetMesh.Attributes()->GetBoneNames()->GetAttribValues();

		if (SourceBoneNames != TargetBoneNames)
		{
			TargetBoneToIndex = MakeUnique<TMap<FName, uint16>>();
			TargetBoneToIndex->Reserve(TargetBoneNames.Num());

			for (int BoneID = 0; BoneID < TargetBoneNames.Num(); ++BoneID)
			{
				const FName& BoneName = TargetBoneNames[BoneID];
				if (TargetBoneToIndex->Contains(BoneName))
				{
					checkSlow(false);
					return false; // there should be no duplicates
				}
				TargetBoneToIndex->Add(BoneName, static_cast<uint16>(BoneID));
			}
		}
	}

	bool bFailed = false;
	
	MatchedVertices.Init(false, InOutTargetMesh.MaxVertexID());

	if (TransferMethod == ETransferBoneWeightsMethod::ClosestPointOnSurface)
	{
		ParallelFor(InOutTargetMesh.MaxVertexID(), [&](int32 VertexID)
		{
			if (Cancelled()) 
			{
				return;
			}
			
			if (InOutTargetMesh.IsVertex(VertexID)) 
			{
				const FVector3d Point = InOutTargetMesh.GetVertex(VertexID);
				const FVector3f Normal = NormalThreshold >= 0 ? InOutTargetMesh.Attributes()->PrimaryNormals()->GetElement(VertexID) : FVector3f::Zero();

				FBoneWeights Weights;
				if (TransferWeightsToPoint(Weights, Point, TargetBoneToIndex.Get(), Normal))
				{
					TargetSkinWeights->SetValue(VertexID, Weights);
					MatchedVertices[VertexID] = true;
				}
			}

		}, bUseParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);

		// If the caller requested to simply find the closest point for all vertices then the number of matched vertices
		// must be equal to the target mesh vertex count
		if (SearchRadius < 0 && NormalThreshold < 0)
		{
			int NumMatched = 0;
			for (bool Flag : MatchedVertices)
			{
				if (Flag) 
				{ 
					NumMatched++;
				}
			}
			bFailed = NumMatched != InOutTargetMesh.VertexCount();
		}
	} 
	else if (TransferMethod == ETransferBoneWeightsMethod::InpaintWeights)
	{
		/**
         *  Given two meshes, Mesh1 without weights and Mesh2 with weights, assume they are aligned in 3d space.
         *  For every vertex on Mesh1 find the closest point on the surface of Mesh2 within a radius R. If the difference 
         *  between the normals of the two points is below the threshold, then it's a match. Otherwise no match.
         *  So now we have two sets of vertices on Mesh1. One with a match on the source mesh and one without a match.
         *  For all the vertices with a match, copy weights over. For all the vertices without the match, do nothing.
         *  Now, for all the vertices without a match, try to approximate the weights by smoothly interpolating between 
         *  the weights at the known vertices via solving a quadratic problem. 
         *  
         *  The solver minimizes an energy
         *      trace(W^t Q W)
         *      W \in R^(nxm) is a matrix where n is the number of vertices and m is the number of bones. So (i,j) entry is 
         *                    the influence (weight) of a vertex i by bone j
         *      Q \in R^(nxn) is a matrix that combines both Dirichlet and Laplacian energies, Q = -0.5L + 0.5LM^-1L
         *                    where L is a cotangent Laplacian and M is a mass matrix
         *  
         *  subject to constraints
         *      All weights at a single vertex sum to 1: sum(W(i,:)) = 1
         *      All weights must be non-negative: W(i,j) >=0 for any i, j
         *      Any vertex for which we found a match must have fixed weights that can't be changed, 
         *      i.e. W(i,j) = KnownWeights(i,j) where i is a vertex for which we found a match on the body.
		 */
		
		// For every vertex on the target mesh try to find the match on the source mesh using the distance and normal checks
		ParallelFor(InOutTargetMesh.MaxVertexID(), [&](int32 VertexID)
		{
			if (Cancelled()) 
			{
				return;
			}
			
			if (InOutTargetMesh.IsVertex(VertexID)) 
			{
				const FVector3d Point = InOutTargetMesh.GetVertex(VertexID);
				const FVector3f Normal = NormalThreshold >= 0 ? InOutTargetMesh.Attributes()->PrimaryNormals()->GetElement(VertexID) : FVector3f::Zero();

				FBoneWeights Weights;
				if (TransferWeightsToPoint(Weights, Point, TargetBoneToIndex.Get(), Normal))
				{
					TargetSkinWeights->SetValue(VertexID, Weights);
					MatchedVertices[VertexID] = true;
				}
			}
		}, bUseParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);

		if (Cancelled()) 
		{
			return false;
		}

		// Compute linearization so we can store constraints at linearized indices
		FVertexLinearization VtxLinearization(InOutTargetMesh, false);
		const TArray<int32>& ToMeshV = VtxLinearization.ToId();
		const TArray<int32>& ToIndex = VtxLinearization.ToIndex();
		
		int32 NumMatched = 0;
		for (bool Flag : MatchedVertices)
		{
			if (Flag) 
			{ 
				NumMatched++;
			}
		}

		// If all vertices were matched then nothing else to do
		if (NumMatched == InOutTargetMesh.VertexCount())
		{
			return true;
		}

		// Setup the sparse matrix FixedValues of known (matched) weight values and the array (FixedIndices) of the matched vertex IDs
		const int TargetNumBones = InOutTargetMesh.Attributes()->GetBoneNames()->GetAttribValues().Num();
		FSparseMatrixD FixedValues;
		FixedValues.resize(NumMatched, TargetNumBones);
		std::vector<Eigen::Triplet<FSparseMatrixD::Scalar>> FixedValuesTriplets;
		FixedValuesTriplets.reserve(NumMatched);
		
		TArray<int> FixedIndices;
		FixedIndices.Reserve(NumMatched);

		for (int VertexID = 0; VertexID < InOutTargetMesh.MaxVertexID(); ++VertexID)
		{
			if (InOutTargetMesh.IsVertex(VertexID) && MatchedVertices[VertexID])
			{
				FBoneWeights Data;
				TargetSkinWeights->GetValue(VertexID, Data);

				const int NumBones = Data.Num();
				checkSlow(NumBones > 0);
				const int CurIdx = FixedIndices.Num();
				for (int BoneID = 0; BoneID < NumBones; ++BoneID)
				{
					const int BoneIdx = Data[BoneID].GetBoneIndex();
					const double BoneWeight = (double)Data[BoneID].GetWeight();
					FixedValuesTriplets.emplace_back(CurIdx, BoneIdx, BoneWeight);
				}

				checkSlow(VertexID < ToIndex.Num());
				FixedIndices.Add(ToIndex[VertexID]);
			}
		}
		FixedValues.setFromTriplets(FixedValuesTriplets.begin(), FixedValuesTriplets.end());

		// Setup the Laplacian matrix
		const int32 NumVerts = VtxLinearization.NumVerts();
		FEigenSparseMatrixAssembler LaplacianAssembler(NumVerts, NumVerts);
		UE::MeshDeformation::ConstructFullCotangentLaplacian<double>(InOutTargetMesh, 
																	 VtxLinearization, 
																	 LaplacianAssembler,
																	 UE::MeshDeformation::ECotangentWeightMode::Default,
																	 UE::MeshDeformation::ECotangentAreaMode::NoArea);
		FSparseMatrixD CotangentMatrix;
		LaplacianAssembler.ExtractResult(CotangentMatrix);
		FSparseMatrixD NegativeCotangentMatrix = -0.25 * CotangentMatrix; //TODO: Add LM^-1L term in the future

		// Solve the QP problem with fixed constraints
		bFailed = true;
		FQuadraticProgramming QProgram(&NegativeCotangentMatrix);
		if (ensure(QProgram.SetFixedConstraints(&FixedIndices, &FixedValues)))
		{
			if (ensure(QProgram.PreFactorize()))
			{
				FDenseMatrixD TargetWeights;
				if (ensure(QProgram.Solve(TargetWeights)))
				{
					FBoneWeightsSettings BoneSettings;
					BoneSettings.SetNormalizeType(EBoneWeightNormalizeType::None);
					for (int IdxI = 0; IdxI < TargetWeights.rows(); ++IdxI)
					{
						FBoneWeights WeightArray;
						for (int IdxJ = 0; IdxJ < TargetWeights.cols(); ++IdxJ)
						{
							const FBoneIndexType BoneId = static_cast<FBoneIndexType>(IdxJ);
							const float Weight = (float)TargetWeights(IdxI, IdxJ);
							if (Weight > KINDA_SMALL_NUMBER)
							{
								FBoneWeight Bweight(BoneId, Weight);
								WeightArray.SetBoneWeight(Bweight, BoneSettings);
							}
						}

						WeightArray.Renormalize(FBoneWeightsSettings());

						checkSlow(IdxI < ToMeshV.Num());
						TargetSkinWeights->SetValue(ToMeshV[IdxI], WeightArray);
					}

					bFailed = false;
				}
			}
		}
	}
	else 
	{
		checkNoEntry(); // unsupported method
	}

	if (Cancelled() || bFailed) 
	{
		return false;
	}
		
	return true;
}

bool FTransferBoneWeights::TransferWeightsToPoint(UE::AnimationCore::FBoneWeights& OutWeights, 
												  const FVector3d& InPoint, 
												  const TMap<FName, uint16>* TargetBoneToIndex,
												  const FVector3f& InNormal)
{	
	// Find the containing triangle and the barycentric coordinates of the closest point
	int32 TriID; 
	FVector3d Bary;
	if (!FindClosestPointOnSourceSurface(InPoint, TargetToWorld, TriID, Bary))
	{
		return false;
	}
	FVector3f BaryF = FVector3f((float)Bary[0], (float)Bary[1], (float)Bary[2]);
	const FIndex3i TriVertex = SourceMesh->GetTriangle(TriID);

	const FDynamicMeshVertexSkinWeightsAttribute* SourceSkinWeights = SourceMesh->Attributes()->GetSkinWeightsAttribute(SourceProfileName);
	const TArray<FName>* SourceBoneNames = nullptr;
	if (!bIgnoreBoneAttributes) 
	{
		SourceBoneNames = &SourceMesh->Attributes()->GetBoneNames()->GetAttribValues();
	}

	if (SearchRadius < 0 && NormalThreshold < 0)
	{
		// If the radius and normals are ignored, simply interpolate the weights and return the result
		TransferBoneWeightsLocals::InterpolateBoneWeights(OutWeights, TriVertex, BaryF, SourceSkinWeights, SourceBoneNames, TargetBoneToIndex);
	}
	else
	{
		bool bPassedRadiusCheck = true;
		if (SearchRadius >= 0)
		{
			const FVector3d MatchedPoint = SourceMesh->GetTriBaryPoint(TriID, Bary[0], Bary[1], Bary[2]);
			bPassedRadiusCheck = (InPoint - MatchedPoint).Length() <= SearchRadius;
		}

		bool bPassedNormalsCheck = true;
		if (NormalThreshold >= 0)
		{
			FVector3f Normal0, Normal1, Normal2;
			SourceMesh->Attributes()->PrimaryNormals()->GetTriElements(TriID, Normal0, Normal1, Normal2);

			const FVector3f MatchedNormal = Normalized(BaryF[0]*Normal0 + BaryF[1]*Normal1 + BaryF[2]*Normal2);
			const FVector3f InNormalNormalized = Normalized(InNormal);
			const float NormalAngle = FMathf::ACos(InNormalNormalized.Dot(MatchedNormal));
			bPassedNormalsCheck = (double)NormalAngle <= NormalThreshold;
		}
		
		if (bPassedRadiusCheck && bPassedNormalsCheck)
		{
			TransferBoneWeightsLocals::InterpolateBoneWeights(OutWeights, TriVertex, BaryF, SourceSkinWeights, SourceBoneNames, TargetBoneToIndex);
		}
		else
		{
			return false;
		}
	}

	return true;
}

bool FTransferBoneWeights::FindClosestPointOnSourceSurface(const FVector3d& InPoint, const FTransformSRT3d& InToWorld, int32& NearTriID, FVector3d& Bary)
{
	IMeshSpatial::FQueryOptions Options;
	double NearestDistSqr;
	
	const FVector3d WorldPoint = InToWorld.TransformPosition(InPoint);
	if (SourceBVH != nullptr) 
	{ 
		NearTriID = SourceBVH->FindNearestTriangle(WorldPoint, NearestDistSqr, Options);
	}
	else 
	{
		NearTriID = InternalSourceBVH->FindNearestTriangle(WorldPoint, NearestDistSqr, Options);
	}

	if (!ensure(NearTriID != IndexConstants::InvalidID))
	{
		return false;
	}

	const FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(*SourceMesh, NearTriID, WorldPoint);
	const FVector3d NearestPnt = Query.ClosestTrianglePoint;
	const FIndex3i TriVertex = SourceMesh->GetTriangle(NearTriID);

	Bary = VectorUtil::BarycentricCoords(NearestPnt, SourceMesh->GetVertexRef(TriVertex.A),
													 SourceMesh->GetVertexRef(TriVertex.B),
													 SourceMesh->GetVertexRef(TriVertex.C));

	return true;
}
