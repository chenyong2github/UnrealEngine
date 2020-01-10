// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/SkinWeightProfile.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UObjectIterator.h"
#include "ContentStreaming.h"

#include "Components/SkinnedMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"

#if WITH_EDITOR
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Animation/DebugSkelMeshComponent.h"
#endif

static void OnDefaultProfileCVarsChanged(IConsoleVariable* Variable)
{
	if (GSkinWeightProfilesLoadByDefaultMode >= 0)
	{
		const bool bClearBuffer = GSkinWeightProfilesLoadByDefaultMode == 2 || GSkinWeightProfilesLoadByDefaultMode == 0;
		const bool bSetBuffer = GSkinWeightProfilesLoadByDefaultMode == 3;

		if (bClearBuffer || bSetBuffer)
		{
			// Make sure no pending skeletal mesh LOD updates
			if (IStreamingManager::Get_Concurrent())
			{
				IStreamingManager::Get().GetRenderAssetStreamingManager().BlockTillAllRequestsFinished();
			}

			for (TObjectIterator<USkeletalMesh> It; It; ++It)
			{
				if (*It)
				{
					if (FSkeletalMeshRenderData* RenderData = It->GetResourceForRendering())
					{
						FSkinnedMeshComponentRecreateRenderStateContext RecreateState(*It);
						for (int32 LODIndex = 0; LODIndex < RenderData->LODRenderData.Num(); ++LODIndex)
						{
							FSkeletalMeshLODRenderData& LOD = RenderData->LODRenderData[LODIndex];
							if (bClearBuffer)
							{
								LOD.SkinWeightProfilesData.ClearDynamicDefaultSkinWeightProfile(*It, LODIndex);
							}
							else if (bSetBuffer)
							{
								LOD.SkinWeightProfilesData.ClearDynamicDefaultSkinWeightProfile(*It, LODIndex);
								LOD.SkinWeightProfilesData.SetDynamicDefaultSkinWeightProfile(*It, LODIndex);
							}
						}
					}
				}
			}
		}
	}
}

int32 GSkinWeightProfilesLoadByDefaultMode = -1;
static FAutoConsoleVariableRef CVarSkinWeightsLoadByDefaultMode(
	TEXT("a.SkinWeightProfile.LoadByDefaultMode"),
	GSkinWeightProfilesLoadByDefaultMode,
	TEXT("Enables/disables run-time optimization to override the original skin weights with a profile designated as the default to replace it. Can be used to optimize memory for specific platforms or devices")
	TEXT("-1 = disabled")
	TEXT("0 = static disabled")
	TEXT("1 = static enabled")
	TEXT("2 = dynamic disabled")
	TEXT("3 = dynamic enabled"),
	FConsoleVariableDelegate::CreateStatic(&OnDefaultProfileCVarsChanged),
	ECVF_Scalability
);

int32 GSkinWeightProfilesDefaultLODOverride = -1;
static FAutoConsoleVariableRef CVarSkinWeightProfilesDefaultLODOverride(
	TEXT("a.SkinWeightProfile.DefaultLODOverride"),
	GSkinWeightProfilesDefaultLODOverride,
	TEXT("Override LOD index from which on the default Skin Weight Profile should override the Skeletal Mesh's default Skin Weights"),
	FConsoleVariableDelegate::CreateStatic(&OnDefaultProfileCVarsChanged),
	ECVF_Scalability
);

int32 GSkinWeightProfilesAllowedFromLOD = -1;
static FAutoConsoleVariableRef CVarSkinWeightProfilesAllowedFromLOD(
	TEXT("a.SkinWeightProfile.AllowedFromLOD"),
	GSkinWeightProfilesAllowedFromLOD,
	TEXT("Override LOD index from which on the Skin Weight Profile can be applied"),
	FConsoleVariableDelegate::CreateStatic(&OnDefaultProfileCVarsChanged),
	ECVF_Scalability
);

FArchive& operator<<(FArchive& Ar, FRuntimeSkinWeightProfileData& OverrideData)
{
	Ar << OverrideData.OverridesInfo;
	Ar << OverrideData.Weights;	
	Ar << OverrideData.VertexIndexOverrideIndex;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FSkinWeightProfilesData& LODData)
{
	Ar << LODData.OverrideData;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FRuntimeSkinWeightProfileData::FSkinWeightOverrideInfo& OverrideInfo)
{
	Ar << OverrideInfo.InfluencesOffset;
	Ar << OverrideInfo.NumInfluences;

	return Ar;
}

