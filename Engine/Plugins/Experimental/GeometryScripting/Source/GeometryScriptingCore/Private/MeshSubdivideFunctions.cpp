// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshSubdivideFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "Operations/PNTriangles.h"
#include "Operations/UniformTessellate.h"
#include "Operations/AdaptiveTessellate.h"
#include "UDynamicMesh.h"
#include "Math/UnrealMathUtility.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshSubdivideFunctions"


UDynamicMesh* UGeometryScriptLibrary_MeshSubdivideFunctions::ApplyRecursivePNTessellation(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPNTessellateOptions Options,
	int NumIterations,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyPNTessellation_InvalidInput", "ApplyPNTessellation: TargetMesh is Null"));
		return TargetMesh;
	}
	if (NumIterations <= 0)
	{
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		FPNTriangles Tessellator(&EditMesh);
		
		// Convert the number of recursive subdivisions to the equivalent tessellation level to make sure that we 
		// produce the same topology. 
		int TessellationLevel = static_cast<int>(FMath::RoundHalfFromZero(FMath::Exp2((double)NumIterations))) - 1;
		Tessellator.TessellationLevel = TessellationLevel;
		Tessellator.Compute();

		if (Options.bRecomputeNormals && EditMesh.HasAttributes() && EditMesh.Attributes()->PrimaryNormals() != nullptr)
		{
			FMeshNormals MeshNormals(&EditMesh);
			MeshNormals.RecomputeOverlayNormals(EditMesh.Attributes()->PrimaryNormals(), true, true);
			MeshNormals.CopyToOverlay(EditMesh.Attributes()->PrimaryNormals(), false);
		}

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}

UDynamicMesh* UGeometryScriptLibrary_MeshSubdivideFunctions::ApplyPNTessellation(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPNTessellateOptions Options,
	int TessellationLevel,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyPNTessellation_InvalidInput", "ApplyPNTessellation: TargetMesh is Null"));
		return TargetMesh;
	}
	if (TessellationLevel <= 0)
	{
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		FPNTriangles Tessellator(&EditMesh);
		Tessellator.TessellationLevel = TessellationLevel;
		Tessellator.bRecalculateNormals = Options.bRecomputeNormals;
		
		if (Tessellator.Validate() != EOperationValidationResult::Ok)
		{
			UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyPNTessellation_Error", "ApplyPNTessellation: The inputs are invalid"));
			return;
		}

		if (Tessellator.Compute() == false) 
		{
			UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::OperationFailed, LOCTEXT("ApplyPNTessellation_Failed", "ApplyPNTessellation: Tessellation failed"));
			return;
		}
		
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}

UDynamicMesh* UGeometryScriptLibrary_MeshSubdivideFunctions::ApplyUniformTessellation(
	UDynamicMesh* TargetMesh,
	int TessellationLevel,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyUniformTessellation_InvalidInput", "ApplyUniformTessellation: TargetMesh is Null"));
		return TargetMesh;
	}
	if (TessellationLevel <= 0)
	{
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		FUniformTessellate Tessellator(&EditMesh);
		Tessellator.TessellationNum = TessellationLevel;
		
		if (Tessellator.Validate() != EOperationValidationResult::Ok)
		{
			UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyUniformTessellation_Error", "ApplyUniformTessellation: The inputs are invalid"));
			return;
		} 

		if (Tessellator.Compute() == false) 
		{
			UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::OperationFailed, LOCTEXT("ApplyUniformTessellation_Failed", "ApplyUniformTessellation: Tessellation failed")); 
		}
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}


UDynamicMesh* UGeometryScriptLibrary_MeshSubdivideFunctions::ApplyAdaptiveTessellation(
	UDynamicMesh* TargetMesh,
	FGeometryScriptAdaptiveTessellateOptions Options,
	FGeometryScriptIndexList IndexList,
	int TessellationLevel,
	EAdaptiveTessellatePatternType PatternType,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyAdapativeTessellation_InvalidInput", "ApplyAdapativeTessellation: TargetMesh is Null"));
		return TargetMesh;
	}
	if (TessellationLevel <= 0)
	{
		return TargetMesh;
	}
	if (PatternType != EAdaptiveTessellatePatternType::ConcentricRings)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyAdapativeTessellation_InvalidPatternType", "Only ConcentricRings pattern is currently supported"));
		return TargetMesh;
	} 
	if (IndexList.IndexType != EGeometryScriptIndexType::Triangle)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyAdapativeTessellation_InvalidSelectionType", "Only Triangle selection is currently supported"));
		return TargetMesh;
	} 

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{	
		FDynamicMesh3 TessellatedMesh;
		FAdaptiveTessellate Tessellator(&EditMesh, &TessellatedMesh);

		TSharedPtr<TArray<int>> List = IndexList.List;
		TUniquePtr<FTessellationPattern> Pattern; 
		if (List == nullptr) //if list is not provided then tessellate the whole mesh
		{	
			switch(PatternType) 
			{
				case EAdaptiveTessellatePatternType::Uniform:
				{
					Pattern = nullptr; //TODO: implement
					break;
				}
				case EAdaptiveTessellatePatternType::InnerUniform: 
				{
					Pattern = nullptr; //TODO: implement
					break;
				}
				case EAdaptiveTessellatePatternType::ConcentricRings:
				{
					Pattern = FAdaptiveTessellate::CreateConcentricRingsTessellationPattern(&EditMesh, TessellationLevel);
					break;
				} 
				default:
				{
					checkSlow(false);
				}
			}
		}
		else
		{
			switch(PatternType) 
			{
				case EAdaptiveTessellatePatternType::Uniform:
				{
					Pattern = nullptr; //TODO: implement
					break;
				}
				case EAdaptiveTessellatePatternType::InnerUniform: 
				{
					Pattern = nullptr; //TODO: implement
					break;
				}
				case EAdaptiveTessellatePatternType::ConcentricRings:
				{
					Pattern = FAdaptiveTessellate::CreateConcentricRingsTessellationPattern(&EditMesh, TessellationLevel, *List);
					break;
				} 
				default:
				{
					checkSlow(false);
				}
			}
		}

		Tessellator.SetPattern(Pattern.Get());
		Tessellator.bUseParallel = Options.bEnableMultithreading;
		
		if (Tessellator.Validate() != EOperationValidationResult::Ok)
		{
			UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyAdapativeTessellation_Error", "ApplyAdapativeTessellation: The inputs are invalid"));
			return;
		} 

		if (Tessellator.Compute() == false)
		{
			UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::OperationFailed, LOCTEXT("ApplyAdapativeTessellate_Failed", "ApplyAdapativeTessellate: Tessellation failed")); 
		}
		else 
		{
			EditMesh = MoveTemp(TessellatedMesh);
		}

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}

#undef LOCTEXT_NAMESPACE
