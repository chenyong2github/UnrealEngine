// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NearestNeighborModel.h"
#include "NearestNeighborModelInputInfo.h"
#include "NearestNeighborModelInstance.h"
#include "MLDeformerComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GeometryCache.h"
#include "Misc/FileHelper.h"
#include "Rendering/SkeletalMeshModel.h"
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
	VizSettings = ObjectInitializer.CreateEditorOnlyDefaultSubobject<UNearestNeighborModelVizSettings>(this, TEXT("VizSettings"));
#endif

	ExternalMorphSetID = 100;
}

UMLDeformerInputInfo* UNearestNeighborModel::CreateInputInfo()
{
	UNearestNeighborModelInputInfo* NearestNeighborModelInputInfo = NewObject<UNearestNeighborModelInputInfo>(this);
#if WITH_EDITORONLY_DATA
	NearestNeighborModelInputInfo->InitRefBoneRotations(SkeletalMesh);
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
	InitPreviousWeights();

#if WITH_EDITORONLY_DATA
	NearestNeighborIds.SetNumZeroed(GetNumParts());
	UpdateNetworkInputDim();
	UpdateNetworkOutputDim();
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

#if WITH_EDITORONLY_DATA
void UNearestNeighborModel::UpdatePCACoeffNums()
{
	uint32 PCACoeffStart = 0;
	for(int32 PartId = 0; PartId < GetNumParts(); PartId++)
	{
		ClothPartData[PartId].PCACoeffStart = PCACoeffStart;
		PCACoeffStart+=ClothPartData[PartId].PCACoeffNum;
	}
}

void UNearestNeighborModel::UpdateNetworkInputDim()
{
	InputDim = 3 * BoneIncludeList.Num();
}

void UNearestNeighborModel::UpdateNetworkOutputDim()
{
	OutputDim = 0;
	for (int32 i = 0; i < GetNumParts(); i++)
	{
		OutputDim += ClothPartData[i].PCACoeffNum; 
	}
}

void UNearestNeighborModel::UpdateClothPartData()
{
	if(ClothPartData.Num() == 0)
	{
		UE_LOG(LogNearestNeighborModel, Error, TEXT("There should be at least 1 cloth part"));
	}

	ClothPartData.SetNum(ClothPartEditorData.Num());
	for(int32 PartId = 0; PartId < GetNumParts(); PartId++)
	{
		if(FPaths::FileExists(ClothPartEditorData[PartId].VertexMapPath))
		{
			ClothPartData[PartId].PCACoeffNum = ClothPartEditorData[PartId].PCACoeffNum;
			ClothPartData[PartId].VertexMap = ReadTxt(ClothPartEditorData[PartId].VertexMapPath);
			ClothPartData[PartId].NumVertices = ClothPartData[PartId].VertexMap.Num();

			// Initialize PCA data with 0 to prevent NearestNeighborModelInstance::Tick from crashing
			if (!CheckPCAData(PartId))
			{
				ClothPartData[PartId].VertexMean.SetNumZeroed(ClothPartData[PartId].NumVertices * 3);
				ClothPartData[PartId].PCABasis.SetNumZeroed(ClothPartData[PartId].NumVertices * 3 * ClothPartData[PartId].PCACoeffNum);

				// NumNeighbors cannot be 0 because of the NearestNeighborModel.usf shader.
				// Init default neighbor data.
				ClothPartData[PartId].NeighborCoeffs.SetNumZeroed(ClothPartData[PartId].PCACoeffNum);
				ClothPartData[PartId].NeighborOffsets.SetNumZeroed(ClothPartData[PartId].NumVertices * 3);
				ClothPartData[PartId].NumNeighbors = 1;
			}
		}
		else
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("%s does not exist"), *ClothPartEditorData[PartId].VertexMapPath);
			return;
		}
	}
	UpdatePCACoeffNums();

	NearestNeighborIds.SetNumZeroed(GetNumParts());
	NearestNeighborData.SetNumZeroed(GetNumParts());
	UpdateNetworkInputDim();
	UpdateNetworkOutputDim();
	bClothPartDataValid = true;
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
	const TObjectPtr<UGeometryCache> Cache = GetNearestNeighborCache(PartId);
	if (!Cache.IsNull())
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
#endif

void UNearestNeighborModel::InitInputInfo()
{
	UNearestNeighborModelInputInfo* NearestNeighborModelInputInfo = static_cast<UNearestNeighborModelInputInfo*>(GetInputInfo());
	check(NearestNeighborModelInputInfo != nullptr);
#if WITH_EDITORONLY_DATA
	NearestNeighborModelInputInfo->InitRefBoneRotations(SkeletalMesh);
#endif
}

bool UNearestNeighborModel::CheckPCAData(int32 PartId) const
{
	const FClothPartData& Data = ClothPartData[PartId];
	return Data.VertexMap.Num() > 0 && Data.PCABasis.Num() == Data.VertexMap.Num() * 3 * Data.PCACoeffNum;
}


void UNearestNeighborModel::InitPreviousWeights()
{
	int32 NumWeights = 1;
	for (int32 PartId = 0; PartId < GetNumParts(); PartId++)
	{
		NumWeights += GetPCACoeffNum(PartId);
		NumWeights += GetNumNeighbors(PartId);
	}
	PreviousWeights.SetNumZeroed(NumWeights);
}
#undef LOCTEXT_NAMESPACE