#if WITH_EDITORONLY_DATA
FArchive& operator<<(FArchive& Ar, FImportedSkinWeightProfileData& ProfileData)
{
	Ar << ProfileData.SkinWeights;
	Ar << ProfileData.SourceModelInfluences;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FRawSkinWeight& OverrideEntry)
{
	for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
	{
		Ar << OverrideEntry.InfluenceBones[InfluenceIndex];
		Ar << OverrideEntry.InfluenceWeights[InfluenceIndex];
	}

	return Ar;
}
#endif // WITH_EDITORONLY_DATA

void FSkinWeightProfilesData::Init(FSkinWeightVertexBuffer* InBaseBuffer) 
{
	BaseBuffer = InBaseBuffer;
}

FSkinWeightProfilesData::~FSkinWeightProfilesData()
{
	bDefaultOverriden = false;
	bStaticOverriden = false;
	DefaultProfileName = NAME_None;
	ReleaseResources();
}

#if !WITH_EDITOR
void FSkinWeightProfilesData::OverrideBaseBufferSkinWeightData(USkeletalMesh* Mesh, int32 LODIndex)
{
	if (GSkinWeightProfilesLoadByDefaultMode == 1)
	{
		const TArray<FSkinWeightProfileInfo>& Profiles = Mesh->GetSkinWeightProfiles();
		// Try and find a default buffer and whether or not it is set for this LOD index 
		const int32 DefaultProfileIndex = Profiles.IndexOfByPredicate([LODIndex](FSkinWeightProfileInfo ProfileInfo)
		{
			// Setup to not apply any skin weight profiles at this LOD level
			if (LODIndex < GSkinWeightProfilesAllowedFromLOD)
			{
				return false;
			}

			// In case the default LOD index has been overridden check against that
			if (GSkinWeightProfilesDefaultLODOverride >= 0)
			{
				return (ProfileInfo.DefaultProfile.Default && LODIndex >= GSkinWeightProfilesDefaultLODOverride);
			}

			// Otherwise check if this profile is set as default and the current LOD index is applicable
			return (ProfileInfo.DefaultProfile.Default && LODIndex >= ProfileInfo.DefaultProfileFromLODIndex.Default);
		});

		bool bProfileSet = false;
		// If we found a profile try and find the override skin weights and apply if found
		if (DefaultProfileIndex != INDEX_NONE)
		{
			ApplyOverrideProfile(BaseBuffer, Profiles[DefaultProfileIndex].Name);
			bDefaultOverriden = true;
			bStaticOverriden = true;
			DefaultProfileName = Profiles[DefaultProfileIndex].Name;
		}
	}
}
#endif 

void FSkinWeightProfilesData::SetDynamicDefaultSkinWeightProfile(USkeletalMesh* Mesh, int32 LODIndex)
{
	if (bStaticOverriden)
	{
		UE_LOG(LogSkeletalMesh, Error, TEXT("[%s] Skeletal Mesh has overridden the default Skin Weights buffer during serialization, cannot set any other skin weight profile."), *Mesh->GetName());
		return;
	}

	if (GSkinWeightProfilesLoadByDefaultMode == 3)
	{
		const TArray<FSkinWeightProfileInfo>& Profiles = Mesh->GetSkinWeightProfiles();
		// Try and find a default buffer and whether or not it is set for this LOD index 
		const int32 DefaultProfileIndex = Profiles.IndexOfByPredicate([LODIndex](FSkinWeightProfileInfo ProfileInfo)
		{
			// Setup to not apply any skin weight profiles at this LOD level
			if (LODIndex < GSkinWeightProfilesAllowedFromLOD)
			{
				return false;
			}

			// In case the default LOD index has been overridden check against that
			if (GSkinWeightProfilesDefaultLODOverride >= 0)
			{
				return (ProfileInfo.DefaultProfile.Default && LODIndex >= GSkinWeightProfilesDefaultLODOverride);
			}

			// Otherwise check if this profile is set as default and the current LOD index is applicable
			return (ProfileInfo.DefaultProfile.Default && LODIndex >= ProfileInfo.DefaultProfileFromLODIndex.Default);
		});

		bool bProfileSet = false;
		// If we found a profile try and find the override skin weights and apply if found
		if (DefaultProfileIndex != INDEX_NONE)
		{
			const bool bNoDefaultProfile = DefaultOverrideSkinWeightBuffer == nullptr;
			const bool bDifferentDefaultProfile = bNoDefaultProfile && (!bDefaultOverriden || DefaultProfileName != Profiles[DefaultProfileIndex].Name);
			if (bNoDefaultProfile || bDifferentDefaultProfile)
			{
				DefaultOverrideSkinWeightBuffer = GetOverrideBuffer(Profiles[DefaultProfileIndex].Name);
				bDefaultOverriden = true;
				DefaultProfileName = Profiles[DefaultProfileIndex].Name;
			}
		}
	}
}

