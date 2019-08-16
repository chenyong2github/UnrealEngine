// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Rendering/SkinWeightVertexBuffer.h"
#include "PerPlatformProperties.h"
#include "UObject/NameTypes.h"
#include "Misc/CoreStats.h"
#include "RenderingThread.h"
#include "HAL/UnrealMemory.h"

#include "SkinWeightProfile.generated.h"

class USkeletalMesh;
class UDebugSkelMeshComponent;

namespace SkeletalMeshImportData
{
	struct FVertInfluence;
}

extern ENGINE_API int32 GSkinWeightProfilesLoadByDefaultMode;
extern ENGINE_API int32 GSkinWeightProfilesDefaultLODOverride;
extern ENGINE_API int32 GSkinWeightProfilesAllowedFromLOD;

/** Structure storing user facing properties, and is used to identify profiles at the SkeletalMesh level*/
USTRUCT()
struct FSkinWeightProfileInfo
{
	GENERATED_BODY()

	/** Name of the Skin Weight Profile*/
	UPROPERTY(EditAnywhere, Category = SkinWeights)
	FName Name;
	
	/** Whether or not this Profile should be considered the Default loaded for specific LODs rather than the original Skin Weights of the Skeletal Mesh */
	UPROPERTY(EditAnywhere, Category = SkinWeights)
	FPerPlatformBool DefaultProfile;

	/** When DefaultProfile is set any LOD below this LOD Index will override the Skin Weights of the Skeletal Mesh with the Skin Weights from this Profile */
	UPROPERTY(EditAnywhere, Category = SkinWeights, meta=(EditCondition="DefaultProfile", ClampMin=0, DisplayName = "Default Profile from LOD Index"))
	FPerPlatformInt DefaultProfileFromLODIndex;
	
#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = SkinWeights)
	TMap<int32, FString> PerLODSourceFiles;
#endif
};

#if WITH_EDITORONLY_DATA

/** Editor only skin weight representation */
struct FRawSkinWeight
{
	// MAX_TOTAL_INFLUENCES for now
	uint8 InfluenceBones[MAX_TOTAL_INFLUENCES];
	uint8 InfluenceWeights[MAX_TOTAL_INFLUENCES];

	friend FArchive& operator<<(FArchive& Ar, FRawSkinWeight& OverrideEntry);
};

/** Editor only representation of a Skin Weight profile, stored as part of FSkeletalMeshLODModel, used as a base for generating the runtime version (FSkeletalRenderDataSkinWeightProfilesData) */
struct FImportedSkinWeightProfileData
{
	TArray<FRawSkinWeight> SkinWeights;

	//This is the result of the imported data before the chunking
	//We use this data every time we need to re-chunk the skeletal mesh
	TArray<SkeletalMeshImportData::FVertInfluence> SourceModelInfluences;

	friend FArchive& operator<<(FArchive& Ar, FImportedSkinWeightProfileData& ProfileData);
};

#endif // WITH_EDITORONLY_DATA

/** Runtime structure containing the set of override weights and the associated vertex indices */
struct FRuntimeSkinWeightProfileData
{
	/** Structure containing per Skin Weight offset and length */
	struct FSkinWeightOverrideInfo
	{
		/** Offset into FRuntimeSkinWeightOverrideData.Weights */
		uint32 InfluencesOffset;
		/** Number of influences to be read from FRuntimeSkinWeightOverrideData.Weights */
		uint8 NumInfluences;

		friend FArchive& operator<<(FArchive& Ar, FSkinWeightOverrideInfo& OverrideInfo);
	};

	template<bool bExtraBoneInfluences>
	void ApplyOverrides(FSkinWeightVertexBuffer* OverrideBuffer, const FSkinWeightVertexBuffer* BaseBuffer) const
	{
		const TSkinWeightInfo<bExtraBoneInfluences>* SkinWeightInfoPtr = BaseBuffer->GetSkinWeightPtr<bExtraBoneInfluences>(0);
		
		if (SkinWeightInfoPtr)
		{
			TArray<TSkinWeightInfo<bExtraBoneInfluences>> OverrideArray;
			const int32 ExpectedNumVerts = BaseBuffer->GetNumVertices();
			OverrideArray.SetNumUninitialized(ExpectedNumVerts);
			FMemory::Memcpy(OverrideArray.GetData(), SkinWeightInfoPtr, sizeof(TSkinWeightInfo<bExtraBoneInfluences>) * ExpectedNumVerts);

			// Apply overrides
			{
				for (auto VertexIndexOverridePair : VertexIndexOverrideIndex)
				{
					const uint32 VertexIndex = VertexIndexOverridePair.Key;
					TSkinWeightInfo<bExtraBoneInfluences>& Entry = OverrideArray[VertexIndex];

					const uint32 OverrideIndex = VertexIndexOverridePair.Value;
					const FSkinWeightOverrideInfo& OverrideInfo = OverridesInfo[OverrideIndex];

					FMemory::Memzero(Entry.InfluenceBones);
					FMemory::Memzero(Entry.InfluenceWeights);

					for (int32 Index = 0; Index < OverrideInfo.NumInfluences; ++Index)
					{
						const uint16 WeightData = Weights[OverrideInfo.InfluencesOffset + Index];

						Entry.InfluenceBones[Index] = (WeightData) >> 8;
						Entry.InfluenceWeights[Index] = (WeightData & 0xFF);
					}
				}
			}

			(*OverrideBuffer) = OverrideArray;
		}
		else
		{
			OverrideBuffer->CopyMetaData(*BaseBuffer);
		}
	}

