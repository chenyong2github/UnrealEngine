// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryFlowNodeUtil.h"
#include "DataTypes/DynamicMeshData.h"
#include "DataTypes/IndexSetsData.h"


namespace UE
{
namespace GeometryFlow
{


class FMakeTriangleSetsFromMeshNode : public FNode
{
public:
	static const FString InParam() { return TEXT("Mesh"); }

	static const FString OutParamIndexSets() { return TEXT("IndexSets"); }

public:
	FMakeTriangleSetsFromMeshNode()
	{
		AddInput(InParam(), MakeUnique<FDynamicMeshInput>());
		
		AddOutput(OutParamIndexSets(), MakeBasicOutput<FIndexSets>());
	}

	virtual void Evaluate(
		const FNamedDataMap& DatasIn,
		FNamedDataMap& DatasOut,
		TUniquePtr<FEvaluationInfo>& EvaluationInfo) override
	{
		if (ensure(DatasOut.Contains(OutParamIndexSets())))
		{
			bool bAllInputsValid = true;
			bool bRecomputeRequired = (IsOutputAvailable(OutParamIndexSets()) == false);
			TSafeSharedPtr<IData> MeshArg = FindAndUpdateInputForEvaluate(InParam(), DatasIn, bRecomputeRequired, bAllInputsValid);
			if (bAllInputsValid)
			{
				if (bRecomputeRequired)
				{
					const FDynamicMesh3& Mesh = MeshArg->GetDataConstRef<FDynamicMesh3>((int)EMeshProcessingDataTypes::DynamicMesh);

					FIndexSets NewSets;
					ComputeIndexSets(DatasIn, Mesh, NewSets);

					SetOutput(OutParamIndexSets(), MakeMovableData<FIndexSets>(MoveTemp(NewSets)));
					EvaluationInfo->CountCompute(this);
				}
				DatasOut.SetData(OutParamIndexSets(), GetOutput(OutParamIndexSets()));
			}
		}
	}


	virtual void CheckAdditionalInputs(const FNamedDataMap& DatasIn, bool& bRecomputeRequired, bool& bAllInputsValid)
	{
		// none
	}


	virtual void ComputeIndexSets(const FNamedDataMap& DatasIn, const FDynamicMesh3& Mesh, FIndexSets& SetsOut)
	{
		SetsOut.IndexSets.SetNum(1);
		SetsOut.IndexSets[0].Reserve(Mesh.TriangleCount());
		for (int32 tid : Mesh.TriangleIndicesItr())
		{
			SetsOut.IndexSets[0].Add(tid);
		}
	}

};




class FMakeTriangleSetsFromGroupsNode : public FMakeTriangleSetsFromMeshNode
{
public:
	static const FString InParamIgnoreGroups() { return TEXT("IgnoreGroups"); }


public:
	FMakeTriangleSetsFromGroupsNode() : FMakeTriangleSetsFromMeshNode()
	{
		AddInput(InParamIgnoreGroups(), MakeBasicInput<FIndexSets>());
	}

	virtual void CheckAdditionalInputs(const FNamedDataMap& DatasIn, bool& bRecomputeRequired, bool& bAllInputsValid) override
	{
		FindAndUpdateInputForEvaluate(InParamIgnoreGroups(), DatasIn, bRecomputeRequired, bAllInputsValid);
	}


public:
	
	virtual void ComputeIndexSets(const FNamedDataMap& DatasIn, const FDynamicMesh3& Mesh, FIndexSets& SetsOut) override
	{
		TSafeSharedPtr<IData> IgnoreGroupsArg = DatasIn.FindData(InParamIgnoreGroups());
		const FIndexSets& IgnoreGroupsSets = IgnoreGroupsArg->GetDataConstRef<FIndexSets>(FIndexSets::DataTypeIdentifier);
		TSet<int32> IgnoreGroups;
		IgnoreGroupsSets.GetAllValues(IgnoreGroups);

		TMap<int32, int32> GroupsMap;
		TArray<int32> GroupCounts;
		int32 NumGroups = 0;
		for (int32 tid : Mesh.TriangleIndicesItr())
		{
			int32 GroupID = Mesh.GetTriangleGroup(tid);
			if (IgnoreGroups.Contains(GroupID))
			{
				continue;
			}

			int32* FoundIndex = GroupsMap.Find(GroupID);
			if (FoundIndex == nullptr)
			{
				int32 Index = NumGroups++;
				GroupsMap.Add(GroupID, Index);
				GroupCounts.Add(1);
			}
			else
			{
				GroupCounts[*FoundIndex]++;
			}
		}

		SetsOut.IndexSets.SetNum(NumGroups);
		for (int32 k = 0; k < NumGroups; ++k)
		{
			SetsOut.IndexSets[k].Reserve(GroupCounts[k]);
		}

		for (int32 tid : Mesh.TriangleIndicesItr())
		{
			int32 GroupID = Mesh.GetTriangleGroup(tid);
			if (IgnoreGroups.Contains(GroupID))
			{
				continue;
			}

			int32* FoundIndex = GroupsMap.Find(GroupID);
			SetsOut.IndexSets[*FoundIndex].Add(tid);
		}
	}

};




}	// end namespace GeometryFlow
}	// end namespace UE