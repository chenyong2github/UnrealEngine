// Copyright Epic Games, Inc. All Rights Reserved.

#include "NearestNeighborModel.h"
#include "NearestNeighborModelInputInfo.h"
#include "NearestNeighborModelInstance.h"
#include "MLDeformerComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GeometryCache.h"
#include "Misc/FileHelper.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/ExternalMorphSet.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/MorphTargetVertexInfoBuffers.h"
#include "UObject/UObjectGlobals.h"
#include "NeuralNetwork.h"

#define LOCTEXT_NAMESPACE "UNearestNeighborModel"

NEARESTNEIGHBORMODEL_API DEFINE_LOG_CATEGORY(LogNearestNeighborModel)

namespace UE::NearestNeighborModel
{
	class NEARESTNEIGHBORMODEL_API FNearestNeighborModelModule
		: public IModuleInterface
	{
	};
}
IMPLEMENT_MODULE(UE::NearestNeighborModel::FNearestNeighborModelModule, NearestNeighborModel)


UNearestNeighborModel::UNearestNeighborModel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	SetVizSettings(ObjectInitializer.CreateEditorOnlyDefaultSubobject<UNearestNeighborModelVizSettings>(this, TEXT("VizSettings")));
#endif
}

UMLDeformerInputInfo* UNearestNeighborModel::CreateInputInfo()
{
	UNearestNeighborModelInputInfo* NearestNeighborModelInputInfo = NewObject<UNearestNeighborModelInputInfo>(this);
#if WITH_EDITORONLY_DATA
	NearestNeighborModelInputInfo->InitRefBoneRotations(GetSkeletalMesh());
#endif
	return NearestNeighborModelInputInfo;
}

UMLDeformerModelInstance* UNearestNeighborModel::CreateModelInstance(UMLDeformerComponent* Component)
{
	return NewObject<UNearestNeighborModelInstance>(Component);
}

void UNearestNeighborModel::PostLoad()
{
	Super::PostLoad();

	InitInputInfo();

#if WITH_EDITORONLY_DATA
	UpdateNetworkInputDim();
	UpdateNetworkOutputDim();
	UpdateNetworkSize();
	UpdateMorphTargetSize();
#endif
}


TArray<uint32> ReadTxt(const FString &Path)
{
	TArray<FString> StrArr;
	FFileHelper::LoadFileToStringArray(StrArr, *Path);
	TArray<uint32> UIntArr; UIntArr.SetNum(StrArr.Num());
	for (int32 i = 0; i < UIntArr.Num(); i++)
	{
		UIntArr[i] = FCString::Atoi(*StrArr[i]);
	}

	return UIntArr;
}

TArray<uint32> Range(const uint32 Start, const uint32 End)
{
	TArray<uint32> Arr; Arr.SetNum(End - Start + 1);
	for (uint32 i = Start; i < End; i++)
	{
		Arr[i - Start] = i;
	}
	return Arr;
}

TArray<uint32> AddConstant(const TArray<uint32> &InArr, uint32 Constant)
{
	TArray<uint32> OutArr = InArr;
	for (int32 i = 0; i < InArr.Num(); i++)
	{
		OutArr[i] = InArr[i] + Constant;
	}
	return OutArr;
}

void UNearestNeighborModel::ClipInputs(float* InputPtr, int NumInputs)
{
	if(InputsMin.Num() == NumInputs && InputsMax.Num() == NumInputs)
	{
		for(int32 i = 0; i < NumInputs; i++)
		{
			if (InputPtr[i] > InputsMax[i])
			{
				InputPtr[i] = InputsMax[i];
			}
			if (InputPtr[i] < InputsMin[i])
			{
				InputPtr[i] = InputsMin[i];
			}
		}
	}	
}

int32 UNearestNeighborModel::GetTotalNumPCACoeffs() const
{
	int32 TotalNumPCACoeffs = 0;
	for (int32 PartId = 0; PartId < GetNumParts(); PartId++)
	{
		TotalNumPCACoeffs += GetPCACoeffNum(PartId);
	}
	return TotalNumPCACoeffs;
}

int32 UNearestNeighborModel::GetTotalNumNeighbors() const
{
	int32 TotalNumNeighbors = 0;
	for (int32 PartId = 0; PartId < GetNumParts(); PartId++)
	{
		TotalNumNeighbors += GetNumNeighbors(PartId);
	}
	return TotalNumNeighbors;
}

class FMorphTargetBuffersInfo : public FMorphTargetVertexInfoBuffers
{
public:
	uint32 GetMorphDataSize() const { return MorphData.Num() * sizeof(uint32); }
	void ResetCPUData() { FMorphTargetVertexInfoBuffers::ResetCPUData(); }
};