	/** Per skin weight offset into Weights array and number of weights stored */
	TArray<FSkinWeightOverrideInfo> OverridesInfo;
	/** Bulk data containing all Weights, stored as bone id in upper and weight in lower (8) bits */
	TArray<uint16> Weights;	
	/** Map between Vertex Indices and entries of OverridesInfo */
	TMap<uint32, uint32> VertexIndexOverrideIndex;
	
	friend FArchive& operator<<(FArchive& Ar, FRuntimeSkinWeightProfileData& OverrideData);
};

/** Runtime structure for keeping track of skin weight profile(s) and the associated buffer */
struct ENGINE_API FSkinWeightProfilesData
{
	FSkinWeightProfilesData() : BaseBuffer(nullptr), DefaultOverrideSkinWeightBuffer(nullptr), bDefaultOverriden(false), bStaticOverriden(false), DefaultProfileName(NAME_None) {}
	void Init(FSkinWeightVertexBuffer* InBaseBuffer);

	~FSkinWeightProfilesData();

#if !WITH_EDITOR
	// Mark this as non-editor only to prevent mishaps from users
	void OverrideBaseBufferSkinWeightData(USkeletalMesh* Mesh, int32 LODIndex);
#endif 
	void SetDynamicDefaultSkinWeightProfile(USkeletalMesh* Mesh, int32 LODIndex);	
	void ClearDynamicDefaultSkinWeightProfile(USkeletalMesh* Mesh, int32 LODIndex);
	FSkinWeightVertexBuffer* GetDefaultOverrideBuffer() const { return DefaultOverrideSkinWeightBuffer; }

	void ApplyOverrideProfile(FSkinWeightVertexBuffer* OverrideBuffer, const FName& ProfileName);
	
	FSkinWeightVertexBuffer* GetOverrideBuffer(const FName& ProfileName);
	const FRuntimeSkinWeightProfileData* GetOverrideData(const FName& ProfileName) const;
	FRuntimeSkinWeightProfileData& AddOverrideData(const FName& ProfileName);
	
	void ReleaseBuffer(const FName& ProfileName);
	void ReleaseResources();

	SIZE_T GetResourcesSize() const;

	friend FArchive& operator<<(FArchive& Ar, FSkinWeightProfilesData& OverrideData);

	void SerializeMetaData(FArchive& Ar);

	void ReleaseCPUResources();

	void CreateRHIBuffers_RenderThread(TArray<TPair<FName, FVertexBufferRHIRef>>& OutBuffers);
	void CreateRHIBuffers_Async(TArray<TPair<FName, FVertexBufferRHIRef>>& OutBuffers);

	template <uint32 MaxNumUpdates>
	void InitRHIForStreaming(const TArray<TPair<FName, FVertexBufferRHIRef>>& IntermediateBuffers, TRHIResourceUpdateBatcher<MaxNumUpdates>& Batcher)
	{
		for (int32 Idx = 0; Idx < IntermediateBuffers.Num(); ++Idx)
		{
			const FName& ProfileName = IntermediateBuffers[Idx].Key;
			FRHIVertexBuffer* IntermediateBuffer = IntermediateBuffers[Idx].Value;
			ProfileNameToBuffer.FindChecked(ProfileName)->InitRHIForStreaming(IntermediateBuffer, Batcher);
		}
	}
	
	template <uint32 MaxNumUpdates>
	void ReleaseRHIForStreaming(TRHIResourceUpdateBatcher<MaxNumUpdates>& Batcher)
	{
		for (TMap<FName, FSkinWeightVertexBuffer*>::TIterator It(ProfileNameToBuffer); It; ++It)
		{
			It->Value->ReleaseRHIForStreaming(Batcher);
		}
	}

protected:
	template <bool bRenderThread>
	void CreateRHIBuffers_Internal(TArray<TPair<FName, FVertexBufferRHIRef>>& OutBuffers);

	FSkinWeightVertexBuffer* BaseBuffer;
	FSkinWeightVertexBuffer* DefaultOverrideSkinWeightBuffer;

	TMap<FName, FSkinWeightVertexBuffer*> ProfileNameToBuffer;
	TMap<FName, FRuntimeSkinWeightProfileData> OverrideData;

	bool bDefaultOverriden;
	bool bStaticOverriden;
	FName DefaultProfileName;
};

