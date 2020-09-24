// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomBindingAsset.h"
#include "EngineUtils.h"
#include "GroomAsset.h"

/////////////////////////////////////////////////////////////////////////////////////////

FArchive& operator<<(FArchive& Ar, UGroomBindingAsset::FHairGroupData& GroupData)
{
	GroupData.SimRootData.Serialize(Ar);
	GroupData.RenRootData.Serialize(Ar);
	return Ar;
}

void UGroomBindingAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar << HairGroupDatas;
}

void UGroomBindingAsset::InitResource()
{
	for (FHairGroupData& Data : HairGroupDatas)
	{
		FHairGroupResource& Resource = HairGroupResources.AddDefaulted_GetRef();
		Resource.SimRootResources = new FHairStrandsRestRootResource(Data.SimRootData);
		Resource.RenRootResources = new FHairStrandsRestRootResource(Data.RenRootData);

		BeginInitResource(Resource.SimRootResources);
		BeginInitResource(Resource.RenRootResources);
	}
}

void UGroomBindingAsset::UpdateResource()
{
	for (FHairGroupResource& Resource : HairGroupResources)
	{
		BeginUpdateResourceRHI(Resource.SimRootResources);
		BeginUpdateResourceRHI(Resource.RenRootResources);
	}
}

void UGroomBindingAsset::ReleaseResource()
{
	// Delay destruction to insure that the rendering thread is done with all resources usage
	if (HairGroupResources.Num() > 0)
	{
		for (FHairGroupResource& Resource : HairGroupResources)
		{
			FHairStrandsRestRootResource* InSimRootResources = Resource.SimRootResources;
			FHairStrandsRestRootResource* InRenRootResources = Resource.RenRootResources;
			ENQUEUE_RENDER_COMMAND(ReleaseHairStrandsResourceCommand)(
				[InSimRootResources, InRenRootResources](FRHICommandList& RHICmdList)
			{
				InSimRootResources->ReleaseResource();
				InRenRootResources->ReleaseResource();
				delete InSimRootResources;
				delete InRenRootResources;
			});
			Resource.SimRootResources = nullptr;
			Resource.RenRootResources = nullptr;
		}
		HairGroupResources.Empty();
	}

	// Process resources to be deleted (should happen only in editor)
	FHairGroupResource ResourceToDelete;
	while (HairGroupResourcesToDelete.Dequeue(ResourceToDelete))
	{
		FHairStrandsRestRootResource* InSimRootResources = ResourceToDelete.SimRootResources;
		FHairStrandsRestRootResource* InRenRootResources = ResourceToDelete.RenRootResources;
		ENQUEUE_RENDER_COMMAND(ReleaseHairStrandsResourceCommand)(
			[InSimRootResources, InRenRootResources](FRHICommandList& RHICmdList)
		{
			InSimRootResources->ReleaseResource();
			InRenRootResources->ReleaseResource();
			delete InSimRootResources;
			delete InRenRootResources;
		});
	}
}

void UGroomBindingAsset::Reset()
{
	ReleaseResource();
	for (FHairGroupData& Data : HairGroupDatas)
	{
		Data.SimRootData.Reset();
		Data.RenRootData.Reset();
	}
}

void UGroomBindingAsset::PostLoad()
{
	Super::PostLoad();

	if (Groom)
	{
		// Make sure that the asset initialized its resources first since the component needs them to initialize its own resources
		Groom->ConditionalPostLoad();

		// Sanity check. This function will report back warnings/issues into the log for user.
		UGroomBindingAsset::IsCompatible(Groom, this, true);
	}

	if (!IsTemplate())
	{
		InitResource();
	}
}

void UGroomBindingAsset::PreSave(const class ITargetPlatform* TargetPlatform)
{
#if WITH_EDITOR
	while (QueryStatus == EQueryStatus::Submitted)
	{
		FPlatformProcess::Sleep(1);
	}
#endif
	Super::PreSave(TargetPlatform);
#if WITH_EDITOR
	OnGroomBindingAssetChanged.Broadcast();
#endif
}

void UGroomBindingAsset::PostSaveRoot(bool bCleanupIsRequired)
{
	Super::PostSaveRoot(bCleanupIsRequired);
#if WITH_EDITOR
	OnGroomBindingAssetChanged.Broadcast();
#endif
}

void UGroomBindingAsset::BeginDestroy()
{
	ReleaseResource();
	Super::BeginDestroy();
}