void UNearestNeighborModel::ResetMorphBuffers()
{
	if (GetMorphTargetSet())
	{
		FMorphTargetBuffersInfo* MorphBuffersInfo = static_cast<FMorphTargetBuffersInfo*>(&GetMorphTargetSet()->MorphBuffers);
		MorphBuffersInfo->ResetCPUData();
	}
}

#if WITH_EDITORONLY_DATA
void UNearestNeighborModel::UpdatePCACoeffNums()
{
	uint32 PCACoeffStart = 0;
	for(int32 PartId = 0; PartId < GetNumParts(); PartId++)
	{
		ClothPartData[PartId].PCACoeffStart = PCACoeffStart;
		PCACoeffStart += ClothPartData[PartId].PCACoeffNum;
	}
}

void UNearestNeighborModel::UpdateNetworkInputDim()
{
	InputDim = 3 * GetBoneIncludeList().Num();
}

void UNearestNeighborModel::UpdateNetworkOutputDim()
{
	OutputDim = 0;
	for (int32 i = 0; i < GetNumParts(); i++)
	{
		OutputDim += ClothPartData[i].PCACoeffNum; 
	}
}

UE::NearestNeighborModel::EUpdateResult UNearestNeighborModel::UpdateVertexMap(int32 PartId, const FString& VertexMapPath, const FSkelMeshImportedMeshInfo& Info)
{
	using namespace UE::NearestNeighborModel;
	uint8 ReturnCode = EUpdateResult::SUCCESS;
	const int32 StartIndex = Info.StartImportedVertex;
	const int32 NumVertices = Info.NumVertices;
	bool bIsVertexMapValid = true;

	if (VertexMapPath.IsEmpty())
	{
		bIsVertexMapValid = false;
	}
	else if (!FPaths::FileExists(VertexMapPath))
	{
		bIsVertexMapValid = false;
		ReturnCode |= EUpdateResult::ERROR;
		UE_LOG(LogNearestNeighborModel, Error, TEXT("Part %d txt path %s does not exist"), PartId, *VertexMapPath, NumVertices);
	}
	else
	{
		const TArray<uint32> PartVertexMap = ReadTxt(ClothPartEditorData[PartId].VertexMapPath);
		if (PartVertexMap.Num() > NumVertices)
		{
			bIsVertexMapValid = false;
			UE_LOG(LogNearestNeighborModel, Error, TEXT("Part %d vertex map has %d vertices, larger than %d vertices in skeletal mesh, using %d vertices instead"), PartId, PartVertexMap.Num(), NumVertices, NumVertices);
			ReturnCode |= EUpdateResult::ERROR;
		}
		else
		{
			const int32 MaxIndex = FMath::Max(PartVertexMap);
			if (MaxIndex >= NumVertices)
			{
				bIsVertexMapValid = false;
				UE_LOG(LogNearestNeighborModel, Error, TEXT("Part %d vertex map max index is %d. There are only %d vertices in skeletal mesh, using %d vertices instead"), PartId, MaxIndex, NumVertices, NumVertices);
				ReturnCode |= EUpdateResult::ERROR;
			}
		}
		if (bIsVertexMapValid)
		{
			ClothPartData[PartId].VertexMap = AddConstant(PartVertexMap, StartIndex);
			return (EUpdateResult)ReturnCode;
		}
	}

	ClothPartData[PartId].VertexMap = Range(StartIndex, StartIndex + NumVertices);
	return (EUpdateResult)ReturnCode;
}

UE::NearestNeighborModel::EUpdateResult UNearestNeighborModel::UpdateClothPartData()
{
	using namespace UE::NearestNeighborModel;
	uint8 ReturnCode = EUpdateResult::SUCCESS;
	if(ClothPartEditorData.Num() == 0)
	{
		UE_LOG(LogNearestNeighborModel, Error, TEXT("There should be at least 1 cloth part"));
		return EUpdateResult::ERROR;
	}
	if (!GetSkeletalMesh() || !GetSkeletalMesh()->GetImportedModel() || GetSkeletalMesh()->GetImportedModel()->LODModels[0].ImportedMeshInfos.IsEmpty())
	{
		UE_LOG(LogNearestNeighborModel, Error, TEXT("SkeletalMesh is None or SkeletalMesh has no imported model."));
		return EUpdateResult::ERROR;
	}
	const TArray<FSkelMeshImportedMeshInfo>& SkelMeshInfos = GetSkeletalMesh()->GetImportedModel()->LODModels[0].ImportedMeshInfos;

	ClothPartData.SetNum(ClothPartEditorData.Num());
	for(int32 PartId = 0; PartId < GetNumParts(); PartId++)
	{
		ClothPartData[PartId].PCACoeffNum = ClothPartEditorData[PartId].PCACoeffNum;
		ReturnCode |= UpdateVertexMap(PartId, ClothPartEditorData[PartId].VertexMapPath, SkelMeshInfos[ClothPartEditorData[PartId].MeshIndex]);
		ClothPartData[PartId].NumVertices = ClothPartData[PartId].VertexMap.Num();

		if (!CheckPCAData(PartId))
		{
			ClothPartData[PartId].VertexMean.SetNumZeroed(ClothPartData[PartId].NumVertices * 3);
			ClothPartData[PartId].PCABasis.SetNumZeroed(ClothPartData[PartId].NumVertices * 3 * ClothPartData[PartId].PCACoeffNum);

			// Init default neighbor data.
			ClothPartData[PartId].NeighborCoeffs.SetNumZeroed(ClothPartData[PartId].PCACoeffNum);
			ClothPartData[PartId].NeighborOffsets.SetNumZeroed(ClothPartData[PartId].NumVertices * 3);
			ClothPartData[PartId].NumNeighbors = 1;
		}
	}
	UpdatePCACoeffNums();

	NearestNeighborData.SetNumZeroed(GetNumParts());
	UpdateNetworkInputDim();
	UpdateNetworkOutputDim();
	bClothPartDataValid = true;
	return (EUpdateResult)ReturnCode;
}