void FSkinWeightProfilesData::ClearDynamicDefaultSkinWeightProfile(USkeletalMesh* Mesh, int32 LODIndex)
{
	if (bStaticOverriden)
	{
		UE_LOG(LogSkeletalMesh, Error, TEXT("[%s] Skeletal Mesh has overridden the default Skin Weights buffer during serialization, cannot clear the skin weight profile."), *Mesh->GetName());		
		return;
	}

	if (bDefaultOverriden)
	{
		if (DefaultOverrideSkinWeightBuffer != nullptr)
		{
			ReleaseBuffer(DefaultProfileName);
			DefaultOverrideSkinWeightBuffer = nullptr;			
		}

		bDefaultOverriden = false;
		DefaultProfileName = NAME_None;		
	}
}

FSkinWeightVertexBuffer* FSkinWeightProfilesData::GetOverrideBuffer(const FName& ProfileName)
{
	SCOPED_NAMED_EVENT(FSkinWeightProfilesData_GetOverrideBuffer, FColor::Red);

	LLM_SCOPE(ELLMTag::SkeletalMesh);

	// In case we have overridden the default skin weight buffer we do not need to create an override buffer, if it was statically overridden we cannot load any other profile
	if (bDefaultOverriden && (ProfileName == DefaultProfileName || bStaticOverriden))
	{	
		if (bStaticOverriden && ProfileName != DefaultProfileName)
		{
			UE_LOG(LogSkeletalMesh, Error, TEXT("Skeletal Mesh has overridden the default Skin Weights buffer during serialization, cannot set any other skin weight profile."));
		}	

		return nullptr;
	}

	if (BaseBuffer)
	{
		if (ProfileNameToBuffer.Contains(ProfileName))
		{
			return ProfileNameToBuffer.FindChecked(ProfileName);
		}
		else
		{
			if (OverrideData.Contains(ProfileName))
			{
				FSkinWeightVertexBuffer* OverrideBuffer = new FSkinWeightVertexBuffer();
				ProfileNameToBuffer.Add(ProfileName, OverrideBuffer);
				OverrideBuffer->SetNeedsCPUAccess(true);

				ApplyOverrideProfile(OverrideBuffer, ProfileName);

				INC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, OverrideBuffer->GetVertexDataSize());
				BeginInitResource(OverrideBuffer);

				return OverrideBuffer;
			}
		}
	}

	return nullptr;
}

const FRuntimeSkinWeightProfileData* FSkinWeightProfilesData::GetOverrideData(const FName& ProfileName) const
{
	return OverrideData.Find(ProfileName);
}

FRuntimeSkinWeightProfileData& FSkinWeightProfilesData::AddOverrideData(const FName& ProfileName)
{
	return OverrideData.FindOrAdd(ProfileName);
}

void FSkinWeightProfilesData::ApplyOverrideProfile(FSkinWeightVertexBuffer* OverrideBuffer, const FName& ProfileName)
{
	const bool bExtraWeights = BaseBuffer->HasExtraBoneInfluences();
	OverrideBuffer->SetHasExtraBoneInfluences(bExtraWeights);

	const FRuntimeSkinWeightProfileData* ProfilePtr = OverrideData.Find(ProfileName);
	if (ProfilePtr)
	{
		if (bExtraWeights)
		{
			ProfilePtr->ApplyOverrides<true>(OverrideBuffer, BaseBuffer);
		}
		else
		{
			ProfilePtr->ApplyOverrides<false>(OverrideBuffer, BaseBuffer);
		}
	}
}