bool UGroomBindingAsset::IsCompatible(const USkeletalMesh* InSkeletalMesh, const UGroomBindingAsset* InBinding, bool bIssueWarning)
{
	if (InBinding && InSkeletalMesh)
	{
		if (!InBinding->TargetSkeletalMesh)
		{
			if (bIssueWarning)
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The binding asset (%s) does not have a target skeletal mesh. Falling back onto non-binding version."), *InBinding->GetName());
			}
			return false;
		}
		
		// TODO: need something better to assess that skeletal meshes match. In the mean time, string comparison. 
		// Since they can be several instances of a skeletalMesh asset (??), a numerical suffix is sometime added to the name (e.g., SkeletalName_0).
		// This is why we are using substring comparison.
		//if (InSkeletalMesh->GetPrimaryAssetId() != InBinding->TargetSkeletalMesh->GetPrimaryAssetId())
		if (!InSkeletalMesh->GetName().Contains(InBinding->TargetSkeletalMesh->GetName()))
		{
			if (bIssueWarning)
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The Groom binding (%s) does not reference the same skeletal asset (BindingAsset's skeletal:%s vs. Attached skeletal:%s). The binding asset will not be used."),
					*InBinding->GetName(),
					*InBinding->TargetSkeletalMesh->GetName(),
					*InSkeletalMesh->GetName());
			}
			return false;
		}

		for (const FHairGroupResource& Resource : InBinding->HairGroupResources)
		{
			if (InSkeletalMesh->GetLODNum() != Resource.RenRootResources->RootData.MeshProjectionLODs.Num() ||
				InSkeletalMesh->GetLODNum() != Resource.SimRootResources->RootData.MeshProjectionLODs.Num())
			{
				if (bIssueWarning)
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The Groom binding (%s) does not have the same have LOD count (LOD render:%d, LOD sim:%d) than the skeletal mesh (%s, LOD:%d). The binding asset will not be used."),
						*InBinding->GetName(),
						Resource.RenRootResources->RootData.MeshProjectionLODs.Num(),
						Resource.SimRootResources->RootData.MeshProjectionLODs.Num(),
						*InSkeletalMesh->GetName(),
						InSkeletalMesh->GetLODNum());
				}
				return false;
			}
		}
	}

	return true;
}

bool UGroomBindingAsset::IsCompatible(const UGroomAsset* InGroom, const UGroomBindingAsset* InBinding, bool bIssueWarning)
{
	if (InBinding && InGroom)
	{
		if (!InBinding->Groom)
		{
			if (bIssueWarning)
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The binding asset (%s) does not reference a groom. Falling back onto non-binding version."), *InBinding->GetName());
			}
			return false;
		}

		if (InGroom->GetPrimaryAssetId() != InBinding->Groom->GetPrimaryAssetId())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The Groom binding (%) does not reference the same groom asset (BindingAsset's groom:%s vs. Groom:%s). The binding asset will not be used."), 
				*InBinding->GetName(),
				*InBinding->Groom->GetName(),
				*InGroom->GetName());
			return false;
		}

		const uint32 GroupCount = InGroom->GetNumHairGroups();
		if (GroupCount != InBinding->GroupInfos.Num())
		{
			if (bIssueWarning)
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The GroomBinding asset (%s) does not contains the same number of groups (%d vs. %d) than the groom (%s). The binding asset will not be used."),
					*InBinding->GetName(),
					GroupCount,
					InBinding->GroupInfos.Num(),
					*InGroom->GetName());
			}
			return false;
		}

		for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
		{
			{
				const uint32 GroomCount = InGroom->HairGroupsData[GroupIt].Guides.Data.GetNumCurves();
				const uint32 BindingCount = InBinding->GroupInfos[GroupIt].SimRootCount;

				if (GroomCount != BindingCount)
				{
					if (bIssueWarning)
					{
						UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The GroomBinding asset (%s) does not contains the same guides in group %d (%d vs. %d) than the groom (%s). The binding asset will not be used."),
							*InBinding->GetName(),
							GroupIt,
							GroomCount,
							BindingCount,
							*InGroom->GetName());
					}
					return false;
				}
			}

			{
				const uint32 GroomCount = InGroom->HairGroupsData[GroupIt].Strands.Data.GetNumCurves();
				const uint32 BindingCount = InBinding->GroupInfos[GroupIt].RenRootCount;

				if (GroomCount != BindingCount)
				{
					if (bIssueWarning)
					{
						UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The GroomBinding asset (%s) does not contains the same curves in group %d (%d vs. %d) than the groom (%s). The binding asset will not be used."),
							*InBinding->GetName(),
							GroupIt,
							GroomCount,
							BindingCount,
							*InGroom->GetName());
					}
					return false;
				}
			}
		}
	}
	return true;
}

bool UGroomBindingAsset::IsBindingAssetValid(const UGroomBindingAsset* InBinding, bool bIsBindingReloading, bool bIssueWarning)
{
	if (InBinding)
	{
		if (!InBinding->Groom)
		{
			if (bIssueWarning)
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The binding asset (%s) does not reference a groom. Falling back onto non-binding version."), *InBinding->GetName());
			}
			return false;
		}

		if (const UPackage* Package = InBinding->GetOutermost())
		{
			if (Package->IsDirty() && !bIsBindingReloading)
			{
				if (bIssueWarning)
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The binding asset (%s) is not saved and will be considered as invalid. Falling back onto non-binding version."), *InBinding->GetName());
				}
				return false;
			}
		}

		if (InBinding->GroupInfos.Num() == 0)
		{
			if (bIssueWarning)
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The GroomBinding asset (Groom:%s) does not contain any groups. It is invalid and can't be assigned. The binding asset will not be used."), *InBinding->Groom->GetName());
			}
			return false;
		}

		for (const FGoomBindingGroupInfo& Info : InBinding->GroupInfos)
		{
			if (Info.SimRootCount == 0)
			{
				if (bIssueWarning)
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The GroomBinding asset (Groom:%s) has group with 0 guides. It is invalid and can't be assigned. The binding asset will not be used."), *InBinding->Groom->GetName());
				}
				return false;
			}

			if (Info.RenRootCount == 0)
			{
				if (bIssueWarning)
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The GroomBinding asset (Groom:%s) has group with 0 curves. It is invalid and can't be assigned. The binding asset will not be used."), *InBinding->Groom->GetName());
				}
				return false;
			}
		}
	}
	return true;
}

#if WITH_EDITOR
void UGroomBindingAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateResource();
	OnGroomBindingAssetChanged.Broadcast();
}
#endif // WITH_EDITOR