TObjectPtr<UAnimSequence> UNearestNeighborModel::GetNearestNeighborSkeletons(int32 PartId)
{ 
	if (PartId < NearestNeighborData.Num())
	{
		return NearestNeighborData[PartId].Skeletons; 
	}
	return nullptr;
}

const TObjectPtr<UAnimSequence> UNearestNeighborModel::GetNearestNeighborSkeletons(int32 PartId) const
{ 
	if (PartId < NearestNeighborData.Num())
	{
		return NearestNeighborData[PartId].Skeletons; 
	}
	return nullptr;
}

TObjectPtr<UGeometryCache> UNearestNeighborModel::GetNearestNeighborCache(int32 PartId)
{ 
	if (PartId < NearestNeighborData.Num())
	{
		return NearestNeighborData[PartId].Cache;
	}
	return nullptr;
}

const TObjectPtr<UGeometryCache> UNearestNeighborModel::GetNearestNeighborCache(int32 PartId) const
{ 
	if (PartId < NearestNeighborData.Num())
	{
		return NearestNeighborData[PartId].Cache;
	}
	return nullptr;
}

int32 UNearestNeighborModel::GetNumNeighborsFromGeometryCache(int32 PartId) const
{
	const UGeometryCache* Cache = GetNearestNeighborCache(PartId);
	if (Cache)
	{
		const int32 StartFrame = Cache->GetStartFrame();
		const int32 EndFrame = Cache->GetEndFrame();
		return EndFrame - StartFrame + 1;
	}
	else
	{
		return 0;
	}
}

int32 UNearestNeighborModel::GetNumNeighborsFromAnimSequence(int32 PartId) const
{
	const UAnimSequence* Anim = GetNearestNeighborSkeletons(PartId);
	if (Anim)
	{
		return Anim->GetDataModel()->GetNumberOfKeys();
	}
	else
	{
		return 0;
	}
}

void UNearestNeighborModel::UpdateNetworkSize()
{
	UNeuralNetwork* Network = GetNeuralNetwork();
	if (Network != nullptr)
	{
		const SIZE_T NumBytes = Network->GetResourceSizeBytes(EResourceSizeMode::EstimatedTotal);
		SavedNetworkSize = (double)NumBytes / 1024 / 1024;
	}
	else
	{
		SavedNetworkSize = 0.0f;
	}
}

void UNearestNeighborModel::UpdateMorphTargetSize()
{
	if (GetMorphTargetSet())
	{
		const FMorphTargetBuffersInfo* MorphBuffersInfo = static_cast<FMorphTargetBuffersInfo*>(&GetMorphTargetSet()->MorphBuffers);
		const double Size = MorphBuffersInfo->GetMorphDataSize();
		MorphDataSize = Size / 1024 / 1024;
	}
	else
	{
		MorphDataSize = 0.0f;
	}
}

FString UNearestNeighborModel::GetModelDir() const
{
	if (bUseFileCache)
	{
		return FileCacheDirectory;
	}
	else
	{
		return FPaths::ProjectIntermediateDir() + TEXT("NearestNeighborModel/");
	}
}
#endif

void UNearestNeighborModel::InitInputInfo()
{
	UNearestNeighborModelInputInfo* NearestNeighborModelInputInfo = static_cast<UNearestNeighborModelInputInfo*>(GetInputInfo());
	check(NearestNeighborModelInputInfo != nullptr);
#if WITH_EDITORONLY_DATA
	NearestNeighborModelInputInfo->InitRefBoneRotations(GetSkeletalMesh());
#endif
}

bool UNearestNeighborModel::CheckPCAData(int32 PartId) const
{
	const FClothPartData& Data = ClothPartData[PartId];
	return Data.VertexMap.Num() > 0 && Data.PCABasis.Num() == Data.VertexMap.Num() * 3 * Data.PCACoeffNum;
}
#undef LOCTEXT_NAMESPACE