void FSkinWeightProfilesData::ReleaseBuffer(const FName& ProfileName)
{
	if (ProfileNameToBuffer.Contains(ProfileName) && (!bDefaultOverriden || ProfileName != DefaultProfileName))
	{
		FSkinWeightVertexBuffer* Buffer = nullptr;
		ProfileNameToBuffer.RemoveAndCopyValue(ProfileName, Buffer);

		if (Buffer)
		{
			DEC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, Buffer->GetVertexDataSize());
			ENQUEUE_RENDER_COMMAND(ReleaseSkinSkinWeightProfilesDataBufferCommand)(
				[Buffer](FRHICommandListImmediate& RHICmdList)
			{			
				Buffer->ReleaseResource();
				delete Buffer;		
			});
		}
	}
}

void FSkinWeightProfilesData::ReleaseResources()
{
	TArray<FSkinWeightVertexBuffer*> Buffers;
	ProfileNameToBuffer.GenerateValueArray(Buffers);
	ProfileNameToBuffer.Empty();

	// Never release a default buffer
	if (bDefaultOverriden)
	{
		Buffers.Remove(DefaultOverrideSkinWeightBuffer);
		ProfileNameToBuffer.Add(DefaultProfileName, DefaultOverrideSkinWeightBuffer);
	}

	Buffers.Remove(nullptr);

	if (Buffers.Num())
	{
		ENQUEUE_RENDER_COMMAND(ReleaseSkinSkinWeightProfilesDataBufferCommand)(
			[Buffers](FRHICommandListImmediate& RHICmdList)
		{
			for (FSkinWeightVertexBuffer* Buffer : Buffers)
			{	
				Buffer->ReleaseResource();
				delete Buffer;
			}
		});
	}
}

SIZE_T FSkinWeightProfilesData::GetResourcesSize() const
{
	SIZE_T SummedSize = 0;
	for (const TPair<FName, FSkinWeightVertexBuffer*>& Pair : ProfileNameToBuffer)
	{
		SummedSize += Pair.Value->GetVertexDataSize();
	}

	return SummedSize;
}

void FSkinWeightProfilesData::SerializeMetaData(FArchive& Ar)
{
	TArray<FName, TInlineAllocator<8>> ProfileNames;
	if (Ar.IsSaving())
	{
		OverrideData.GenerateKeyArray(ProfileNames);
		Ar << ProfileNames;
	}
	else
	{
		Ar << ProfileNames;
		OverrideData.Empty(ProfileNames.Num());
		for (int32 Idx = 0; Idx < ProfileNames.Num(); ++Idx)
		{
			OverrideData.Add(ProfileNames[Idx]);
		}
	}
}

void FSkinWeightProfilesData::ReleaseCPUResources()
{
	for (TMap<FName, FRuntimeSkinWeightProfileData>::TIterator It(OverrideData); It; ++It)
	{
		It->Value = FRuntimeSkinWeightProfileData();
	}
}

template <bool bRenderThread>
void FSkinWeightProfilesData::CreateRHIBuffers_Internal(TArray<TPair<FName, FVertexBufferRHIRef>>& OutBuffers)
{
	const int32 NumActiveProfiles = ProfileNameToBuffer.Num();
	check(BaseBuffer || !NumActiveProfiles);
	OutBuffers.Empty(NumActiveProfiles);
	for (TMap<FName, FSkinWeightVertexBuffer*>::TIterator It(ProfileNameToBuffer); It; ++It)
	{
		const FName& ProfileName = It->Key;
		FSkinWeightVertexBuffer* OverrideBuffer = It->Value;
		ApplyOverrideProfile(OverrideBuffer, ProfileName);
		if (bRenderThread)
		{
			OutBuffers.Emplace(ProfileName, OverrideBuffer->CreateRHIBuffer_RenderThread());
		}
		else
		{
			OutBuffers.Emplace(ProfileName, OverrideBuffer->CreateRHIBuffer_Async());
		}
	}
}

void FSkinWeightProfilesData::CreateRHIBuffers_RenderThread(TArray<TPair<FName, FVertexBufferRHIRef>>& OutBuffers)
{
	CreateRHIBuffers_Internal<true>(OutBuffers);
}

void FSkinWeightProfilesData::CreateRHIBuffers_Async(TArray<TPair<FName, FVertexBufferRHIRef>>& OutBuffers)
{
	CreateRHIBuffers_Internal<false>(OutBuffers);
}

