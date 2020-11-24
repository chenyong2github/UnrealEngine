// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryFlowCoreNodes.h"
#include "GeometryFlowNodeUtil.h"
#include "DataTypes/DynamicMeshData.h"


namespace UE
{
namespace GeometryFlow
{







class FProcessMeshBaseNode : public FNode
{
public:
	static const FString InParamMesh() { return TEXT("Mesh"); }
	static const FString OutParamResultMesh() { return TEXT("ResultMesh"); }

public:
	FProcessMeshBaseNode()
	{
		AddInput(InParamMesh(), MakeUnique<FDynamicMeshInput>());

		AddOutput(OutParamResultMesh(), MakeUnique<FDynamicMeshOutput>());
	}


protected:

	//
	// FProcessMeshBaseNode API that subclasses must/can implement
	//

	virtual void ProcessMesh(
		const FNamedDataMap& DatasIn,
		const FDynamicMesh3& MeshIn,
		FDynamicMesh3& MeshOut)
	{
		check(false);		// subclass must implement ProcessMesh()
	}

	virtual void ProcessMeshInPlace(
		const FNamedDataMap& DatasIn,
		FDynamicMesh3& MeshInOut)
	{
		ensureMsgf(false, TEXT("TProcessMeshBaseNode::ProcessMeshInPlace called but not defined!"));
	}


	virtual void CheckAdditionalInputs(const FNamedDataMap& DatasIn, bool& bRecomputeRequired, bool& bAllInputsValid)
	{
		// none
	}

public:

	virtual void Evaluate(
		const FNamedDataMap& DatasIn,
		FNamedDataMap& DatasOut,
		TUniquePtr<FEvaluationInfo>& EvaluationInfo) override
	{
		if (ensure(DatasOut.Contains(OutParamResultMesh())))
		{
			bool bAllInputsValid = true;
			bool bRecomputeRequired = ( IsOutputAvailable(OutParamResultMesh()) == false );
			TSafeSharedPtr<IData> MeshArg = FindAndUpdateInputForEvaluate(InParamMesh(), DatasIn, bRecomputeRequired, bAllInputsValid);
			CheckAdditionalInputs(DatasIn, bRecomputeRequired, bAllInputsValid);
			if (bAllInputsValid)
			{
				if (bRecomputeRequired)
				{
					bool bIsMeshMutable = DatasIn.GetDataFlags(InParamMesh()).bIsMutableData;
					if (bIsMeshMutable)
					{
						UE_LOG(LogTemp, Warning, TEXT("[%s]  RECOMPUTING MeshOp In Place!"), *GetIdentifier());

						FDynamicMesh3 EditableMesh;
						MeshArg->GiveTo<FDynamicMesh3>(EditableMesh, (int)EMeshProcessingDataTypes::DynamicMesh);
						ProcessMeshInPlace(DatasIn, EditableMesh);

						// store new result
						TSafeSharedPtr<FDataDynamicMesh> Result = MakeSafeShared<FDataDynamicMesh>();
						Result->MoveData(MoveTemp(EditableMesh));
						SetOutput(OutParamResultMesh(), Result);
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("[%s]  RECOMPUTING MeshOp"), *GetIdentifier());

						// do we ever want to support using a copy of the source mesh?
						const FDynamicMesh3& SourceMesh = MeshArg->GetDataConstRef<FDynamicMesh3>((int)EMeshProcessingDataTypes::DynamicMesh);

						// run mesh processing
						FDynamicMesh3 ResultMesh;
						ProcessMesh(DatasIn, SourceMesh, ResultMesh);

						// store new result
						TSafeSharedPtr<FDataDynamicMesh> Result = MakeSafeShared<FDataDynamicMesh>();
						Result->MoveData(MoveTemp(ResultMesh));
						SetOutput(OutParamResultMesh(), Result);
					}

					EvaluationInfo->CountCompute(this);
				}
				DatasOut.SetData(OutParamResultMesh(), GetOutput(OutParamResultMesh()));
			}
		}
	}
};











template<typename SettingsType>
class TProcessMeshWithSettingsBaseNode : public FNode
{
public:
	using SettingsDataType = TMovableData<SettingsType, SettingsType::DataTypeIdentifier>;

public:
	static const FString InParamMesh() { return TEXT("Mesh"); }
	static const FString InParamSettings() { return TEXT("Settings"); }
	static const FString OutParamResultMesh() { return TEXT("ResultMesh"); }

protected:
	TProcessMeshWithSettingsBaseNode()
	{
		AddInput(InParamMesh(), MakeUnique<FDynamicMeshInput>());
		AddInput(InParamSettings(), MakeUnique<TBasicNodeInput<SettingsType, SettingsType::DataTypeIdentifier>>());

		AddOutput(OutParamResultMesh(), MakeUnique<FDynamicMeshOutput>());
	}


	virtual void ProcessMesh(
		const FNamedDataMap& DatasIn,
		const SettingsType& SettingsIn,
		const FDynamicMesh3& MeshIn,
		FDynamicMesh3& MeshOut)
	{
	}

	virtual void ProcessMeshInPlace(
		const FNamedDataMap& DatasIn,
		const SettingsType& SettingsIn,
		FDynamicMesh3& MeshInOut)
	{
		ensureMsgf(false, TEXT("TProcessMeshWithSettingsBaseNode::ProcessMeshInPlace called but not defined!"));
	}


	virtual void CheckAdditionalInputs(const FNamedDataMap& DatasIn, bool& bRecomputeRequired, bool& bAllInputsValid)
	{
		// none
	}

public:

	virtual void Evaluate(
		const FNamedDataMap& DatasIn,
		FNamedDataMap& DatasOut,
		TUniquePtr<FEvaluationInfo>& EvaluationInfo) override
	{
		if (ensure(DatasOut.Contains(OutParamResultMesh())))
		{
			bool bAllInputsValid = true;
			bool bRecomputeRequired = ( IsOutputAvailable(OutParamResultMesh()) == false );
			TSafeSharedPtr<IData> MeshArg = FindAndUpdateInputForEvaluate(InParamMesh(), DatasIn, bRecomputeRequired, bAllInputsValid);
			TSafeSharedPtr<IData> SettingsArg = FindAndUpdateInputForEvaluate(InParamSettings(), DatasIn, bRecomputeRequired, bAllInputsValid);
			CheckAdditionalInputs(DatasIn, bRecomputeRequired, bAllInputsValid);
			if (bAllInputsValid)
			{
				if (bRecomputeRequired)
				{
					// always make a copy of settings
					SettingsType Settings;
					SettingsArg->GetDataCopy(Settings, SettingsType::DataTypeIdentifier);

					bool bIsMeshMutable = DatasIn.GetDataFlags(InParamMesh()).bIsMutableData;
					if (bIsMeshMutable)
					{
						UE_LOG(LogTemp, Warning, TEXT("[%s]  RECOMPUTING MeshOp In Place!"), *GetIdentifier());

						FDynamicMesh3 EditableMesh;
						MeshArg->GiveTo<FDynamicMesh3>(EditableMesh, (int)EMeshProcessingDataTypes::DynamicMesh);
						ProcessMeshInPlace(DatasIn, Settings, EditableMesh);

						// store new result
						TSafeSharedPtr<FDataDynamicMesh> Result = MakeSafeShared<FDataDynamicMesh>();
						Result->MoveData(MoveTemp(EditableMesh));
						SetOutput(OutParamResultMesh(), Result);
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("[%s]  RECOMPUTING MeshOp"), *GetIdentifier());

						// do we ever want to support using a copy of the source mesh?
						const FDynamicMesh3& SourceMesh = MeshArg->GetDataConstRef<FDynamicMesh3>((int)EMeshProcessingDataTypes::DynamicMesh);

						// run mesh processing
						FDynamicMesh3 ResultMesh;
						ProcessMesh(DatasIn, Settings, SourceMesh, ResultMesh);

						// store new result
						TSafeSharedPtr<FDataDynamicMesh> Result = MakeSafeShared<FDataDynamicMesh>();
						Result->MoveData(MoveTemp(ResultMesh));
						SetOutput(OutParamResultMesh(), Result);
					}

					EvaluationInfo->CountCompute(this);
				}
				DatasOut.SetData(OutParamResultMesh(), GetOutput(OutParamResultMesh()));
			}
		}
	}
};



















}	// end namespace GeometryFlow
}	// end namespace UE


