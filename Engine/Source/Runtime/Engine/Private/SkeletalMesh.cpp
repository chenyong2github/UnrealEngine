// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkeletalMesh.cpp: Unreal skeletal mesh and animation implementation.
=============================================================================*/

#include "Engine/SkeletalMesh.h"
#include "Serialization/CustomVersion.h"
#include "UObject/FrameworkObjectVersion.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "EngineStats.h"
#include "EngineGlobals.h"
#include "RawIndexBuffer.h"
#include "Engine/TextureStreamingTypes.h"
#include "Engine/Brush.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Components/SkinnedMeshComponent.h"
#include "Animation/SmartName.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "ComponentReregisterContext.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/CoreObjectVersion.h"
#include "EngineUtils.h"
#include "EditorSupportDelegates.h"
#include "GPUSkinVertexFactory.h"
#include "TessellationRendering.h"
#include "SkeletalRenderPublic.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "SceneManagement.h"
#include "PhysicsPublic.h"
#include "Animation/MorphTarget.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/AssetUserData.h"
#include "Engine/Engine.h"
#include "Animation/NodeMappingContainer.h"
#include "GPUSkinCache.h"
#include "Misc/ConfigCacheIni.h"
#include "SkeletalMeshTypes.h"
#include "Rendering/SkeletalMeshVertexBuffer.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/PropertyPortFlags.h"
#include "Templates/UniquePtr.h"
#include "AnimationRuntime.h"
#include "Animation/AnimSequence.h"
#include "UObject/NiagaraObjectVersion.h"
#include "Animation/SkinWeightProfile.h"
#include "Streaming/SkeletalMeshUpdate.h"
#include "UObject/CoreRedirects.h"
#include "HAL/FileManager.h"

#if WITH_EDITOR
#include "Async/ParallelFor.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "MeshUtilities.h"
#include "Engine/SkeletalMeshEditorData.h"

#if WITH_APEX_CLOTHING
#include "ApexClothingUtils.h"
#endif

#include "IMeshReductionManagerModule.h"
#include "SkeletalMeshReductionSettings.h"
#include "Engine/RendererSettings.h"

#endif // #if WITH_EDITOR

#include "Misc/CoreMisc.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"

#if WITH_APEX
#include "PhysXIncludes.h"
#endif// #if WITH_APEX

#include "EditorFramework/AssetImportData.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Components/BrushComponent.h"
#include "Streaming/UVChannelDensity.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"

#include "ClothingAssetBase.h"

#if WITH_EDITOR
#include "ClothingAssetFactoryInterface.h"
#include "ClothingSystemEditorInterfaceModule.h"
#endif
#include "SkeletalDebugRendering.h"
#include "Misc/RuntimeErrors.h"
#include "PlatformInfo.h"

#if RHI_RAYTRACING
#include "RayTracingInstance.h"
#endif
#include "Animation/SkinWeightProfileManager.h"

#define LOCTEXT_NAMESPACE "SkeltalMesh"

DEFINE_LOG_CATEGORY(LogSkeletalMesh);
DECLARE_CYCLE_STAT(TEXT("GetShadowShapes"), STAT_GetShadowShapes, STATGROUP_Anim);

TAutoConsoleVariable<int32> CVarDebugDrawSimpleBones(TEXT("a.DebugDrawSimpleBones"), 0, TEXT("When drawing bones (using Show Bones), draw bones as simple lines."));
TAutoConsoleVariable<int32> CVarDebugDrawBoneAxes(TEXT("a.DebugDrawBoneAxes"), 0, TEXT("When drawing bones (using Show Bones), draw bone axes."));

const FGuid FSkeletalMeshCustomVersion::GUID(0xD78A4A00, 0xE8584697, 0xBAA819B5, 0x487D46B4);
FCustomVersionRegistration GRegisterSkeletalMeshCustomVersion(FSkeletalMeshCustomVersion::GUID, FSkeletalMeshCustomVersion::LatestVersion, TEXT("SkeletalMeshVer"));

static TAutoConsoleVariable<int32> CVarRayTracingSkeletalMeshes(
	TEXT("r.RayTracing.Geometry.SkeletalMeshes"),
	1,
	TEXT("Include skeletal meshes in ray tracing effects (default = 1 (skeletal meshes enabled in ray tracing))"));

#if WITH_APEX_CLOTHING
/*-----------------------------------------------------------------------------
	utility functions for apex clothing 
-----------------------------------------------------------------------------*/

static apex::ClothingAsset* LoadApexClothingAssetFromBlob(const TArray<uint8>& Buffer)
{
	// Wrap this blob with the APEX read stream class
	physx::PxFileBuf* Stream = GApexSDK->createMemoryReadStream( Buffer.GetData(), Buffer.Num() );
	// Create an NvParameterized serializer
	NvParameterized::Serializer* Serializer = GApexSDK->createSerializer(NvParameterized::Serializer::NST_BINARY);
	// Deserialize into a DeserializedData buffer
	NvParameterized::Serializer::DeserializedData DeserializedData;
	Serializer->deserialize( *Stream, DeserializedData );
	apex::Asset* ApexAsset = NULL;
	if( DeserializedData.size() > 0 )
	{
		// The DeserializedData has something in it, so create an APEX asset from it
		ApexAsset = GApexSDK->createAsset( DeserializedData[0], NULL);
		// Make sure it's a Clothing asset
		if (ApexAsset 
			&& ApexAsset->getObjTypeID() != GApexModuleClothing->getModuleID()
			)
		{
			GPhysCommandHandler->DeferredRelease(ApexAsset);
			ApexAsset = NULL;
		}
	}

	apex::ClothingAsset* ApexClothingAsset = static_cast<apex::ClothingAsset*>(ApexAsset);
	// Release our temporary objects
	Serializer->release();
	GApexSDK->releaseMemoryReadStream( *Stream );

	return ApexClothingAsset;
}

static bool SaveApexClothingAssetToBlob(const apex::ClothingAsset *InAsset, TArray<uint8>& OutBuffer)
{
	bool bResult = false;
	uint32 Size = 0;
	// Get the NvParameterized data for our Clothing asset
	if( InAsset != NULL )
	{
		// Create an APEX write stream
		physx::PxFileBuf* Stream = GApexSDK->createMemoryWriteStream();
		// Create an NvParameterized serializer
		NvParameterized::Serializer* Serializer = GApexSDK->createSerializer(NvParameterized::Serializer::NST_BINARY);

		const NvParameterized::Interface* AssetParameterized = InAsset->getAssetNvParameterized();
		if( AssetParameterized != NULL )
		{
			// Serialize the data into the stream
			Serializer->serialize( *Stream, &AssetParameterized, 1 );
			// Read the stream data into our buffer for UE serialzation
			Size = Stream->getFileLength();
			OutBuffer.AddUninitialized( Size );
			Stream->read( OutBuffer.GetData(), Size );
			bResult = true;
		}

		// Release our temporary objects
		Serializer->release();
		Stream->release();
	}

	return bResult;
}

#endif//#if WITH_APEX_CLOTHING


/*-----------------------------------------------------------------------------
FGPUSkinVertexBase
-----------------------------------------------------------------------------*/

/**
* Serializer
*
* @param Ar - archive to serialize with
*/
void TGPUSkinVertexBase::Serialize(FArchive& Ar)
{
	Ar << TangentX;
	Ar << TangentZ;
}






const FGuid FRecomputeTangentCustomVersion::GUID(0x5579F886, 0x933A4C1F, 0x83BA087B, 0x6361B92F);
// Register the custom version with core
FCustomVersionRegistration GRegisterRecomputeTangentCustomVersion(FRecomputeTangentCustomVersion::GUID, FRecomputeTangentCustomVersion::LatestVersion, TEXT("RecomputeTangentCustomVer"));

const FGuid FOverlappingVerticesCustomVersion::GUID(0x612FBE52, 0xDA53400B, 0x910D4F91, 0x9FB1857C);
// Register the custom version with core
FCustomVersionRegistration GRegisterOverlappingVerticesCustomVersion(FOverlappingVerticesCustomVersion::GUID, FOverlappingVerticesCustomVersion::LatestVersion, TEXT("OverlappingVerticeDetectionVer"));


FArchive& operator<<(FArchive& Ar, FMeshToMeshVertData& V)
{
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

	Ar << V.PositionBaryCoordsAndDist
		<< V.NormalBaryCoordsAndDist
		<< V.TangentBaryCoordsAndDist
		<< V.SourceMeshVertIndices[0]
		<< V.SourceMeshVertIndices[1]
		<< V.SourceMeshVertIndices[2]
		<< V.SourceMeshVertIndices[3];

	if (Ar.IsLoading() && 
		Ar.CustomVer(FReleaseObjectVersion::GUID) < FReleaseObjectVersion::WeightFMeshToMeshVertData)
	{
		// Old version had "uint32 Padding[2]"
		uint32 Discard;
		Ar << Discard << V.Padding;
	}
	else
	{
		// New version has "float Weight and "uint32 Padding"
		Ar << V.Weight << V.Padding;
	}

	return Ar;
}


/*-----------------------------------------------------------------------------
FreeSkeletalMeshBuffersSinkCallback
-----------------------------------------------------------------------------*/

void FreeSkeletalMeshBuffersSinkCallback()
{
	// If r.FreeSkeletalMeshBuffers==1 then CPU buffer copies are to be released.
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.FreeSkeletalMeshBuffers"));
	bool bFreeSkeletalMeshBuffers = CVar->GetValueOnGameThread() == 1;
	if(bFreeSkeletalMeshBuffers)
	{
		FlushRenderingCommands();
		for (TObjectIterator<USkeletalMesh> It;It;++It)
		{
			if (!It->HasPendingInitOrStreaming() && !It->GetResourceForRendering()->RequiresCPUSkinning(GMaxRHIFeatureLevel))
			{
				It->ReleaseCPUResources();
			}
		}
	}
}

/*-----------------------------------------------------------------------------
	FClothingAssetData
-----------------------------------------------------------------------------*/

FArchive& operator<<(FArchive& Ar, FClothingAssetData_Legacy& A)
{
	// Serialization to load and save ApexClothingAsset
	if( Ar.IsLoading() )
	{
		uint32 AssetSize;
		Ar << AssetSize;

		if( AssetSize > 0 )
		{
			// Load the binary blob data
			TArray<uint8> Buffer;
			Buffer.AddUninitialized( AssetSize );
			Ar.Serialize( Buffer.GetData(), AssetSize );
#if WITH_APEX_CLOTHING
			A.ApexClothingAsset = LoadApexClothingAssetFromBlob(Buffer);
#endif //#if WITH_APEX_CLOTHING
		}
	}
	else
	if( Ar.IsSaving() )
	{
#if WITH_APEX_CLOTHING
		if (A.ApexClothingAsset)
		{
			TArray<uint8> Buffer;
			SaveApexClothingAssetToBlob(A.ApexClothingAsset, Buffer);
			uint32 AssetSize = Buffer.Num();
			Ar << AssetSize;
			Ar.Serialize(Buffer.GetData(), AssetSize);
		}
		else
#endif// #if WITH_APEX_CLOTHING
		{
			uint32 AssetSize = 0;
			Ar << AssetSize;
		}
	}

	return Ar;
}


FSkeletalMeshClothBuildParams::FSkeletalMeshClothBuildParams()
	: TargetAsset(nullptr)
	, TargetLod(INDEX_NONE)
	, bRemapParameters(false)
	, AssetName("Clothing")
	, LodIndex(0)
	, SourceSection(0)
	, bRemoveFromMesh(false)
	, PhysicsAsset(nullptr)
{

}

#if WITH_EDITOR
FScopedSkeletalMeshPostEditChange::FScopedSkeletalMeshPostEditChange(USkeletalMesh* InSkeletalMesh, bool InbCallPostEditChange /*= true*/, bool InbReregisterComponents /*= true*/)
{
	SkeletalMesh = nullptr;
	bReregisterComponents = InbReregisterComponents;
	bCallPostEditChange = InbCallPostEditChange;
	RecreateExistingRenderStateContext = nullptr;
	ComponentReregisterContexts.Empty();
	//Validation of the data
	if (bCallPostEditChange && !bReregisterComponents)
	{
		//We never want to call PostEditChange without re register components, since PostEditChange will recreate the skeletalmesh render resources
		ensure(bReregisterComponents);
		bReregisterComponents = true;
	}
	if (InSkeletalMesh != nullptr)
	{
		//Only set a valid skeletal mesh
		SetSkeletalMesh(InSkeletalMesh);
	}
}

FScopedSkeletalMeshPostEditChange::~FScopedSkeletalMeshPostEditChange()
{
	//If decrementing the post edit change stack counter return 0 it mean we are the first scope call instance, so we have to call posteditchange and re register component
	if (SkeletalMesh != nullptr && SkeletalMesh->UnStackPostEditChange() == 0)
	{
		if (bCallPostEditChange)
		{
			SkeletalMesh->PostEditChange();
		}
	}
	//If there is some re register data it will be delete when the destructor go out of scope. This will re register
}

void FScopedSkeletalMeshPostEditChange::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	//Some parallel task may try to call post edit change, we must prevent it
	if (!IsInGameThread())
	{
		return;
	}
	//We cannot set a different skeletal mesh, check that it was construct with null
	check(SkeletalMesh == nullptr);
	//We can only set a valid skeletal mesh
	check(InSkeletalMesh != nullptr);

	SkeletalMesh = InSkeletalMesh;
	//If we are the first to increment, unregister the data we need to
	if (SkeletalMesh->StackPostEditChange() == 1)
	{
		//Only allocate data if we re register
		if (bReregisterComponents)
		{
			//Make sure all components using this skeletalmesh have there render ressources free
			RecreateExistingRenderStateContext = new FSkinnedMeshComponentRecreateRenderStateContext(InSkeletalMesh, false);

			// Now iterate over all skeletal mesh components and unregister them from the world, we will reregister them in the destructor
			for (TObjectIterator<USkinnedMeshComponent> It; It; ++It)
			{
				USkinnedMeshComponent* SkinComp = *It;
				if (SkinComp->SkeletalMesh == SkeletalMesh)
				{
					ComponentReregisterContexts.Add(new FComponentReregisterContext(SkinComp));
				}
			}
		}

		if (bCallPostEditChange)
		{
			//Make sure the render ressource use by the skeletalMesh is free, we will reconstruct them when a PostEditChange will be call
			SkeletalMesh->FlushRenderState();
		}
	}
}

#endif //WITH_EDITOR

/*-----------------------------------------------------------------------------
	USkeletalMesh
-----------------------------------------------------------------------------*/


USkeletalMesh::USkeletalMesh(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetSkelMirrorAxis(EAxis::X);
	SetSkelMirrorFlipAxis(EAxis::Z);
#if WITH_EDITORONLY_DATA
	ImportedModel = MakeShareable(new FSkeletalMeshModel());
	SetVertexColorGuid(FGuid());
	SetSupportLODStreaming(FPerPlatformBool(false));
	SetMaxNumStreamedLODs(FPerPlatformInt(0));
	// TODO: support saving some but not all optional LODs
	SetMaxNumOptionalLODs(FPerPlatformInt(0));
#endif
	SetMinLod(FPerPlatformInt(0));
	SetDisableBelowMinLodStripping(FPerPlatformBool(false));
	bSupportRayTracing = true;
}

USkeletalMesh::USkeletalMesh(FVTableHelper& Helper)
	: Super(Helper)
{
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
USkeletalMesh::~USkeletalMesh() = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void USkeletalMesh::PostInitProperties()
{
#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		SetAssetImportData(NewObject<UAssetImportData>(this, TEXT("AssetImportData")));
	}
#endif
	Super::PostInitProperties();
}

FBoxSphereBounds USkeletalMesh::GetBounds() const
{
	return ExtendedBounds;
}

FBoxSphereBounds USkeletalMesh::GetImportedBounds() const
{
	return ImportedBounds;
}

void USkeletalMesh::SetImportedBounds(const FBoxSphereBounds& InBounds)
{
	ImportedBounds = InBounds;
	CalculateExtendedBounds();
}

void USkeletalMesh::SetPositiveBoundsExtension(const FVector& InExtension)
{
	PositiveBoundsExtension = InExtension;
	CalculateExtendedBounds();
}

void USkeletalMesh::SetNegativeBoundsExtension(const FVector& InExtension)
{
	NegativeBoundsExtension = InExtension;
	CalculateExtendedBounds();
}

void USkeletalMesh::CalculateExtendedBounds()
{
	FBoxSphereBounds CalculatedBounds = ImportedBounds;

	// Convert to Min and Max
	FVector Min = CalculatedBounds.Origin - CalculatedBounds.BoxExtent;
	FVector Max = CalculatedBounds.Origin + CalculatedBounds.BoxExtent;
	// Apply bound extensions
	Min -= NegativeBoundsExtension;
	Max += PositiveBoundsExtension;
	// Convert back to Origin, Extent and update SphereRadius
	CalculatedBounds.Origin = (Min + Max) / 2;
	CalculatedBounds.BoxExtent = (Max - Min) / 2;
	CalculatedBounds.SphereRadius = CalculatedBounds.BoxExtent.GetAbsMax();

	ExtendedBounds = CalculatedBounds;
}

void USkeletalMesh::ValidateBoundsExtension()
{
	FVector HalfExtent = ImportedBounds.BoxExtent;

	PositiveBoundsExtension.X = FMath::Clamp(PositiveBoundsExtension.X, -HalfExtent.X, MAX_flt);
	PositiveBoundsExtension.Y = FMath::Clamp(PositiveBoundsExtension.Y, -HalfExtent.Y, MAX_flt);
	PositiveBoundsExtension.Z = FMath::Clamp(PositiveBoundsExtension.Z, -HalfExtent.Z, MAX_flt);

	NegativeBoundsExtension.X = FMath::Clamp(NegativeBoundsExtension.X, -HalfExtent.X, MAX_flt);
	NegativeBoundsExtension.Y = FMath::Clamp(NegativeBoundsExtension.Y, -HalfExtent.Y, MAX_flt);
	NegativeBoundsExtension.Z = FMath::Clamp(NegativeBoundsExtension.Z, -HalfExtent.Z, MAX_flt);
}

#if WITH_EDITOR
/* Return true if the reduction settings are setup to reduce a LOD*/
bool USkeletalMesh::IsReductionActive(int32 LODIndex) const
{
	//Invalid LOD are not reduced
	if(!IsValidLODIndex(LODIndex))
	{
		return false;
	}

	bool bReductionActive = false;
	if (IMeshReduction* ReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionManagerModule>("MeshReductionInterface").GetSkeletalMeshReductionInterface())
	{
		FSkeletalMeshOptimizationSettings ReductionSettings = GetReductionSettings(LODIndex);
		uint32 LODVertexNumber = MAX_uint32;
		uint32 LODTriNumber = MAX_uint32;
		const FSkeletalMeshLODInfo* LODInfoPtr = GetLODInfo(LODIndex);
		bool bLODHasBeenSimplified = LODInfoPtr && LODInfoPtr->bHasBeenSimplified;
		if (GetImportedModel() && GetImportedModel()->LODModels.IsValidIndex(LODIndex))
		{
			if (!bLODHasBeenSimplified)
			{
				LODVertexNumber = 0;
				LODTriNumber = 0;
				const FSkeletalMeshLODModel& LODModel = GetImportedModel()->LODModels[LODIndex];
				//We can take the vertices and triangles count from the source model
				for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); ++SectionIndex)
				{
					const FSkelMeshSection& Section = LODModel.Sections[SectionIndex];

					//Make sure the count fit in a uint32
					LODVertexNumber += Section.NumVertices < 0 ? 0 : Section.NumVertices;
					LODTriNumber += Section.NumTriangles;
				}
			}
			else if (GetImportedModel()->OriginalReductionSourceMeshData.IsValidIndex(LODIndex)
				&& !GetImportedModel()->OriginalReductionSourceMeshData[LODIndex]->IsEmpty())
			{
				//In this case we have to use the stored reduction source data to know how many vertices/triangles we have before the reduction
				USkeletalMesh* MutableSkeletalMesh = const_cast<USkeletalMesh*>(this);
				GetImportedModel()->OriginalReductionSourceMeshData[LODIndex]->GetGeometryInfo(LODVertexNumber, LODTriNumber, MutableSkeletalMesh);
			}
		}
		bReductionActive = ReductionModule->IsReductionActive(ReductionSettings, LODVertexNumber, LODTriNumber);
	}
	return bReductionActive;
}

/* Get a copy of the reduction settings for a specified LOD index. */
FSkeletalMeshOptimizationSettings USkeletalMesh::GetReductionSettings(int32 LODIndex) const
{
	check(IsValidLODIndex(LODIndex));
	const FSkeletalMeshLODInfo& CurrentLODInfo = *(GetLODInfo(LODIndex));
	return CurrentLODInfo.ReductionSettings;
}

#endif

void USkeletalMesh::AddClothingAsset(UClothingAssetBase* InNewAsset)
{
	// Check the outer is us
	if(InNewAsset && InNewAsset->GetOuter() == this)
	{
		// Ok this should be a correctly created asset, we can add it
		GetMeshClothingAssets().AddUnique(InNewAsset);

		// Consolidate the shared cloth configs
		InNewAsset->PostUpdateAllAssets();

#if WITH_EDITOR
		OnClothingChange.Broadcast();
#endif
	}
}

#if WITH_EDITOR
void USkeletalMesh::RemoveClothingAsset(int32 InLodIndex, int32 InSectionIndex)
{
	if(UClothingAssetBase* Asset = GetSectionClothingAsset(InLodIndex, InSectionIndex))
	{
		Asset->UnbindFromSkeletalMesh(this, InLodIndex);
		GetMeshClothingAssets().Remove(Asset);
		OnClothingChange.Broadcast();
	}
}
#endif

UClothingAssetBase* USkeletalMesh::GetSectionClothingAsset(int32 InLodIndex, int32 InSectionIndex)
{
	if(FSkeletalMeshRenderData* SkelResource = GetResourceForRendering())
	{
		if(SkelResource->LODRenderData.IsValidIndex(InLodIndex))
		{
			FSkeletalMeshLODRenderData& LodData = SkelResource->LODRenderData[InLodIndex];
			if(LodData.RenderSections.IsValidIndex(InSectionIndex))
			{
				FSkelMeshRenderSection& Section = LodData.RenderSections[InSectionIndex];

				FGuid ClothingAssetGuid = Section.ClothingData.AssetGuid;

				if(ClothingAssetGuid.IsValid())
				{
					UClothingAssetBase** FoundAsset = GetMeshClothingAssets().FindByPredicate([&](UClothingAssetBase* InAsset)
					{
						return InAsset && InAsset->GetAssetGuid() == ClothingAssetGuid;
					});

					return FoundAsset ? *FoundAsset : nullptr;
				}
			}
		}
	}

	return nullptr;
}

const UClothingAssetBase* USkeletalMesh::GetSectionClothingAsset(int32 InLodIndex, int32 InSectionIndex) const
{
	if (FSkeletalMeshRenderData* SkelResource = GetResourceForRendering())
	{
		if (SkelResource->LODRenderData.IsValidIndex(InLodIndex))
		{
			FSkeletalMeshLODRenderData& LodData = SkelResource->LODRenderData[InLodIndex];
			if (LodData.RenderSections.IsValidIndex(InSectionIndex))
			{
				FSkelMeshRenderSection& Section = LodData.RenderSections[InSectionIndex];

				FGuid ClothingAssetGuid = Section.ClothingData.AssetGuid;

				if (ClothingAssetGuid.IsValid())
				{
					UClothingAssetBase* const* FoundAsset = GetMeshClothingAssets().FindByPredicate([&](UClothingAssetBase* InAsset)
					{
						return InAsset && InAsset->GetAssetGuid() == ClothingAssetGuid;
					});

					return FoundAsset ? *FoundAsset : nullptr;
				}
			}
		}
	}

	return nullptr;
}

UClothingAssetBase* USkeletalMesh::GetClothingAsset(const FGuid& InAssetGuid) const
{
	if(!InAssetGuid.IsValid())
	{
		return nullptr;
	}

	UClothingAssetBase* const* FoundAsset = GetMeshClothingAssets().FindByPredicate([&](UClothingAssetBase* CurrAsset)
	{
		return CurrAsset && CurrAsset->GetAssetGuid() == InAssetGuid;
	});

	return FoundAsset ? *FoundAsset : nullptr;
}

int32 USkeletalMesh::GetClothingAssetIndex(UClothingAssetBase* InAsset) const
{
	return InAsset ? GetClothingAssetIndex(InAsset->GetAssetGuid()) : INDEX_NONE;
}

int32 USkeletalMesh::GetClothingAssetIndex(const FGuid& InAssetGuid) const
{
	const TArray<UClothingAssetBase*>& CachedMeshClothingAssets = GetMeshClothingAssets();
	const int32 NumAssets = CachedMeshClothingAssets.Num();
	for(int32 SearchIndex = 0; SearchIndex < NumAssets; ++SearchIndex)
	{
		if(CachedMeshClothingAssets[SearchIndex] &&
		   CachedMeshClothingAssets[SearchIndex]->GetAssetGuid() == InAssetGuid)
		{
			return SearchIndex;
		}
	}
	return INDEX_NONE;
}

bool USkeletalMesh::HasActiveClothingAssets() const
{
#if WITH_EDITOR
	return ComputeActiveClothingAssets();
#else
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return bHasActiveClothingAssets;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
}

bool USkeletalMesh::HasActiveClothingAssetsForLOD(int32 LODIndex) const
{
	if(FSkeletalMeshRenderData* Resource = GetResourceForRendering())
	{
		if (Resource->LODRenderData.IsValidIndex(LODIndex))
		{
			const FSkeletalMeshLODRenderData& LodData = Resource->LODRenderData[LODIndex];
			const int32 NumSections = LodData.RenderSections.Num();
			for(int32 SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
			{
				const FSkelMeshRenderSection& Section = LodData.RenderSections[SectionIdx];

				if(Section.ClothingData.AssetGuid.IsValid())
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool USkeletalMesh::ComputeActiveClothingAssets() const
{
	if(FSkeletalMeshRenderData* Resource = GetResourceForRendering())
	{
		for(const FSkeletalMeshLODRenderData& LodData : Resource->LODRenderData)
		{
			const int32 NumSections = LodData.RenderSections.Num();
			for(int32 SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
			{
				const FSkelMeshRenderSection& Section = LodData.RenderSections[SectionIdx];

				if(Section.ClothingData.AssetGuid.IsValid())
				{
					return true;
				}
			}
		}
	}

	return false;
}

void USkeletalMesh::GetClothingAssetsInUse(TArray<UClothingAssetBase*>& OutClothingAssets) const
{
	OutClothingAssets.Reset();
	
	if(FSkeletalMeshRenderData* Resource = GetResourceForRendering())
	{
		for (FSkeletalMeshLODRenderData& LodData : Resource->LODRenderData)
		{
			const int32 NumSections = LodData.RenderSections.Num();
			for(int32 SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
			{
				FSkelMeshRenderSection& Section = LodData.RenderSections[SectionIdx];
				if(Section.ClothingData.AssetGuid.IsValid())
				{
					if(UClothingAssetBase* Asset = GetClothingAsset(Section.ClothingData.AssetGuid))
					{
						OutClothingAssets.AddUnique(Asset);
					}
				}
			}
		}
	}
}

bool USkeletalMesh::NeedCPUData(int32 LODIndex)const
{
	return SamplingInfo.IsSamplingEnabled(this, LODIndex);
}

void USkeletalMesh::InitResources()
{
	LLM_SCOPE_BYNAME(TEXT("SkeletalMesh/InitResources")); // This is an important test case for SCOPE_BYNAME without a matching LLM_DEFINE_TAG

	UpdateUVChannelData(false);
	CachedSRRState.Clear();

	FSkeletalMeshRenderData* SkelMeshRenderData = GetResourceForRendering();
	if (SkelMeshRenderData)
	{
#if WITH_EDITOR
		//Editor sanity check, we must ensure all the data is in sync between LODModel, RenderData and UserSectionsData
		if (ImportedModel.IsValid())
		{
			for (int32 LODIndex = 0; LODIndex < GetLODNum(); ++LODIndex)
			{
				if (!ImportedModel->LODModels.IsValidIndex(LODIndex) || !SkeletalMeshRenderData->LODRenderData.IsValidIndex(LODIndex))
				{
					continue;
				}
				const FSkeletalMeshLODModel& ImportLODModel = ImportedModel->LODModels[LODIndex];
				FSkeletalMeshLODRenderData& RenderLODModel = SkeletalMeshRenderData->LODRenderData[LODIndex];
				check(ImportLODModel.Sections.Num() == RenderLODModel.RenderSections.Num());
				for (int32 SectionIndex = 0; SectionIndex < ImportLODModel.Sections.Num(); ++SectionIndex)
				{
					const FSkelMeshSection& ImportSection = ImportLODModel.Sections[SectionIndex];
					
					//In Editor we want to make sure the data is in sync between UserSectionsData and LODModel Sections
					const FSkelMeshSourceSectionUserData& SectionUserData = ImportLODModel.UserSectionsData.FindChecked(ImportSection.OriginalDataSectionIndex);
					bool bImportDataInSync = SectionUserData.bDisabled == ImportSection.bDisabled &&
						SectionUserData.bCastShadow == ImportSection.bCastShadow &&
						SectionUserData.bRecomputeTangent == ImportSection.bRecomputeTangent &&
						SectionUserData.RecomputeTangentsVertexMaskChannel == ImportSection.RecomputeTangentsVertexMaskChannel;
					//Check the cloth only for parent section, since chunked section should not have cloth
					if (bImportDataInSync && ImportSection.ChunkedParentSectionIndex == INDEX_NONE)
					{
						bImportDataInSync = SectionUserData.CorrespondClothAssetIndex == ImportSection.CorrespondClothAssetIndex &&
							SectionUserData.ClothingData.AssetGuid == ImportSection.ClothingData.AssetGuid &&
							SectionUserData.ClothingData.AssetLodIndex == ImportSection.ClothingData.AssetLodIndex;
					}
					
					//In Editor we want to make sure the data is in sync between UserSectionsData and RenderSections
					const FSkelMeshRenderSection& RenderSection = RenderLODModel.RenderSections[SectionIndex];
					bool bRenderDataInSync = SectionUserData.bDisabled == RenderSection.bDisabled &&
						SectionUserData.bCastShadow == RenderSection.bCastShadow &&
						SectionUserData.bRecomputeTangent == RenderSection.bRecomputeTangent &&
						SectionUserData.RecomputeTangentsVertexMaskChannel == RenderSection.RecomputeTangentsVertexMaskChannel &&
						SectionUserData.CorrespondClothAssetIndex == RenderSection.CorrespondClothAssetIndex &&
						SectionUserData.ClothingData.AssetGuid == RenderSection.ClothingData.AssetGuid &&
						SectionUserData.ClothingData.AssetLodIndex == RenderSection.ClothingData.AssetLodIndex;

					if (!bImportDataInSync || !bRenderDataInSync)
					{
						UE_ASSET_LOG(LogSkeletalMesh, Error, this, TEXT("Data out of sync in lod %d. bImportDataInSync=%d, bRenderDataInSync=%d. This happen when DDC cache has corrupted data (Key has change during the skeletalmesh build)"), LODIndex, bImportDataInSync, bRenderDataInSync);
					}
				}
			}
		}
#endif
		bool bAllLODsLookValid = true;	// TODO figure this out
		for (int32 LODIdx = 0; LODIdx < SkeletalMeshRenderData->LODRenderData.Num(); ++LODIdx)
		{
			const FSkeletalMeshLODRenderData& LODRenderData = SkeletalMeshRenderData->LODRenderData[LODIdx];
			if (!LODRenderData.GetNumVertices() && (!LODRenderData.bIsLODOptional || LODRenderData.BuffersSize > 0))
			{
				bAllLODsLookValid = false;
				break;
			}
		}

		{
			const int32 NumLODs = SkelMeshRenderData->LODRenderData.Num();
			const int32 MinFirstLOD = GetMinLod().GetValue();

			CachedSRRState.NumNonStreamingLODs = SkelMeshRenderData->NumInlinedLODs;
			CachedSRRState.NumNonOptionalLODs = SkelMeshRenderData->NumNonOptionalLODs;
			// Limit the number of LODs based on MinLOD value.
			CachedSRRState.MaxNumLODs = FMath::Clamp<int32>(NumLODs - MinFirstLOD, SkelMeshRenderData->NumInlinedLODs, NumLODs);
			CachedSRRState.AssetLODBias = MinFirstLOD;
			CachedSRRState.LODBiasModifier = SkelMeshRenderData->LODBiasModifier;
			// The optional LOD might be culled now.
			CachedSRRState.NumNonOptionalLODs = FMath::Min(CachedSRRState.NumNonOptionalLODs, CachedSRRState.MaxNumLODs);
			// Set LOD count to fit the current state.
			CachedSRRState.NumResidentLODs = NumLODs - SkelMeshRenderData->CurrentFirstLODIdx;
			CachedSRRState.NumRequestedLODs = CachedSRRState.NumResidentLODs;
			// Set whether the mips can be streamed.
			CachedSRRState.bSupportsStreaming = !NeverStream && bAllLODsLookValid && CachedSRRState.NumNonStreamingLODs != CachedSRRState.MaxNumLODs;
		}

		// TODO : Update RenderData->CurrentFirstLODIdx based on whether IStreamingManager::Get().IsRenderAssetStreamingEnabled(EStreamableRenderAssetType::SkeletalMesh).

		SkelMeshRenderData->InitResources(GetHasVertexColors(), GetMorphTargets(), this);
		CachedSRRState.bHasPendingInitHint = true;
	}

	LinkStreaming();
}


void USkeletalMesh::ReleaseResources()
{
	FSkeletalMeshRenderData* SkelMeshRenderData = GetResourceForRendering();
	if (SkelMeshRenderData && SkelMeshRenderData->IsInitialized())
	{

		if(GIsEditor && !GIsPlayInEditorWorld)
		{
			//Flush the rendering command to be sure there is no command left that can create/modify a rendering ressource
			FlushRenderingCommands();
		}

		SkelMeshRenderData->ReleaseResources();

		// insert a fence to signal when these commands completed
		ReleaseResourcesFence.BeginFence();
	}
}

#if WITH_EDITORONLY_DATA
static void AccumulateUVDensities(float* OutWeightedUVDensities, float* OutWeights, const FSkeletalMeshLODRenderData& LODData, const FSkelMeshRenderSection& Section)
{
	const int32 NumTotalTriangles = LODData.GetTotalFaces();
	const int32 NumCoordinateIndex = FMath::Min<int32>(LODData.GetNumTexCoords(), TEXSTREAM_MAX_NUM_UVCHANNELS);

	FUVDensityAccumulator UVDensityAccs[TEXSTREAM_MAX_NUM_UVCHANNELS];
	for (int32 CoordinateIndex = 0; CoordinateIndex < NumCoordinateIndex; ++CoordinateIndex)
	{
		UVDensityAccs[CoordinateIndex].Reserve(NumTotalTriangles);
	}

	TArray<uint32> Indices;
	LODData.MultiSizeIndexContainer.GetIndexBuffer( Indices );
	if (!Indices.Num()) return;

	const uint32* SrcIndices = Indices.GetData() + Section.BaseIndex;
	uint32 NumTriangles = Section.NumTriangles;

	// Figure out Unreal unit per texel ratios.
	for (uint32 TriangleIndex=0; TriangleIndex < NumTriangles; TriangleIndex++ )
	{
		//retrieve indices
		uint32 Index0 = SrcIndices[TriangleIndex*3];
		uint32 Index1 = SrcIndices[TriangleIndex*3+1];
		uint32 Index2 = SrcIndices[TriangleIndex*3+2];

		const float Aera = FUVDensityAccumulator::GetTriangleAera(
			LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index0),
			LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index1),
			LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index2));

		if (Aera > SMALL_NUMBER)
		{
			for (int32 CoordinateIndex = 0; CoordinateIndex < NumCoordinateIndex; ++CoordinateIndex)
			{
				const float UVAera = FUVDensityAccumulator::GetUVChannelAera(
					LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index0, CoordinateIndex),
					LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index1, CoordinateIndex),
					LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index2, CoordinateIndex));

				UVDensityAccs[CoordinateIndex].PushTriangle(Aera, UVAera);
			}
		}
	}

	for (int32 CoordinateIndex = 0; CoordinateIndex < NumCoordinateIndex; ++CoordinateIndex)
	{
		UVDensityAccs[CoordinateIndex].AccumulateDensity(OutWeightedUVDensities[CoordinateIndex], OutWeights[CoordinateIndex]);
	}
}
#endif

void USkeletalMesh::UpdateUVChannelData(bool bRebuildAll)
{
#if WITH_EDITORONLY_DATA
	// Once cooked, the data requires to compute the scales will not be CPU accessible.
	FSkeletalMeshRenderData* Resource = GetResourceForRendering();
	if (FPlatformProperties::HasEditorOnlyData() && Resource)
	{
		TArray<FSkeletalMaterial>& MeshMaterials = GetMaterials();
		for (int32 MaterialIndex = 0; MaterialIndex < MeshMaterials.Num(); ++MaterialIndex)
		{
			FMeshUVChannelInfo& UVChannelData = MeshMaterials[MaterialIndex].UVChannelData;

			// Skip it if we want to keep it.
			if (UVChannelData.IsInitialized() && (!bRebuildAll || UVChannelData.bOverrideDensities))
				continue;

			float WeightedUVDensities[TEXSTREAM_MAX_NUM_UVCHANNELS] = {0, 0, 0, 0};
			float Weights[TEXSTREAM_MAX_NUM_UVCHANNELS] = {0, 0, 0, 0};

			for (const FSkeletalMeshLODRenderData& LODData : Resource->LODRenderData)
			{
				for (const FSkelMeshRenderSection& SectionInfo : LODData.RenderSections)
				{
					if (SectionInfo.MaterialIndex != MaterialIndex)
							continue;

					AccumulateUVDensities(WeightedUVDensities, Weights, LODData, SectionInfo);
				}
			}

			UVChannelData.bInitialized = true;
			UVChannelData.bOverrideDensities = false;
			for (int32 CoordinateIndex = 0; CoordinateIndex < TEXSTREAM_MAX_NUM_UVCHANNELS; ++CoordinateIndex)
			{
				UVChannelData.LocalUVDensities[CoordinateIndex] = (Weights[CoordinateIndex] > KINDA_SMALL_NUMBER) ? (WeightedUVDensities[CoordinateIndex] / Weights[CoordinateIndex]) : 0;
			}
		}

		Resource->SyncUVChannelData(GetMaterials());
	}
#endif
}

const FMeshUVChannelInfo* USkeletalMesh::GetUVChannelData(int32 MaterialIndex) const
{
	if (GetMaterials().IsValidIndex(MaterialIndex))
	{
		ensure(GetMaterials()[MaterialIndex].UVChannelData.bInitialized);
		return &GetMaterials()[MaterialIndex].UVChannelData;
	}

	return nullptr;
}

void USkeletalMesh::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);
	// Default implementation handles subobjects

	if (SkeletalMeshRenderData.IsValid())
	{
		SkeletalMeshRenderData->GetResourceSizeEx(CumulativeResourceSize);
	}

	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(GetRefBasesInvMatrix().GetAllocatedSize());
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(GetRefSkeleton().GetDataSize());
}

int32 USkeletalMesh::CalcCumulativeLODSize(int32 NumLODs) const
{
	uint32 Accum = 0;
	const int32 LODCount = GetLODNum();
	const int32 LastLODIdx = LODCount - NumLODs;
	for (int32 LODIdx = LODCount - 1; LODIdx >= LastLODIdx; --LODIdx)
	{
		Accum += SkeletalMeshRenderData->LODRenderData[LODIdx].BuffersSize;
	}
	check(Accum >= 0u);
	return Accum;
}

#if USE_BULKDATA_STREAMING_TOKEN
bool USkeletalMesh::GetMipDataFilename(const int32 MipIndex, FString& OutBulkDataFilename) const
{
	// TODO: this is slow. Should cache the name once per mesh
	FString PackageName = GetOutermost()->FileName.ToString();
	// Handle name redirection and localization
	const FCoreRedirectObjectName RedirectedName =
		FCoreRedirects::GetRedirectedName(
			ECoreRedirectFlags::Type_Package,
			FCoreRedirectObjectName(NAME_None, NAME_None, *PackageName));
	FString LocalizedName;
	LocalizedName = FPackageName::GetDelegateResolvedPackagePath(RedirectedName.PackageName.ToString());
	LocalizedName = FPackageName::GetLocalizedPackagePath(LocalizedName);
	bool bSucceed = FPackageName::DoesPackageExist(LocalizedName, nullptr, &OutBulkDataFilename);
	check(bSucceed);
	const FSkeletalMeshRenderData* SkelMeshRenderData = GetResourceForRendering();
	const bool bLODIsOptional = SkelMeshRenderData ? (MipIndex < SkelMeshRenderData->LODRenderData.Num() - SkelMeshRenderData->NumNonOptionalLODs) : false;
	OutBulkDataFilename = FPaths::ChangeExtension(OutBulkDataFilename, bLODIsOptional ? TEXT(".uptnl") : TEXT(".ubulk"));
	return true;
}
#endif // USE_BULKDATA_STREAMING_TOKEN

FIoFilenameHash USkeletalMesh::GetMipIoFilenameHash(const int32 MipIndex) const
{
#if USE_BULKDATA_STREAMING_TOKEN
	FString MipFilename;
	if (GetMipDataFilename(MipIndex, MipFilename))
	{
		return MakeIoFilenameHash(MipFilename);
	}
#else
	if (SkeletalMeshRenderData && SkeletalMeshRenderData->LODRenderData.IsValidIndex(MipIndex))
	{
		return SkeletalMeshRenderData->LODRenderData[MipIndex].StreamingBulkData.GetIoFilenameHash();
	}
#endif
	else
	{
		return INVALID_IO_FILENAME_HASH;
	}
}

bool USkeletalMesh::DoesMipDataExist(const int32 MipIndex) const
{
#if USE_BULKDATA_STREAMING_TOKEN
	FString MipDataFilename;
	return GetMipDataFilename(MipIndex, MipDataFilename) && IFileManager::Get().FileExists(*MipDataFilename);
#else
	return SkeletalMeshRenderData && SkeletalMeshRenderData->LODRenderData.IsValidIndex(MipIndex) && SkeletalMeshRenderData->LODRenderData[MipIndex].StreamingBulkData.DoesExist();
#endif
}

bool USkeletalMesh::HasPendingRenderResourceInitialization() const
{
	return SkeletalMeshRenderData && !SkeletalMeshRenderData->bReadyForStreaming;
}

bool USkeletalMesh::StreamOut(int32 NewMipCount)
{
	check(IsInGameThread());
	if (!HasPendingInitOrStreaming() && CachedSRRState.StreamOut(NewMipCount))
	{
		PendingUpdate = new FSkeletalMeshStreamOut(this);
		return !PendingUpdate->IsCancelled();
	}
	return false;
}

bool USkeletalMesh::StreamIn(int32 NewMipCount, bool bHighPrio)
{
	check(IsInGameThread());
	if (!HasPendingInitOrStreaming() && CachedSRRState.StreamIn(NewMipCount))
	{
#if WITH_EDITOR
		if (FPlatformProperties::HasEditorOnlyData())
		{
			if (GRHISupportsAsyncTextureCreation)
			{
				PendingUpdate = new FSkeletalMeshStreamIn_DDC_Async(this);
			}
			else
			{
				PendingUpdate = new FSkeletalMeshStreamIn_DDC_RenderThread(this);
			}
		}
		else
#endif
		{
			if (GRHISupportsAsyncTextureCreation)
			{
				PendingUpdate = new FSkeletalMeshStreamIn_IO_Async(this, bHighPrio);
			}
			else
			{
				PendingUpdate = new FSkeletalMeshStreamIn_IO_RenderThread(this, bHighPrio);
			}
		}
		return !PendingUpdate->IsCancelled();
	}
	return false;
}

void USkeletalMesh::CancelAllPendingStreamingActions()
{
	FlushRenderingCommands();

	for (TObjectIterator<USkeletalMesh> It; It; ++It)
	{
		USkeletalMesh* StaticMesh = *It;
		StaticMesh->CancelPendingStreamingRequest();
	}

	FlushRenderingCommands();
}

/**
 * Operator for MemCount only, so it only serializes the arrays that needs to be counted.
*/
FArchive &operator<<( FArchive& Ar, FSkeletalMeshLODInfo& I )
{
	Ar << I.LODMaterialMap;

#if WITH_EDITORONLY_DATA
	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_MOVE_SKELETALMESH_SHADOWCASTING )
	{
		Ar << I.bEnableShadowCasting_DEPRECATED;
	}
#endif

	// fortnite version
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	Ar.UsingCustomVersion(FSkeletalMeshCustomVersion::GUID);
	if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::RemoveTriangleSorting)
	{
		uint8 DummyTriangleSorting;
		Ar << DummyTriangleSorting;

		uint8 DummyCustomLeftRightAxis;
		Ar << DummyCustomLeftRightAxis;

		FName DummyCustomLeftRightBoneName;
		Ar << DummyCustomLeftRightBoneName;
		}


	return Ar;
}

void RefreshSkelMeshOnPhysicsAssetChange(const USkeletalMesh* InSkeletalMesh)
{
	if (InSkeletalMesh)
	{
		for (FThreadSafeObjectIterator Iter(USkeletalMeshComponent::StaticClass()); Iter; ++Iter)
		{
			USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(*Iter);
			// if PhysicsAssetOverride is NULL, it uses SkeletalMesh Physics Asset, so I'll need to update here
			if  (SkeletalMeshComponent->SkeletalMesh == InSkeletalMesh &&
				 SkeletalMeshComponent->PhysicsAssetOverride == NULL)
			{
				// it needs to recreate IF it already has been created
				if (SkeletalMeshComponent->IsPhysicsStateCreated())
				{
					// do not call SetPhysAsset as it will setup physics asset override
					SkeletalMeshComponent->RecreatePhysicsState();
					SkeletalMeshComponent->UpdateHasValidBodies();
				}
			}
		}
#if WITH_EDITOR
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
#endif // WITH_EDITOR
	}
}

#if WITH_EDITOR

int32 USkeletalMesh::StackPostEditChange()
{
	check(PostEditChangeStackCounter >= 0);
	//Return true if this is the first stack ID
	PostEditChangeStackCounter++;
	return PostEditChangeStackCounter;
}

int32 USkeletalMesh::UnStackPostEditChange()
{
	check(PostEditChangeStackCounter > 0);
	PostEditChangeStackCounter--;
	return PostEditChangeStackCounter;
}

void USkeletalMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PostEditChangeStackCounter > 0)
	{
		//Ignore those call when we have an active delay stack
		return;
	}
	//Block any re-entrant call by incrementing PostEditChangeStackCounter. It will be decrement when we will go out of scope.
	const bool bCallPostEditChange = false;
	const bool bReRegisterComponents = false;
	FScopedSkeletalMeshPostEditChange BlockRecursiveCallScope(this, bCallPostEditChange, bReRegisterComponents);

	bool bFullPrecisionUVsReallyChanged = false;

	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	
	bool bHasToReregisterComponent = false;
	// Don't invalidate render data when dragging sliders, too slow
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		Build();
		bHasToReregisterComponent = true;
	}

	if( GIsEditor &&
		PropertyThatChanged &&
		PropertyThatChanged->GetFName() == FName(TEXT("PhysicsAsset")) )
	{
		RefreshSkelMeshOnPhysicsAssetChange(this);
	}

	if( GIsEditor &&
		CastField<FObjectProperty>(PropertyThatChanged) &&
		CastField<FObjectProperty>(PropertyThatChanged)->PropertyClass == UMorphTarget::StaticClass() )
	{
		// A morph target has changed, reinitialize morph target maps
		InitMorphTargets();
	}

	if ( GIsEditor &&
		 PropertyThatChanged &&
		 PropertyThatChanged->GetFName() == GetEnablePerPolyCollisionMemberName()
		)
	{
		BuildPhysicsData();
	}

	if(FProperty* MemberProperty = PropertyChangedEvent.MemberProperty)
	{
		if(MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(USkeletalMesh, PositiveBoundsExtension) ||
			MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(USkeletalMesh, NegativeBoundsExtension))
		{
			// If the bounds extensions change, recalculate extended bounds.
			ValidateBoundsExtension();
			CalculateExtendedBounds();
			bHasToReregisterComponent = true;
		}
	}
		
	if (PropertyThatChanged && PropertyThatChanged->GetFName() == USkeletalMesh::GetPostProcessAnimBlueprintMemberName())
	{
		bHasToReregisterComponent = true;
	}

	if (bHasToReregisterComponent)
	{
		TArray<UActorComponent*> ComponentsToReregister;
		for(TObjectIterator<USkeletalMeshComponent> It; It; ++It)
		{
			USkeletalMeshComponent* MeshComponent = *It;
			if(MeshComponent && !MeshComponent->IsTemplate() && MeshComponent->SkeletalMesh == this)
			{
				ComponentsToReregister.Add(*It);
			}
		}
		FMultiComponentReregisterContext ReregisterContext(ComponentsToReregister);
	}


	if (PropertyThatChanged && PropertyChangedEvent.MemberProperty)
	{
		if (PropertyChangedEvent.MemberProperty->GetFName() == FName(TEXT("SamplingInfo")))
		{
			SamplingInfo.BuildRegions(this);
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == FName(TEXT("LODInfo")))
		{
			SamplingInfo.BuildWholeMesh(this);
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == FName(TEXT("bSupportUniformlyDistributedSampling")))
		{
			SamplingInfo.BuildWholeMesh(this);
		}
	}
	else
	{
		//Rebuild the lot. No property could mean a reimport.
		SamplingInfo.BuildRegions(this);
		SamplingInfo.BuildWholeMesh(this);
	}

	UpdateUVChannelData(true);
	UpdateGenerateUpToData();

	OnMeshChanged.Broadcast();

	for (UAssetUserData* Datum : AssetUserData)
	{
		if (Datum != nullptr)
		{
			Datum->PostEditChangeOwner();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	//The stack counter here should be 1 since the BlockRecursiveCallScope protection has the lock and it will be decrement to 0 when we get out of the function scope
	check(PostEditChangeStackCounter == 1);
}

void USkeletalMesh::PostEditUndo()
{
	Super::PostEditUndo();
	for( TObjectIterator<USkinnedMeshComponent> It; It; ++It )
	{
		USkinnedMeshComponent* MeshComponent = *It;
		if( MeshComponent && 
			!MeshComponent->IsTemplate() &&
			MeshComponent->SkeletalMesh == this )
		{
			FComponentReregisterContext Context(MeshComponent);
		}
	}

	if(GetMorphTargets().Num() > GetMorphTargetIndexMap().Num())
	{
		// A morph target remove has been undone, reinitialise
		InitMorphTargets();
	}
}

void USkeletalMesh::UpdateGenerateUpToData()
{
	for (int32 LodIndex = 0; LodIndex < GetImportedModel()->LODModels.Num(); ++LodIndex)
	{
		FSkeletalMeshLODModel& LodModel = GetImportedModel()->LODModels[LodIndex];
		for (int32 SectionIndex = 0; SectionIndex < LodModel.Sections.Num(); ++SectionIndex)
		{
			int32 SpecifiedLodIndex = LodModel.Sections[SectionIndex].GenerateUpToLodIndex;
			if (SpecifiedLodIndex != -1 && SpecifiedLodIndex < LodIndex)
			{
				LodModel.Sections[SectionIndex].GenerateUpToLodIndex = LodIndex;
			}
		}
	}
}

#endif // WITH_EDITOR

void USkeletalMesh::BeginDestroy()
{
	Super::BeginDestroy();

	if (FSkinWeightProfileManager * Manager = FSkinWeightProfileManager::Get(GetWorld()))
	{
		Manager->CancelSkinWeightProfileRequest(this);
	}

	// remove the cache of link up
	if (GetSkeleton())
	{
		GetSkeleton()->RemoveLinkup(this);
	}

#if WITH_EDITORONLY_DATA
#if WITH_APEX_CLOTHING
	// release clothing assets
	for (FClothingAssetData_Legacy& Data : ClothingAssets_DEPRECATED)
	{
		if (Data.ApexClothingAsset)
		{
			GPhysCommandHandler->DeferredRelease(Data.ApexClothingAsset);
			Data.ApexClothingAsset = nullptr;
		}
	}
#endif // #if WITH_APEX_CLOTHING
#endif // WITH_EDITORONLY_DATA

	// Release the mesh's render resources now if no pending streaming op.
	if (!HasPendingInitOrStreaming())
	{
		ReleaseResources();
	}
}

bool USkeletalMesh::IsReadyForFinishDestroy()
{
	if (!Super::IsReadyForFinishDestroy())
	{
		return false;
	}

	ReleaseResources();

	// see if we have hit the resource flush fence
	return ReleaseResourcesFence.IsFenceComplete();
}

#if WITH_EDITOR
FString BuildSkeletalMeshDerivedDataKey(const ITargetPlatform* TargetPlatform, USkeletalMesh* SkelMesh);

static FSkeletalMeshRenderData& GetPlatformSkeletalMeshRenderData(USkeletalMesh* Mesh, const ITargetPlatform* TargetPlatform)
{
	FString PlatformDerivedDataKey = BuildSkeletalMeshDerivedDataKey(TargetPlatform, Mesh);
	FSkeletalMeshRenderData* PlatformRenderData = Mesh->GetResourceForRendering();
	if (Mesh->GetOutermost()->bIsCookedForEditor)
	{
		check(PlatformRenderData);
		return *PlatformRenderData;
	}

	while (PlatformRenderData && PlatformRenderData->DerivedDataKey != PlatformDerivedDataKey)
	{
		PlatformRenderData = PlatformRenderData->NextCachedRenderData.Get();
	}

	if (PlatformRenderData == NULL)
	{
		// Cache render data for this platform and insert it in to the linked list.
		PlatformRenderData = new FSkeletalMeshRenderData();
		PlatformRenderData->Cache(TargetPlatform, Mesh);
		check(PlatformRenderData->DerivedDataKey == PlatformDerivedDataKey);
		Swap(PlatformRenderData->NextCachedRenderData, Mesh->GetResourceForRendering()->NextCachedRenderData);
		Mesh->GetResourceForRendering()->NextCachedRenderData = TUniquePtr<FSkeletalMeshRenderData>(PlatformRenderData);

		{
			//If the running platform DDC key is not equal to the target platform DDC key.
			//We need to cache the skeletalmesh ddc with the running platform to retrieve the ddc editor data LODModel which can be different because of chunking and reduction
			//Normally it should just take back the ddc for the running platform, since the ddc was cache when we have load the asset to cook it.
			ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
			check(RunningPlatform);
			if (RunningPlatform != TargetPlatform)
			{
				FString RunningPlatformDerivedDataKey = BuildSkeletalMeshDerivedDataKey(RunningPlatform, Mesh);
				if (RunningPlatformDerivedDataKey != PlatformDerivedDataKey)
				{
					FSkeletalMeshRenderData RunningPlatformRenderData;
					RunningPlatformRenderData.Cache(RunningPlatform, Mesh);
					check(RunningPlatformRenderData.DerivedDataKey == RunningPlatformDerivedDataKey);
				}
			}
		}
	}
	check(PlatformRenderData->DerivedDataKey == PlatformDerivedDataKey);
	check(PlatformRenderData);
	return *PlatformRenderData;
}
#endif

LLM_DEFINE_TAG(SkeletalMesh_Serialize); // This is an important test case for LLM_DEFINE_TAG

void USkeletalMesh::Serialize( FArchive& Ar )
{
	LLM_SCOPE_BYNAME(TEXT("SkeletalMesh/Serialize")); // This is an important test case for SCOPE_BYNAME with a matching LLM_DEFINE_TAG
	DECLARE_SCOPE_CYCLE_COUNTER( TEXT("USkeletalMesh::Serialize"), STAT_SkeletalMesh_Serialize, STATGROUP_LoadTime );

	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);
	Ar.UsingCustomVersion(FSkeletalMeshCustomVersion::GUID);
	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	Ar.UsingCustomVersion(FNiagaraObjectVersion::GUID);

	FStripDataFlags StripFlags( Ar );

	Ar << ImportedBounds;

	Ar << GetMaterials();

	Ar << GetRefSkeleton();

	if(Ar.IsLoading())
	{
		const bool bRebuildNameMap = false;
		GetRefSkeleton().RebuildRefSkeleton(GetSkeleton(), bRebuildNameMap);
	}

#if WITH_EDITORONLY_DATA
	// Serialize the source model (if we want editor data)
	if (!StripFlags.IsEditorDataStripped())
	{
		ImportedModel->Serialize(Ar, this);
	}
#endif // WITH_EDITORONLY_DATA

	Ar.UsingCustomVersion(FSkeletalMeshCustomVersion::GUID);
	if (Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) >= FSkeletalMeshCustomVersion::SplitModelAndRenderData)
	{
		bool bCooked = Ar.IsCooking();
		Ar << bCooked;

		const bool bIsDuplicating = Ar.HasAnyPortFlags(PPF_Duplicate);

		// Inline the derived data for cooked builds. Never include render data when
		// counting memory as it is included by GetResourceSize.
		if ((bIsDuplicating || bCooked) && !IsTemplate() && !Ar.IsCountingMemory())
		{
			if (Ar.IsLoading())
			{
				SkeletalMeshRenderData = MakeUnique<FSkeletalMeshRenderData>();
				SkeletalMeshRenderData->Serialize(Ar, this);
			}
			else if (Ar.IsSaving())
			{
				FSkeletalMeshRenderData* LocalSkeletalMeshRenderData = SkeletalMeshRenderData.Get();
#if WITH_EDITORONLY_DATA
				const ITargetPlatform* ArchiveCookingTarget = Ar.CookingTarget();
				if (ArchiveCookingTarget)
				{
					LocalSkeletalMeshRenderData = &GetPlatformSkeletalMeshRenderData(this, ArchiveCookingTarget);
				}
				else
				{
					//Fall back in case we use an archive that the cooking target has not been set (i.e. Duplicate archive)
					ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
					check(RunningPlatform != NULL);
					LocalSkeletalMeshRenderData = &GetPlatformSkeletalMeshRenderData(this, RunningPlatform);
				}
#endif
				if (bCooked)
				{
					int32 MaxBonesPerChunk = LocalSkeletalMeshRenderData->GetMaxBonesPerSection();

					TArray<FName> DesiredShaderFormats;
					Ar.CookingTarget()->GetAllTargetedShaderFormats(DesiredShaderFormats);

					for (int32 FormatIndex = 0; FormatIndex < DesiredShaderFormats.Num(); ++FormatIndex)
					{
						const EShaderPlatform LegacyShaderPlatform = ShaderFormatToLegacyShaderPlatform(DesiredShaderFormats[FormatIndex]);
						const ERHIFeatureLevel::Type FeatureLevelType = GetMaxSupportedFeatureLevel(LegacyShaderPlatform);

						int32 MaxNrBones = FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones(Ar.CookingTarget());
						if (MaxBonesPerChunk > MaxNrBones)
						{
							FString FeatureLevelName;
							GetFeatureLevelName(FeatureLevelType, FeatureLevelName);
							UE_LOG(LogSkeletalMesh, Warning, TEXT("Skeletal mesh %s has a LOD section with %d bones and the maximum supported number for feature level %s is %d.\n!This mesh will not be rendered on the specified platform!"),
								*GetFullName(), MaxBonesPerChunk, *FeatureLevelName, MaxNrBones);
						}
					}
				}
				LocalSkeletalMeshRenderData->Serialize(Ar, this);

			}
		}
	}

	// make sure we're counting properly
	if (!Ar.IsLoading() && !Ar.IsSaving())
	{
		Ar << GetRefBasesInvMatrix();
	}

	if( Ar.UE4Ver() < VER_UE4_REFERENCE_SKELETON_REFACTOR )
	{
		TMap<FName, int32> DummyNameIndexMap;
		Ar << DummyNameIndexMap;
	}

	//@todo legacy
	TArray<UObject*> DummyObjs;
	Ar << DummyObjs;

	if (Ar.IsLoading() && Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::TextureStreamingMeshUVChannelData)
	{
		TArray<float> CachedStreamingTextureFactors;
		Ar << CachedStreamingTextureFactors;
	}

#if WITH_EDITORONLY_DATA
	if ( !StripFlags.IsEditorDataStripped() )
	{
		// Backwards compat for old SourceData member
		// Doing a <= check here as no asset from UE4 streams could ever have been saved at exactly 11, but a stray no-op vesion increment was added
		// in Fortnite/Main meaning some assets there were at exactly version 11. Doing a <= allows us to properly apply this version even to those assets
		if (Ar.IsLoading() && Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) <= FSkeletalMeshCustomVersion::RemoveSourceData)
		{
			bool bHaveSourceData = false;
			Ar << bHaveSourceData;
			if (bHaveSourceData)
			{
				FSkeletalMeshLODModel DummyLODModel;
				DummyLODModel.Serialize(Ar, this, INDEX_NONE);
			}
		}
	}

	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_ASSET_IMPORT_DATA_AS_JSON && !GetAssetImportData())
	{
		// AssetImportData should always be valid
		SetAssetImportData(NewObject<UAssetImportData>(this, TEXT("AssetImportData")));
	}
	
	// SourceFilePath and SourceFileTimestamp were moved into a subobject
	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_ADDED_FBX_ASSET_IMPORT_DATA && GetAssetImportData())
	{
		// AssetImportData should always have been set up in the constructor where this is relevant
		FAssetImportInfo Info;
		Info.Insert(FAssetImportInfo::FSourceFile(SourceFilePath_DEPRECATED));
		GetAssetImportData()->SourceData = MoveTemp(Info);
		
		SourceFilePath_DEPRECATED = TEXT("");
		SourceFileTimestamp_DEPRECATED = TEXT("");
	}

	if (Ar.UE4Ver() >= VER_UE4_APEX_CLOTH)
	{
		if(Ar.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::NewClothingSystemAdded)
		{
		// Serialize non-UPROPERTY ApexClothingAsset data.
			for(int32 Idx = 0; Idx < ClothingAssets_DEPRECATED.Num(); Idx++)
		{
				Ar << ClothingAssets_DEPRECATED[Idx];
			}
		}

		if (Ar.UE4Ver() < VER_UE4_REFERENCE_SKELETON_REFACTOR)
		{
			RebuildRefSkeletonNameToIndexMap();
		}
	}

	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_MOVE_SKELETALMESH_SHADOWCASTING )
	{
		// Previous to this version, shadowcasting flags were stored in the LODInfo array
		// now they're in the Materials array so we need to move them over
		MoveDeprecatedShadowFlagToMaterials();
	}

	if (Ar.UE4Ver() < VER_UE4_SKELETON_ASSET_PROPERTY_TYPE_CHANGE)
	{
		GetPreviewAttachedAssetContainer().SaveAttachedObjectsFromDeprecatedProperties();
	}
#endif

	if (GetEnablePerPolyCollision())
	{
		const USkeletalMesh* ConstThis = this;
		UBodySetup* LocalBodySetup = ConstThis->GetBodySetup();
		Ar << LocalBodySetup;
		SetBodySetup(LocalBodySetup);
	}

#if WITH_EDITORONLY_DATA
	if (Ar.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::RefactorMeshEditorMaterials)
	{
		MoveMaterialFlagsToSections();
	}
#endif

#if WITH_EDITORONLY_DATA
	SetRequiresLODScreenSizeConversion(Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::LODsUseResolutionIndependentScreenSize);
	SetRequiresLODHysteresisConversion(Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::LODHysteresisUseResolutionIndependentScreenSize);
#endif

	if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::ConvertReductionSettingOptions)
	{
		const int32 TotalLODNum = LODInfo.Num();
		for (int32 LodIndex = 1; LodIndex < TotalLODNum; LodIndex++)
		{
			FSkeletalMeshLODInfo& ThisLODInfo = LODInfo[LodIndex];
			// prior to this version, both of them were used
			ThisLODInfo.ReductionSettings.ReductionMethod = SMOT_TriangleOrDeviation;
			if (ThisLODInfo.ReductionSettings.MaxDeviationPercentage == 0.f)
			{
				// 0.f and 1.f should produce same result. However, it is bad to display 0.f in the slider
				// as 0.01 and 0.f causes extreme confusion. 
				ThisLODInfo.ReductionSettings.MaxDeviationPercentage = 1.f;
			}
		}
	}

	if (Ar.IsLoading() && Ar.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::SkeletalMeshBuildRefactor)
	{
		const int32 TotalLODNum = LODInfo.Num();
		for (int32 LodIndex = 0; LodIndex < TotalLODNum; LodIndex++)
		{
			FSkeletalMeshLODInfo& ThisLODInfo = LODInfo[LodIndex];
			// Restore the deprecated settings
			ThisLODInfo.BuildSettings.bUseFullPrecisionUVs = bUseFullPrecisionUVs_DEPRECATED;
			ThisLODInfo.BuildSettings.bUseHighPrecisionTangentBasis = bUseHighPrecisionTangentBasis_DEPRECATED;
			ThisLODInfo.BuildSettings.bBuildAdjacencyBuffer = true;
			ThisLODInfo.BuildSettings.bRemoveDegenerates = true;

			//We cannot get back the imported build option here since those option are store in the UAssetImportData which FBX has derive in the UnrealEd module
			//We are in engine module so there is no way to recover this data.
			//Anyway because the asset was not re-import yet the build settings will not be shown in the UI and the asset will not be build
			//With the new build until it get re-import (geo and skinning)
			//So we will leave the default value for the rest of the new build settings
		}
	}
}

void USkeletalMesh::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);
	OutDeps.Add(GetSkeleton());
}

void USkeletalMesh::FlushRenderState()
{
	//TComponentReregisterContext<USkeletalMeshComponent> ReregisterContext;

	// Release the mesh's render resources.
	ReleaseResources();

	// Flush the resource release commands to the rendering thread to ensure that the edit change doesn't occur while a resource is still
	// allocated, and potentially accessing the mesh data.
	ReleaseResourcesFence.Wait();
}

uint32 USkeletalMesh::GetVertexBufferFlags() const
{
	uint32 VertexFlags = ESkeletalMeshVertexFlags::None;
	if (GetHasVertexColors())
	{
		VertexFlags |= ESkeletalMeshVertexFlags::HasVertexColors;
	}
	return VertexFlags;
}

#if WITH_EDITOR
void USkeletalMesh::Build()
{
	// Unregister all instances of this component
	FSkinnedMeshComponentRecreateRenderStateContext RecreateRenderStateContext(this, false);

	// Release the static mesh's resources.
	ReleaseResources();

	// Flush the resource release commands to the rendering thread to ensure that the build doesn't occur while a resource is still
	// allocated, and potentially accessing the USkeletalMesh.
	ReleaseResourcesFence.Wait();


	// rebuild render data from imported model
	CacheDerivedData();

	// Do not need to fix up 16-bit UVs here, as we assume all editor platforms support them.
	ensure(GVertexElementTypeSupport.IsSupported(VET_Half2));

	// Note: meshes can be built during automated importing.  We should not create resources in that case
	// as they will never be released when this object is deleted
	if (FApp::CanEverRender())
	{
		// Reinitialize the static mesh's resources.
		InitResources();
	}
}
#endif

void USkeletalMesh::PreSave(const class ITargetPlatform* TargetPlatform)
{
	// check the parent index of the root bone is invalid
	check((GetRefSkeleton().GetNum() == 0) || (GetRefSkeleton().GetRefBoneInfo()[0].ParentIndex == INDEX_NONE));

	Super::PreSave(TargetPlatform);
}

// Pre-calculate refpose-to-local transforms
void USkeletalMesh::CalculateInvRefMatrices()
{
	const int32 NumRealBones = GetRefSkeleton().GetRawBoneNum();

	if(GetRefBasesInvMatrix().Num() != NumRealBones)
	{
		GetRefBasesInvMatrix().Empty(NumRealBones);
		GetRefBasesInvMatrix().AddUninitialized(NumRealBones);

		// Reset cached mesh-space ref pose
		CachedComposedRefPoseMatrices.Empty(NumRealBones);
		CachedComposedRefPoseMatrices.AddUninitialized(NumRealBones);

		// Precompute the Mesh.RefBasesInverse.
		for( int32 b=0; b<NumRealBones; b++)
		{
			// Render the default pose.
			CachedComposedRefPoseMatrices[b] = GetRefPoseMatrix(b);

			// Construct mesh-space skeletal hierarchy.
			if( b>0 )
			{
				int32 Parent = GetRefSkeleton().GetRawParentIndex(b);
				CachedComposedRefPoseMatrices[b] = CachedComposedRefPoseMatrices[b] * CachedComposedRefPoseMatrices[Parent];
			}

			FVector XAxis, YAxis, ZAxis;

			CachedComposedRefPoseMatrices[b].GetScaledAxes(XAxis, YAxis, ZAxis);
			if(	XAxis.IsNearlyZero(SMALL_NUMBER) &&
				YAxis.IsNearlyZero(SMALL_NUMBER) &&
				ZAxis.IsNearlyZero(SMALL_NUMBER))
			{
				// this is not allowed, warn them 
				UE_LOG(LogSkeletalMesh, Warning, TEXT("Reference Pose for asset %s for joint (%s) includes NIL matrix. Zero scale isn't allowed on ref pose. "), *GetPathName(), *GetRefSkeleton().GetBoneName(b).ToString());
			}

			// Precompute inverse so we can use from-refpose-skin vertices.
			GetRefBasesInvMatrix()[b] = CachedComposedRefPoseMatrices[b].Inverse();
		}

#if WITH_EDITORONLY_DATA
		if(GetRetargetBasePose().Num() == 0)
		{
			SetRetargetBasePose(GetRefSkeleton().GetRefBonePose());
		}
#endif // WITH_EDITORONLY_DATA
	}
}

#if WITH_EDITOR
void USkeletalMesh::ReallocateRetargetBasePose()
{
	// if you're adding other things here, please note that this function is called during postLoad
	// fix up retarget base pose if VB has changed
	// if we have virtual joints, we make sure Retarget Base Pose matches
	const int32 RawNum = GetRefSkeleton().GetRawBoneNum();
	const int32 VBNum = GetRefSkeleton().GetVirtualBoneRefData().Num();
	const int32 BoneNum = GetRefSkeleton().GetNum();
	check(RawNum + VBNum == BoneNum);

	const int32 OldRetargetBasePoseNum = GetRetargetBasePose().Num();
	// we want to make sure retarget base pose contains raw numbers PREVIOUSLY
	// otherwise, we may override wrong transform
	if (OldRetargetBasePoseNum >= RawNum)
	{
		// we have to do this in case buffer size changes (shrink for example)
		GetRetargetBasePose().SetNum(BoneNum);

		// if we have VB, we should override them
		// they're not editable, so it's fine to override them from raw bones
		if (VBNum > 0)
		{
			const TArray<FTransform>& BonePose = GetRefSkeleton().GetRefBonePose();
			check(GetRetargetBasePose().GetTypeSize() == BonePose.GetTypeSize());
			const int32 ElementSize = GetRetargetBasePose().GetTypeSize();
			FMemory::Memcpy(GetRetargetBasePose().GetData() + RawNum, BonePose.GetData() + RawNum, ElementSize*VBNum);
		}
	}
	else
	{
		// else we think, something has changed, we just override retarget base pose to current pose
		GetRetargetBasePose() = GetRefSkeleton().GetRefBonePose();
	}
}

void USkeletalMesh::CalculateRequiredBones(FSkeletalMeshLODModel& LODModel, const struct FReferenceSkeleton& RefSkeleton, const TMap<FBoneIndexType, FBoneIndexType> * BonesToRemove)
{
	// RequiredBones for base model includes all raw bones.
	int32 RequiredBoneCount = RefSkeleton.GetRawBoneNum();
	LODModel.RequiredBones.Empty(RequiredBoneCount);
	for(int32 i=0; i<RequiredBoneCount; i++)
	{
		// Make sure it's not in BonesToRemove
		// @Todo change this to one TArray
		if (!BonesToRemove || BonesToRemove->Find(i) == NULL)
		{
			LODModel.RequiredBones.Add(i);
		}
	}

	LODModel.RequiredBones.Shrink();	
}

#if WITH_APEX_CLOTHING

void USkeletalMesh::UpgradeOldClothingAssets()
{
	// Can only do an old-> new clothing asset upgrade in the editor.
	// And only if APEX clothing is available to upgrade from
	if (ClothingAssets_DEPRECATED.Num() > 0)
	{
		const bool bCallPostEditChange = false;
		const bool bReregisterComponents = false;
		FScopedSkeletalMeshPostEditChange ScopedSkeletalMeshPostEditChange(this, bCallPostEditChange, bReregisterComponents);
		// Upgrade the old deprecated clothing assets in to new clothing assets
		TMap<int32, TArray<int32>> OldLodMappings; // Map asset index to multiple lod indices
		TMap<int32, TArray<int32>> OldSectionMappings; // Map asset index to a section per LOD
		for (int32 AssetIdx = 0; AssetIdx < ClothingAssets_DEPRECATED.Num(); ++AssetIdx)
		{
			FClothingAssetData_Legacy& OldAssetData = ClothingAssets_DEPRECATED[AssetIdx];

			OldLodMappings.Add(AssetIdx);
			OldSectionMappings.Add(AssetIdx);

			if (ImportedModel.IsValid())
			{
				int32 FoundLod = INDEX_NONE;
				int32 FoundSection = INDEX_NONE;
				for (int32 LodIdx = 0; LodIdx < ImportedModel->LODModels.Num(); ++LodIdx)
				{
					FSkeletalMeshLODModel& LodModel = ImportedModel->LODModels[LodIdx];

					for (int32 SecIdx = 0; SecIdx < LodModel.Sections.Num(); ++SecIdx)
					{
						FSkelMeshSection& Section = LodModel.Sections[SecIdx];

						if (Section.CorrespondClothSectionIndex_DEPRECATED != INDEX_NONE && Section.bLegacyClothingSection_DEPRECATED)
						{
							FSkelMeshSection& ClothSection = LodModel.Sections[Section.CorrespondClothSectionIndex_DEPRECATED];

							if (ClothSection.CorrespondClothAssetIndex == AssetIdx)
							{
								FoundSection = SecIdx;
								break;
							}
						}
					}

					if (FoundSection != INDEX_NONE)
					{
						OldLodMappings[AssetIdx].Add(LodIdx);
						OldSectionMappings[AssetIdx].Add(FoundSection);

						// Reset for next LOD
						FoundSection = INDEX_NONE;
					}
				}
			}

			FClothingSystemEditorInterfaceModule& ClothingEditorModule = FModuleManager::Get().LoadModuleChecked<FClothingSystemEditorInterfaceModule>(TEXT("ClothingSystemEditorInterface"));
			UClothingAssetFactoryBase* Factory = ClothingEditorModule.GetClothingAssetFactory();
			if (Factory)
			{
				UClothingAssetBase* NewAsset = Factory->CreateFromApexAsset(OldAssetData.ApexClothingAsset, this, *FPaths::GetBaseFilename(OldAssetData.ApexFileName));
				check(NewAsset);

				// Pull the path across so reimports work as expected
				NewAsset->ImportedFilePath = OldAssetData.ApexFileName;

				GetMeshClothingAssets().Add(NewAsset);
			}
		}

		// Go back over the old assets and remove them from the skeletal mesh so the indices are preserved while
		// calculating the LOD and section mappings above.
		for (int32 AssetIdx = ClothingAssets_DEPRECATED.Num() - 1; AssetIdx >= 0; --AssetIdx)
		{
			ApexClothingUtils::RemoveAssetFromSkeletalMesh(this, AssetIdx, false);
		}

		check(OldLodMappings.Num() == OldSectionMappings.Num());

		for (int32 NewAssetIdx = 0; NewAssetIdx < GetMeshClothingAssets().Num(); ++NewAssetIdx)
		{
			UClothingAssetBase* CurrAsset = GetMeshClothingAssets()[NewAssetIdx];

			for (int32 MappedLodIdx = 0; MappedLodIdx < OldLodMappings[NewAssetIdx].Num(); ++MappedLodIdx)
			{
				const int32 MappedLod = OldLodMappings[NewAssetIdx][MappedLodIdx];
				const int32 MappedSection = OldSectionMappings[NewAssetIdx][MappedLodIdx];

				// Previously Clothing LODs were required to match skeletal mesh LODs, which is why we pass
				// MappedLod for both the mesh and clothing LODs here when doing an upgrade to the new
				// system. This restriction is now lifted and any mapping can be selected in Persona
				CurrAsset->BindToSkeletalMesh(this, MappedLod, MappedSection, MappedLod);
			}
		}

		UE_LOG(LogSkeletalMesh, Warning, TEXT("Legacy clothing asset '%s' was upgraded - please resave this asset."), *GetName());
	}
}

#endif // WITH_APEX_CLOTHING

void USkeletalMesh::RemoveLegacyClothingSections()
{
	// Remove duplicate skeletal mesh sections previously used for clothing simulation
	if(GetLinkerCustomVersion(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::RemoveDuplicatedClothingSections)
	{
		if(FSkeletalMeshModel* Model = GetImportedModel())
		{
			for(FSkeletalMeshLODModel& LodModel : Model->LODModels)
			{
				int32 ClothingSectionCount = 0;
				uint32 BaseVertex = MAX_uint32;
				int32 VertexCount = 0;
				uint32 BaseIndex = MAX_uint32;
				int32 IndexCount = 0;

				for(int32 SectionIndex = 0; SectionIndex < LodModel.Sections.Num(); ++SectionIndex)
				{
					FSkelMeshSection& Section = LodModel.Sections[SectionIndex];

					// If the section is disabled, it could be a clothing section
					if(Section.bLegacyClothingSection_DEPRECATED && Section.CorrespondClothSectionIndex_DEPRECATED != INDEX_NONE)
					{
						FSkelMeshSection& DuplicatedSection = LodModel.Sections[Section.CorrespondClothSectionIndex_DEPRECATED];

						// Cache the base index for the first clothing section (will be in correct order)
						if(ClothingSectionCount == 0)
						{
							PreEditChange(nullptr);
						}
						
						BaseVertex = FMath::Min(DuplicatedSection.BaseVertexIndex, BaseVertex);
						BaseIndex = FMath::Min(DuplicatedSection.BaseIndex, BaseIndex);

						VertexCount += DuplicatedSection.SoftVertices.Num();
						IndexCount += DuplicatedSection.NumTriangles * 3;

						// Mapping data for clothing could be built either on the source or the
						// duplicated section and has changed a few times, so check here for
						// where to get our data from
						if(DuplicatedSection.ClothMappingData.Num() > 0)
						{
							Section.ClothingData = DuplicatedSection.ClothingData;
							Section.ClothMappingData = DuplicatedSection.ClothMappingData;
						}

						Section.CorrespondClothAssetIndex = GetMeshClothingAssets().IndexOfByPredicate([&Section](const UClothingAssetBase* CurrAsset)
						{
							return CurrAsset && CurrAsset->GetAssetGuid() == Section.ClothingData.AssetGuid;
						});

						Section.BoneMap = DuplicatedSection.BoneMap;
						Section.bLegacyClothingSection_DEPRECATED = false;

						// Remove the reference index
						Section.CorrespondClothSectionIndex_DEPRECATED = INDEX_NONE;

						ClothingSectionCount++;
					}
					else
					{
						Section.CorrespondClothAssetIndex = INDEX_NONE;
						Section.ClothingData.AssetGuid = FGuid();
						Section.ClothingData.AssetLodIndex = INDEX_NONE;
						Section.ClothMappingData.Empty();
					}
				}

				if(BaseVertex != MAX_uint32 && BaseIndex != MAX_uint32)
				{
					// Remove from section list
					LodModel.Sections.RemoveAt(LodModel.Sections.Num() - ClothingSectionCount, ClothingSectionCount);

					// Clean up actual geometry
					LodModel.IndexBuffer.RemoveAt(BaseIndex, IndexCount);
					LodModel.NumVertices -= VertexCount;

					// Clean up index entries above the base we removed.
					// Ideally this shouldn't be unnecessary as clothing was at the end of the buffer
					// but this will always be safe to run to make sure adjacency generates correctly.
					for(uint32& Index : LodModel.IndexBuffer)
					{
						if(Index >= BaseVertex)
						{
							Index -= VertexCount;
						}
					}
				}
			}
		}
	}
}

USkeletalMeshEditorData& USkeletalMesh::GetMeshEditorData() const
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (!IsMeshEditorDataValid())
	{
			//The asset is created in the skeletalmesh package. We keep it private so the user cannot see it in the content browser
		//RF_Transactional make sure the asset can be transactional if we want to edit it
		USkeletalMesh* NonConstSkeletalMesh = const_cast<USkeletalMesh*>(this);
		MeshEditorDataObject = NewObject<USkeletalMeshEditorData>(NonConstSkeletalMesh, NAME_None, RF_Transactional);
	}
	//Make sure we have a valid pointer
	check(MeshEditorDataObject != nullptr);
	return *MeshEditorDataObject;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void USkeletalMesh::LoadLODImportedData(const int32 LODIndex, FSkeletalMeshImportData& OutMesh) const
{
	GetMeshEditorData().GetLODImportedData(LODIndex).LoadRawMesh(OutMesh);
}

void USkeletalMesh::SaveLODImportedData(const int32 LODIndex, FSkeletalMeshImportData& InMesh)
{
	FRawSkeletalMeshBulkData& RawSkeletalMeshBulkData = GetMeshEditorData().GetLODImportedData(LODIndex);
	RawSkeletalMeshBulkData.SaveRawMesh(InMesh);
	//Update the cache
	check(ImportedModel->LODModels.IsValidIndex(LODIndex));
	ImportedModel->LODModels[LODIndex].RawSkeletalMeshBulkDataID = RawSkeletalMeshBulkData.GetIdString();
	ImportedModel->LODModels[LODIndex].bIsBuildDataAvailable = RawSkeletalMeshBulkData.IsBuildDataAvailable();
	ImportedModel->LODModels[LODIndex].bIsRawSkeletalMeshBulkDataEmpty = RawSkeletalMeshBulkData.IsEmpty();
}

bool USkeletalMesh::IsLODImportedDataBuildAvailable(const int32 LODIndex) const
{
	if (!ImportedModel->LODModels.IsValidIndex(LODIndex))
	{
		return false;
	}
	return ImportedModel->LODModels[LODIndex].bIsBuildDataAvailable;
}

bool USkeletalMesh::IsLODImportedDataEmpty(const int32 LODIndex) const
{
	if (!ImportedModel->LODModels.IsValidIndex(LODIndex))
	{
		return false;
	}
	return ImportedModel->LODModels[LODIndex].bIsRawSkeletalMeshBulkDataEmpty;
}

void USkeletalMesh::GetLODImportedDataVersions(const int32 LODIndex, ESkeletalMeshGeoImportVersions& OutGeoImportVersion, ESkeletalMeshSkinningImportVersions& OutSkinningImportVersion) const
{
	const FRawSkeletalMeshBulkData& RawSkeletalMeshBulkData = GetMeshEditorData().GetLODImportedData(LODIndex);
	OutGeoImportVersion = RawSkeletalMeshBulkData.GeoImportVersion;
	OutSkinningImportVersion = RawSkeletalMeshBulkData.SkinningImportVersion;
}

void USkeletalMesh::SetLODImportedDataVersions(const int32 LODIndex, const ESkeletalMeshGeoImportVersions& InGeoImportVersion, const ESkeletalMeshSkinningImportVersions& InSkinningImportVersion)
{
	FRawSkeletalMeshBulkData& RawSkeletalMeshBulkData = GetMeshEditorData().GetLODImportedData(LODIndex);
	RawSkeletalMeshBulkData.GeoImportVersion = InGeoImportVersion;
	RawSkeletalMeshBulkData.SkinningImportVersion = InSkinningImportVersion;
	//Update the cache
	check(ImportedModel->LODModels.IsValidIndex(LODIndex));
	ImportedModel->LODModels[LODIndex].RawSkeletalMeshBulkDataID = RawSkeletalMeshBulkData.GetIdString();
	ImportedModel->LODModels[LODIndex].bIsBuildDataAvailable = RawSkeletalMeshBulkData.IsBuildDataAvailable();
	ImportedModel->LODModels[LODIndex].bIsRawSkeletalMeshBulkDataEmpty = RawSkeletalMeshBulkData.IsEmpty();
}

void USkeletalMesh::CopyImportedData(int32 SrcLODIndex, USkeletalMesh* SrcSkeletalMesh, int32 DestLODIndex, USkeletalMesh* DestSkeletalMesh)
{
	check(DestSkeletalMesh->ImportedModel->LODModels.IsValidIndex(DestLODIndex));
	FRawSkeletalMeshBulkData& SrcRawMesh = SrcSkeletalMesh->GetMeshEditorData().GetLODImportedData(SrcLODIndex);
	FRawSkeletalMeshBulkData& DestRawMesh = DestSkeletalMesh->GetMeshEditorData().GetLODImportedData(DestLODIndex);
	FSkeletalMeshImportData SrcImportData;
	SrcRawMesh.LoadRawMesh(SrcImportData);
	DestRawMesh.SaveRawMesh(SrcImportData);
	DestRawMesh.GeoImportVersion = SrcRawMesh.GeoImportVersion;
	DestRawMesh.SkinningImportVersion = SrcRawMesh.SkinningImportVersion;
	
	FSkeletalMeshLODModel& DestLODModel = DestSkeletalMesh->ImportedModel->LODModels[DestLODIndex];
	DestLODModel.RawSkeletalMeshBulkDataID = DestRawMesh.GetIdString();
	DestLODModel.bIsBuildDataAvailable = DestRawMesh.IsBuildDataAvailable();
	DestLODModel.bIsRawSkeletalMeshBulkDataEmpty = DestRawMesh.IsEmpty();
}

void USkeletalMesh::ReserveLODImportData(int32 MaxLODIndex)
{
	//Getting the LODImportedData will allocate the data to default value.
	GetMeshEditorData().GetLODImportedData(MaxLODIndex);
}

void USkeletalMesh::ForceBulkDataResident(const int32 LODIndex)
{
	GetMeshEditorData().GetLODImportedData(LODIndex).GetBulkData().ForceBulkDataResident();
}

void USkeletalMesh::EmptyLODImportData(const int32 LODIndex)
{
	if(!ImportedModel->LODModels.IsValidIndex(LODIndex) || !GetMeshEditorData().IsLODImportDataValid(LODIndex))
	{
		return;
	}

	FRawSkeletalMeshBulkData& RawMesh = GetMeshEditorData().GetLODImportedData(LODIndex);
	FSkeletalMeshImportData EmptyData;
	RawMesh.SaveRawMesh(EmptyData);
	RawMesh.GeoImportVersion = ESkeletalMeshGeoImportVersions::Before_Versionning;
	RawMesh.SkinningImportVersion = ESkeletalMeshSkinningImportVersions::Before_Versionning;
	ImportedModel->LODModels[LODIndex].RawSkeletalMeshBulkDataID = RawMesh.GetIdString();
	ImportedModel->LODModels[LODIndex].bIsBuildDataAvailable = RawMesh.IsBuildDataAvailable();
	ImportedModel->LODModels[LODIndex].bIsRawSkeletalMeshBulkDataEmpty = RawMesh.IsEmpty();
}

void USkeletalMesh::EmptyAllImportData()
{
	const int32 LODNumber = GetLODNum();
	for(int32 LODIndex = 0; LODIndex < LODNumber; ++LODIndex)
	{
		EmptyLODImportData(LODIndex);
	}
}

void USkeletalMesh::CreateUserSectionsDataForLegacyAssets()
{
	//We want to avoid changing the ddc if we load an old asset.
	//This bool should be put to false at the end of the postload, if there is another posteditchange call after a new ddc will be created
	SetUseLegacyMeshDerivedDataKey(true);
	//Fill up the Section ChunkedParentSectionIndex and OriginalDataSectionIndex
	//We also want to create the UserSectionsData structure so the user can change the section data
	for (int32 LodIndex = 0; LodIndex < LODInfo.Num(); LodIndex++)
	{
		FSkeletalMeshLODModel& ThisLODModel = ImportedModel->LODModels[LodIndex];
		FSkeletalMeshLODInfo* ThisLODInfo = GetLODInfo(LodIndex);
		check(ThisLODInfo);

		//Reset the reduction setting to a non active state if the asset has active reduction but have no RawSkeletalMeshBulkData (we cannot reduce it)
		const bool bIsLODReductionActive = IsReductionActive(LodIndex);


		bool bMustUseReductionSourceData = bIsLODReductionActive
			&& ThisLODInfo->bHasBeenSimplified
			&& ImportedModel->OriginalReductionSourceMeshData.IsValidIndex(LodIndex)
			&& !(ImportedModel->OriginalReductionSourceMeshData[LodIndex]->IsEmpty());

		if (bIsLODReductionActive && !ThisLODInfo->bHasBeenSimplified && IsLODImportedDataEmpty(LodIndex))
		{
			if (LodIndex > ThisLODInfo->ReductionSettings.BaseLOD)
			{
				ThisLODInfo->bHasBeenSimplified = true;
			}
			else if (LodIndex == ThisLODInfo->ReductionSettings.BaseLOD)
			{
				if (ThisLODInfo->ReductionSettings.TerminationCriterion == SkeletalMeshTerminationCriterion::SMTC_AbsNumOfTriangles
					|| ThisLODInfo->ReductionSettings.TerminationCriterion == SkeletalMeshTerminationCriterion::SMTC_AbsNumOfVerts
					|| ThisLODInfo->ReductionSettings.TerminationCriterion == SkeletalMeshTerminationCriterion::SMTC_AbsTriangleOrVert)
				{
					//MaxNum.... cannot be inactive, switch to NumOfTriangle
					ThisLODInfo->ReductionSettings.TerminationCriterion = SMTC_NumOfTriangles;
				}

				//Now that we use triangle or vert num, set an inactive value
				if (ThisLODInfo->ReductionSettings.TerminationCriterion == SkeletalMeshTerminationCriterion::SMTC_NumOfTriangles
					|| ThisLODInfo->ReductionSettings.TerminationCriterion == SkeletalMeshTerminationCriterion::SMTC_TriangleOrVert)
				{
					ThisLODInfo->ReductionSettings.NumOfTrianglesPercentage = 1.0f;
				}
				if (ThisLODInfo->ReductionSettings.TerminationCriterion == SkeletalMeshTerminationCriterion::SMTC_NumOfVerts
					|| ThisLODInfo->ReductionSettings.TerminationCriterion == SkeletalMeshTerminationCriterion::SMTC_TriangleOrVert)
				{
					ThisLODInfo->ReductionSettings.NumOfVertPercentage = 1.0f;
				}
			}
			bMustUseReductionSourceData = false;
		}
		ThisLODModel.UpdateChunkedSectionInfo(GetName());

		if (bMustUseReductionSourceData)
		{
			//We must load the reduction source model, since reduction can remove section
			FSkeletalMeshLODModel ReductionSrcLODModel;
			TMap<FString, TArray<FMorphTargetDelta>> TmpMorphTargetData;
			ImportedModel->OriginalReductionSourceMeshData[LodIndex]->LoadReductionData(ReductionSrcLODModel, TmpMorphTargetData, this);
			
			//Fill the user data with the original value
			TMap<int32, FSkelMeshSourceSectionUserData> BackupUserSectionsData = ThisLODModel.UserSectionsData;
			ThisLODModel.UserSectionsData.Empty();

			ThisLODModel.UserSectionsData = ReductionSrcLODModel.UserSectionsData;

			//Now restore the reduce section user change and adjust the originalDataSectionIndex to point on the correct UserSectionData
			TBitArray<> SourceSectionMatched;
			SourceSectionMatched.Init(false, ReductionSrcLODModel.Sections.Num());
			for (int32 SectionIndex = 0; SectionIndex < ThisLODModel.Sections.Num(); ++SectionIndex)
			{
				FSkelMeshSection& Section = ThisLODModel.Sections[SectionIndex];
				FSkelMeshSourceSectionUserData& BackupUserData = FSkelMeshSourceSectionUserData::GetSourceSectionUserData(BackupUserSectionsData, Section);
				for (int32 SourceSectionIndex = 0; SourceSectionIndex < ReductionSrcLODModel.Sections.Num(); ++SourceSectionIndex)
				{
					if (SourceSectionMatched[SourceSectionIndex])
					{
						continue;
					}
					FSkelMeshSection& SourceSection = ReductionSrcLODModel.Sections[SourceSectionIndex];
					FSkelMeshSourceSectionUserData& UserData = FSkelMeshSourceSectionUserData::GetSourceSectionUserData(ThisLODModel.UserSectionsData, SourceSection);
					if (Section.MaterialIndex == SourceSection.MaterialIndex)
					{
						Section.OriginalDataSectionIndex = SourceSection.OriginalDataSectionIndex;
						UserData = BackupUserData;
						SourceSectionMatched[SourceSectionIndex] = true;
						break;
					}
				}
			}
			ThisLODModel.SyncronizeUserSectionsDataArray();
		}
	}
}

void USkeletalMesh::PostLoadValidateUserSectionData()
{
	for (int32 LodIndex = 0; LodIndex < GetLODNum(); LodIndex++)
	{
		const FSkeletalMeshLODInfo* LODInfoPtr = GetLODInfo(LodIndex);
		if (!LODInfoPtr || !LODInfoPtr->bHasBeenSimplified)
		{
			//We validate only generated LOD from a base LOD
			continue;
		}

		FSkeletalMeshLODModel& ThisLODModel = ImportedModel->LODModels[LodIndex];
		const int32 SectionNum = ThisLODModel.Sections.Num();
		//See if more then one section use the same UserSectionData
		bool bLODHaveSectionIssue = false;
		TBitArray<> AvailableUserSectionData;
		AvailableUserSectionData.Init(true, ThisLODModel.UserSectionsData.Num());
		for (int32 SectionIndex = 0; SectionIndex < SectionNum; ++SectionIndex)
		{
			FSkelMeshSection& Section = ThisLODModel.Sections[SectionIndex];
			if (Section.ChunkedParentSectionIndex != INDEX_NONE)
			{
				continue;
			}
			if(!AvailableUserSectionData.IsValidIndex(Section.OriginalDataSectionIndex) || !AvailableUserSectionData[Section.OriginalDataSectionIndex])
			{
				bLODHaveSectionIssue = true;
				break;
			}
			AvailableUserSectionData[Section.OriginalDataSectionIndex] = false;
		}
		if(!bLODHaveSectionIssue)
		{
			//Everything is good nothing to fix
			continue;
		}

		//Force the source UserSectionData, then restore the UserSectionData value each section was using
		//We use the source section user data entry in case we do not have any override
		const FSkeletalMeshLODModel& BaseLODModel = ImportedModel->LODModels[LODInfoPtr->ReductionSettings.BaseLOD];
		TMap<int32, FSkelMeshSourceSectionUserData> NewUserSectionsData;

		int32 CurrentOriginalSectionIndex = 0;
		for (int32 SectionIndex = 0; SectionIndex < SectionNum; ++SectionIndex)
		{
			FSkelMeshSection& Section = ThisLODModel.Sections[SectionIndex];
			if (Section.ChunkedParentSectionIndex != INDEX_NONE)
			{
				//We do not restore user section data for chunked section, the parent has already fix it
				Section.OriginalDataSectionIndex = CurrentOriginalSectionIndex;
				continue;
			}

			FSkelMeshSourceSectionUserData& SectionUserData = NewUserSectionsData.FindOrAdd(CurrentOriginalSectionIndex);
			if(const FSkelMeshSourceSectionUserData* BackupSectionUserData = ThisLODModel.UserSectionsData.Find(Section.OriginalDataSectionIndex))
			{
				SectionUserData = *BackupSectionUserData;
			}
			else if(const FSkelMeshSourceSectionUserData* BaseSectionUserData = BaseLODModel.UserSectionsData.Find(CurrentOriginalSectionIndex))
			{
				SectionUserData = *BaseSectionUserData;
			}

			Section.OriginalDataSectionIndex = CurrentOriginalSectionIndex;
			//Parent (non chunked) section must increment the index
			CurrentOriginalSectionIndex++;
		}
		ThisLODModel.UserSectionsData = NewUserSectionsData;

		UE_ASSET_LOG(LogSkeletalMesh, Display, this, TEXT("Fix some section data of this asset for lod %d. Verify all sections of this mesh are ok and save the asset to fix this issue."), LodIndex);
	}
}

void USkeletalMesh::PostLoadEnsureImportDataExist()
{
	//If we have a LODModel with no import data and the LOD model have at least one section using more bone then any platform max GPU bone count. We will recreate the import data to allow the asset to be build and chunk properly.
	const int32 MinimumPerPlatformMaxGPUSkinBones = FGPUBaseSkinVertexFactory::GetMinimumPerPlatformMaxGPUSkinBonesValue();
	bool bNeedToCreateImportData = false;
	for (int32 LodIndex = 0; LodIndex < GetLODNum(); LodIndex++)
	{
		const FSkeletalMeshLODModel* LODModel = &(GetImportedModel()->LODModels[LodIndex]);
		const FSkeletalMeshLODInfo* ThisLODInfo = GetLODInfo(LodIndex);
		check(ThisLODInfo);
		const bool bRawDataEmpty = IsLODImportedDataEmpty(LodIndex);
		const bool bRawBuildDataAvailable = IsLODImportedDataBuildAvailable(LodIndex);
		if (!bRawDataEmpty && bRawBuildDataAvailable)
		{
			continue;
		}
		const bool bReductionActive = IsReductionActive(LodIndex);
		const bool bInlineReduction = bReductionActive && (ThisLODInfo->ReductionSettings.BaseLOD == LodIndex);
		if (bReductionActive && !bInlineReduction)
		{
			//Generated LOD (not inline) do not need imported data
			continue;
		}
		//See if the LODModel data use more bones then the chunking allow
		int32 MaxBoneperSection = 0;
		for(const FSkelMeshSection& Section : LODModel->Sections)
		{
			MaxBoneperSection = FMath::Max(MaxBoneperSection, Section.BoneMap.Num());
		}
		//If we use more bone then the minimum maxGPUSkinbone, we need to re-create de import data to be able to build the asset
		if (MaxBoneperSection > MinimumPerPlatformMaxGPUSkinBones)
		{
			bNeedToCreateImportData = true;
			break;
		}
	}
	if (bNeedToCreateImportData)
	{
		IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");
		//We create the import data for all LOD that do not have import data except for the generated LODs.
		MeshUtilities.CreateImportDataFromLODModel(this);
#if WITH_EDITORONLY_DATA
		//If the import data is existing we want to turn use legacy derive data key to false
		SetUseLegacyMeshDerivedDataKey(false);
#endif
	}
}

void USkeletalMesh::PostLoadVerifyAndFixBadTangent()
{
	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");
	bool bFoundBadTangents = false;
	for (int32 LodIndex = 0; LodIndex < GetLODNum(); LodIndex++)
	{
		if (!IsLODImportedDataEmpty(LodIndex))
		{
			//No need to verify skeletalmesh that have valid imported data, the tangents will always exist in this case
			continue;
		}
		const FSkeletalMeshLODInfo* LODInfoPtr = GetLODInfo(LodIndex);
		if (!LODInfoPtr || LODInfoPtr->bHasBeenSimplified)
		{
			//No need to validate simplified LOD
			continue;
		}

		auto ComputeTriangleTangent = [&MeshUtilities](const FSoftSkinVertex& VertexA, const FSoftSkinVertex& VertexB, const FSoftSkinVertex& VertexC, TArray<FVector>& OutTangents)
		{
			MeshUtilities.CalculateTriangleTangent(VertexA, VertexB, VertexC, OutTangents, FLT_MIN);
		};

		FSkeletalMeshLODModel& ThisLODModel = ImportedModel->LODModels[LodIndex];
		const int32 SectionNum = ThisLODModel.Sections.Num();
		TArray<FSoftSkinVertex> Vertices;
		TMap<int32, TArray<FVector>> TriangleTangents;

		for (int32 SectionIndex = 0; SectionIndex < SectionNum; ++SectionIndex)
		{
			FSkelMeshSection& Section = ThisLODModel.Sections[SectionIndex];
			const int32 NumVertices = Section.GetNumVertices();
			const int32 SectionBaseIndex = Section.BaseIndex;
			const int32 SectionNumTriangles = Section.NumTriangles;
			TArray<uint32>& IndexBuffer = ThisLODModel.IndexBuffer;
			//We inspect triangle per section so we need to reset the array when we start a new section.
			TriangleTangents.Empty(SectionNumTriangles);
			for (int32 FaceIndex = 0; FaceIndex < SectionNumTriangles; ++FaceIndex)
			{
				int32 BaseFaceIndexBufferIndex = SectionBaseIndex + (FaceIndex * 3);
				if (!ensure(IndexBuffer.IsValidIndex(BaseFaceIndexBufferIndex)) || !ensure(IndexBuffer.IsValidIndex(BaseFaceIndexBufferIndex + 2)))
				{
					break;
				}
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const int32 CornerIndexBufferIndex = BaseFaceIndexBufferIndex + Corner;
					ensure(IndexBuffer.IsValidIndex(CornerIndexBufferIndex));
					int32 VertexIndex = IndexBuffer[CornerIndexBufferIndex] - Section.BaseVertexIndex;
					ensure(Section.SoftVertices.IsValidIndex(VertexIndex));
					FSoftSkinVertex& SoftSkinVertex = Section.SoftVertices[VertexIndex];

					bool bNeedToOrthonormalize = false;

					//Make sure we have normalized tangents
					auto NormalizedTangent = [&bNeedToOrthonormalize, &bFoundBadTangents](FVector& Tangent)
					{
						if (Tangent.ContainsNaN() || Tangent.SizeSquared() < THRESH_VECTOR_NORMALIZED)
						{
							//This is a degenerated tangent, we will set it to zero. It will be fix by the
							//FixTangent lambda function.
							Tangent = FVector::ZeroVector;
							//If we can fix this tangents, we have to orthonormalize the result
							bNeedToOrthonormalize = true;
							bFoundBadTangents = true;
							return false;
						}
						else if (!Tangent.IsNormalized())
						{
							//This is not consider has a bad normal since the tangent vector is not near zero.
							//We are just making sure the tangent is normalize.
							Tangent.Normalize();
						}
						return true;
					};

					/** Call this lambda only if you need to fix the tangent */
					auto FixTangent = [&IndexBuffer, &Section, &TriangleTangents, &ComputeTriangleTangent, &BaseFaceIndexBufferIndex](FVector& TangentA, const FVector& TangentB, const FVector& TangentC, const int32 Offset)
					{
						//If the two other axis are valid, fix the tangent with a cross product and normalize the answer.
						if (TangentB.IsNormalized() && TangentC.IsNormalized())
						{
							TangentA = FVector::CrossProduct(TangentB, TangentC);
							TangentA.Normalize();
							return true;
						}

						//We do not have any valid data to help us for fixing this normal so apply the triangle normals, this will create a faceted mesh but this is better then a black not shade mesh.
						TArray<FVector>& Tangents = TriangleTangents.FindOrAdd(BaseFaceIndexBufferIndex);
						if (Tangents.Num() == 0)
						{
							const int32 VertexIndex0 = IndexBuffer[BaseFaceIndexBufferIndex] - Section.BaseVertexIndex;
							const int32 VertexIndex1 = IndexBuffer[BaseFaceIndexBufferIndex + 1] - Section.BaseVertexIndex;
							const int32 VertexIndex2 = IndexBuffer[BaseFaceIndexBufferIndex + 2] - Section.BaseVertexIndex;
							if (!ensure(
								Section.SoftVertices.IsValidIndex(VertexIndex0) &&
								Section.SoftVertices.IsValidIndex(VertexIndex1) &&
								Section.SoftVertices.IsValidIndex(VertexIndex2) ) )
							{
								//We found bad vertex indices, we cannot compute this face tangents.
								return false;
							}
							ComputeTriangleTangent(Section.SoftVertices[VertexIndex0], Section.SoftVertices[VertexIndex1], Section.SoftVertices[VertexIndex2], Tangents);
							const FVector Axis[3] = { {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f} };
							if (!ensure(Tangents.Num() == 3))
							{
								Tangents.Empty(3);
								Tangents.AddZeroed(3);
							}
							for (int32 TangentIndex = 0; TangentIndex < Tangents.Num(); ++TangentIndex)
							{
								if (Tangents[TangentIndex].IsNearlyZero())
								{
									Tangents[TangentIndex] = Axis[TangentIndex];
								}
							}
							if (!ensure(Tangents.Num() == 3))
							{
								//We are not able to compute the triangle tangent, this is probably a degenerated triangle
								Tangents.Empty(3);

								Tangents.Add(Axis[0]);
								Tangents.Add(Axis[1]);
								Tangents.Add(Axis[2]);
							}
						}
						//Use the offset to know which tangent type we are setting (0: Tangent X, 1: bi-normal Y, 2: Normal Z)
						TangentA = Tangents[(Offset) % 3];
						return TangentA.IsNormalized();
					};

					//The SoftSkinVertex TangentZ is a FVector4 so we must use a temporary FVector to be able to pass reference
					FVector TangentZ = SoftSkinVertex.TangentZ;
					//Make sure the tangent space is normalize before fixing bad tangent, because we want to do a cross product
					//of 2 valid axis if possible. If not possible we will use the triangle normal which give a faceted triangle.
					bool ValidTangentX = NormalizedTangent(SoftSkinVertex.TangentX);
					bool ValidTangentY = NormalizedTangent(SoftSkinVertex.TangentY);
					bool ValidTangentZ = NormalizedTangent(TangentZ);

					if (!ValidTangentX)
					{
						ValidTangentX = FixTangent(SoftSkinVertex.TangentX, SoftSkinVertex.TangentY, TangentZ, 0);
					}
					if (!ValidTangentY)
					{
						ValidTangentY = FixTangent(SoftSkinVertex.TangentY, TangentZ, SoftSkinVertex.TangentX, 1);
					}
					if (!ValidTangentZ)
					{
						ValidTangentZ = FixTangent(TangentZ, SoftSkinVertex.TangentX, SoftSkinVertex.TangentY, 2);
					}

					//Make sure the result tangent space is orthonormal, only if we succeed to fix all tangents
					if (bNeedToOrthonormalize && ValidTangentX && ValidTangentY && ValidTangentZ)
					{
						FVector::CreateOrthonormalBasis(
							SoftSkinVertex.TangentX,
							SoftSkinVertex.TangentY,
							TangentZ
						);
					}
					SoftSkinVertex.TangentZ = TangentZ;
				}
			}
		}
	}
	if (bFoundBadTangents)
	{
		//Notify the user that we have to fix the normals on this model.
		UE_ASSET_LOG(LogSkeletalMesh, Display, this, TEXT("Find and fix some bad tangent! please re-import this skeletal mesh asset to fix the issue. The shading of the skeletal mesh will be bad and faceted."));
	}
}

#endif // WITH_EDITOR

bool USkeletalMesh::IsPostLoadThreadSafe() const
{
	return false;	// PostLoad is not thread safe because of the call to InitMorphTargets, which can call VerifySmartName() that can mutate a shared map in the skeleton.
}

void USkeletalMesh::PostLoad()
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);
	Super::PostLoad();

	// Make sure the cloth assets have finished loading
	// TODO: Remove all UObject PostLoad dependencies.
	//       Even with these ConditionalPostLoad calls, the UObject PostLoads' order of execution cannot be guaranted.
	//       E.g. in some instance it has been found that the SkeletalMesh EndLoad can trigger a ConditionalPostLoad
	//       on the cloth assets even before reaching this point.
	//       In these occurences, the cloth asset's RF_NeedsPostLoad flag is already cleared despite its PostLoad still
	//       being un-executed, making the following block code ineffective.
	for (UClothingAssetBase* MeshClothingAsset : GetMeshClothingAssets())
	{
		MeshClothingAsset->ConditionalPostLoad();
	}

#if WITH_EDITOR

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Make sure the mesh editor data object is a sub object of the skeletalmesh, rename it to change the owner to be the skeletalmesh.
	if (MeshEditorDataObject && MeshEditorDataObject->GetOuter() != this)
	{
		//Post load call so no need to: dirty, redirect, transact or reset the loader.
		const TCHAR* NewName = nullptr;
		MeshEditorDataObject->Rename(NewName, this, REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
		MeshEditorDataObject->SetFlags(RF_Transactional);
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (!GetOutermost()->bIsCookedForEditor)
	{
		// If LODInfo is missing - create array of correct size.
		if (LODInfo.Num() != ImportedModel->LODModels.Num())
		{
			LODInfo.Empty(ImportedModel->LODModels.Num());
			LODInfo.AddZeroed(ImportedModel->LODModels.Num());

			for (int32 i = 0; i < LODInfo.Num(); i++)
			{
				LODInfo[i].LODHysteresis = 0.02f;
			}
		}

		int32 TotalLODNum = LODInfo.Num();
		for (int32 LodIndex = 0; LodIndex < TotalLODNum; LodIndex++)
		{
			FSkeletalMeshLODInfo& ThisLODInfo = LODInfo[LodIndex];
			FSkeletalMeshLODModel& ThisLODModel = ImportedModel->LODModels[LodIndex];

			if (ThisLODInfo.ReductionSettings.BonesToRemove_DEPRECATED.Num() > 0)
			{
				for (auto& BoneToRemove : ThisLODInfo.ReductionSettings.BonesToRemove_DEPRECATED)
				{
					AddBoneToReductionSetting(LodIndex, BoneToRemove.BoneName);
				}

				// since in previous system, we always removed from previous LOD, I'm adding this 
				// here for previous LODs
				for (int32 CurLodIndx = LodIndex + 1; CurLodIndx < TotalLODNum; ++CurLodIndx)
				{
					AddBoneToReductionSetting(CurLodIndx, ThisLODInfo.RemovedBones_DEPRECATED);
				}

				// we don't apply this change here, but this will be applied when you re-gen simplygon
				ThisLODInfo.ReductionSettings.BonesToRemove_DEPRECATED.Empty();
			}

			if (ThisLODInfo.ReductionSettings.BakePose_DEPRECATED != nullptr)
			{
				ThisLODInfo.BakePose = ThisLODInfo.ReductionSettings.BakePose_DEPRECATED;
				ThisLODInfo.ReductionSettings.BakePose_DEPRECATED = nullptr;
			}
		}

		// load LODinfo if using shared asset, it can override existing bone remove settings
		if (GetLODSettings() != nullptr)
		{
			//before we copy
			if (GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AddBakePoseOverrideForSkeletalMeshReductionSetting)
			{
				// if LODsetting doesn't have BakePose, but this does, we'll have to copy that to BakePoseOverride
				const int32 NumSettings = FMath::Min(GetLODSettings()->GetNumberOfSettings(), GetLODNum());
				for (int32 Index = 0; Index < NumSettings; ++Index)
				{
					const FSkeletalMeshLODGroupSettings& GroupSetting = GetLODSettings()->GetSettingsForLODLevel(Index);
					// if lod setting doesn't have bake pose, but this lod does, that means this bakepose has to move to BakePoseOverride
					// since we want to match what GroupSetting has
					if (GroupSetting.BakePose == nullptr && LODInfo[Index].BakePose)
					{
						// in this case,
						LODInfo[Index].BakePoseOverride = LODInfo[Index].BakePose;
						LODInfo[Index].BakePose = nullptr;
					}
				}
			}
			GetLODSettings()->SetLODSettingsToMesh(this);
		}

		if (GetLinkerUE4Version() < VER_UE4_SORT_ACTIVE_BONE_INDICES)
		{
			for (int32 LodIndex = 0; LodIndex < LODInfo.Num(); LodIndex++)
			{
				FSkeletalMeshLODModel & ThisLODModel = ImportedModel->LODModels[LodIndex];
				ThisLODModel.ActiveBoneIndices.Sort();
			}
		}

		// make sure older versions contain active bone indices with parents present
		// even if they're not skinned, missing matrix calculation will mess up skinned children
		if (GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::EnsureActiveBoneIndicesToContainParents)
		{
			for (int32 LodIndex = 0; LodIndex < LODInfo.Num(); LodIndex++)
			{
				FSkeletalMeshLODModel& ThisLODModel = ImportedModel->LODModels[LodIndex];
				GetRefSkeleton().EnsureParentsExistAndSort(ThisLODModel.ActiveBoneIndices);
			}
		}

#if WITH_APEX_CLOTHING
		UpgradeOldClothingAssets();
#endif // WITH_APEX_CLOTHING

		RemoveLegacyClothingSections();

		UpdateGenerateUpToData();

		if (GetLinkerCustomVersion(FEditorObjectVersion::GUID) < FEditorObjectVersion::SkeletalMeshMoveEditorSourceDataToPrivateAsset)
		{
			ReserveLODImportData(ImportedModel->LODModels.Num() - 1);
			for (int32 LODIndex = 0; LODIndex < ImportedModel->LODModels.Num(); ++LODIndex)
			{
				FSkeletalMeshLODModel& ThisLODModel = ImportedModel->LODModels[LODIndex];
				//We can have partial data if the asset was save after the split workflow implementation
				//Use the deprecated member to retrieve this data
				if (GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::NewSkeletalMeshImporterWorkflow)
				{
					if (!ThisLODModel.RawSkeletalMeshBulkData_DEPRECATED.IsEmpty())
					{
						FSkeletalMeshImportData SerializeMeshData;
						ThisLODModel.RawSkeletalMeshBulkData_DEPRECATED.LoadRawMesh(SerializeMeshData);
						SaveLODImportedData(LODIndex, SerializeMeshData);
					}
					//Get the FRawSkeletalMeshBulkData to set the geo and skinning version
					FRawSkeletalMeshBulkData& RawSkeletalMeshBulkData = GetMeshEditorData().GetLODImportedData(LODIndex);
					RawSkeletalMeshBulkData.GeoImportVersion = ThisLODModel.RawSkeletalMeshBulkData_DEPRECATED.GeoImportVersion;
					RawSkeletalMeshBulkData.SkinningImportVersion = ThisLODModel.RawSkeletalMeshBulkData_DEPRECATED.SkinningImportVersion;
					//Empty the DEPRECATED member
					FSkeletalMeshImportData EmptyMeshData;
					ThisLODModel.RawSkeletalMeshBulkData_DEPRECATED.SaveRawMesh(EmptyMeshData);
					ThisLODModel.RawSkeletalMeshBulkData_DEPRECATED.EmptyBulkData();
				}
				//Set the cache data into the LODModel
				FRawSkeletalMeshBulkData& RawSkeletalMeshBulkData = GetMeshEditorData().GetLODImportedData(LODIndex);
				ThisLODModel.bIsRawSkeletalMeshBulkDataEmpty = RawSkeletalMeshBulkData.IsEmpty();
				ThisLODModel.bIsBuildDataAvailable = RawSkeletalMeshBulkData.IsBuildDataAvailable();
				ThisLODModel.RawSkeletalMeshBulkDataID = RawSkeletalMeshBulkData.GetIdString();
			}
		}

		if (GetLinkerCustomVersion(FEditorObjectVersion::GUID) < FEditorObjectVersion::SkeletalMeshBuildRefactor)
		{
			CreateUserSectionsDataForLegacyAssets();
		}

		PostLoadValidateUserSectionData();

		PostLoadEnsureImportDataExist();

		PostLoadVerifyAndFixBadTangent();

		if (GetResourceForRendering() == nullptr)
		{
			CacheDerivedData();
		}

		//Make sure unused cloth are unbind
		if (GetMeshClothingAssets().Num() > 0)
		{
			TArray<UClothingAssetBase *> InUsedClothingAssets;
			GetClothingAssetsInUse(InUsedClothingAssets);
			//Look if we have some cloth binding to unbind
			for (UClothingAssetBase* MeshClothingAsset : GetMeshClothingAssets())
			{
				if (MeshClothingAsset == nullptr)
				{
					continue;
				}
				bool bFound = false;
				for (UClothingAssetBase* UsedMeshClothingAsset : InUsedClothingAssets)
				{
					if (UsedMeshClothingAsset->GetAssetGuid() == MeshClothingAsset->GetAssetGuid())
					{
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					//No post edit change and no reregister, we just prevent the inner scope to call postedit change and reregister
					FScopedSkeletalMeshPostEditChange ScopedPostEditChange(this, false, false);
					//Make sure the asset is unbind, some old code path was allowing to have bind cloth asset not present in the imported model.
					//The old inline reduction code was not rebinding the cloth asset nor unbind it.
					MeshClothingAsset->UnbindFromSkeletalMesh(this);
				}
			}
		}
	}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	if (GetLinkerCustomVersion(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::FixedMeshUVDensity)
	{
		UpdateUVChannelData(true);
	}
#endif // WITH_EDITOR

	// init morph targets. 
	// should do this before InitResource, so that we clear invalid morphtargets
	InitMorphTargets();

	// initialize rendering resources
	if (FApp::CanEverRender())
	{
		InitResources();
	}
	else
	{
		// Update any missing data when cooking.
		UpdateUVChannelData(false);
	}

	CalculateInvRefMatrices();

#if WITH_EDITORONLY_DATA
	if (GetRetargetBasePose().Num() == 0 && !GetOutermost()->bIsCookedForEditor)
	{
		GetRetargetBasePose() = GetRefSkeleton().GetRefBonePose();
	}

	if (GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::SupportVirtualBoneInRetargeting)
	{
		ReallocateRetargetBasePose();
	}
#endif

	// Bounds have been loaded - apply extensions.
	CalculateExtendedBounds();

#if WITH_EDITORONLY_DATA
	if (GetRequiresLODScreenSizeConversion() || GetRequiresLODHysteresisConversion())
	{
		// Convert screen area to screen size
		ConvertLegacyLODScreenSize();
	}
#endif

#if WITH_EDITOR
	// If inverse masses have never been cached, invalidate data so it will be recalculated
	if(GetLinkerCustomVersion(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::CachedClothInverseMasses)
	{
		for(UClothingAssetBase* ClothingAsset : GetMeshClothingAssets())
		{
			if(ClothingAsset) 
			{
				ClothingAsset->InvalidateCachedData();
			}
		}
	}
#endif

	SetHasActiveClothingAssets(ComputeActiveClothingAssets());

#if WITH_EDITOR
	if (GetLinkerCustomVersion(FNiagaraObjectVersion::GUID) < FNiagaraObjectVersion::SkeletalMeshVertexSampling)
	{
		SamplingInfo.BuildRegions(this);
		SamplingInfo.BuildWholeMesh(this);
	}

#endif

#if !WITH_EDITOR
	RebuildSocketMap();
#endif // !WITH_EDITOR
#if WITH_EDITORONLY_DATA
	//Next postedit change will use the new ddc key scheme
	SetUseLegacyMeshDerivedDataKey(false);
#endif
}

#if WITH_EDITORONLY_DATA

void USkeletalMesh::RebuildRefSkeletonNameToIndexMap()
{
	TArray<FBoneIndexType> DuplicateBones;
	// Make sure we have no duplicate bones. Some content got corrupted somehow. :(
	GetRefSkeleton().RemoveDuplicateBones(this, DuplicateBones);

	// If we have removed any duplicate bones, we need to fix up any broken LODs as well.
	// Duplicate bones are given from highest index to lowest.
	// so it's safe to decrease indices for children, we're not going to lose the index of the remaining duplicate bones.
	for (int32 Index = 0; Index < DuplicateBones.Num(); Index++)
	{
		const FBoneIndexType& DuplicateBoneIndex = DuplicateBones[Index];
		for (int32 LodIndex = 0; LodIndex < LODInfo.Num(); LodIndex++)
		{
			FSkeletalMeshLODModel& ThisLODModel = ImportedModel->LODModels[LodIndex];
			{
				int32 FoundIndex;
				if (ThisLODModel.RequiredBones.Find(DuplicateBoneIndex, FoundIndex))
				{
					ThisLODModel.RequiredBones.RemoveAt(FoundIndex, 1);
					// we need to shift indices of the remaining bones.
					for (int32 j = FoundIndex; j < ThisLODModel.RequiredBones.Num(); j++)
					{
						ThisLODModel.RequiredBones[j] = ThisLODModel.RequiredBones[j] - 1;
					}
				}
			}

			{
				int32 FoundIndex;
				if (ThisLODModel.ActiveBoneIndices.Find(DuplicateBoneIndex, FoundIndex))
				{
					ThisLODModel.ActiveBoneIndices.RemoveAt(FoundIndex, 1);
					// we need to shift indices of the remaining bones.
					for (int32 j = FoundIndex; j < ThisLODModel.ActiveBoneIndices.Num(); j++)
					{
						ThisLODModel.ActiveBoneIndices[j] = ThisLODModel.ActiveBoneIndices[j] - 1;
					}
				}
			}
		}
	}

	// Rebuild name table.
	GetRefSkeleton().RebuildNameToIndexMap();
}

#endif


void USkeletalMesh::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	int32 NumTriangles = 0;
	int32 NumVertices = 0;
	FSkeletalMeshRenderData* SkelMeshRenderData = GetResourceForRendering();
	if (SkelMeshRenderData && SkelMeshRenderData->LODRenderData.Num() > 0)
	{
		const FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[0];
		NumTriangles = LODData.GetTotalFaces();
		NumVertices = LODData.GetNumVertices();
	}
	
	int32 NumLODs = LODInfo.Num();

	OutTags.Add(FAssetRegistryTag("Vertices", FString::FromInt(NumVertices), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("Triangles", FString::FromInt(NumTriangles), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("LODs", FString::FromInt(NumLODs), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("Bones", FString::FromInt(GetRefSkeleton().GetRawBoneNum()), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("MorphTargets", FString::FromInt(GetMorphTargets().Num()), FAssetRegistryTag::TT_Numerical));

#if WITH_EDITORONLY_DATA
	if (GetAssetImportData())
	{
		OutTags.Add( FAssetRegistryTag(SourceFileTagName(), GetAssetImportData()->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
#if WITH_EDITOR
		GetAssetImportData()->AppendAssetRegistryTags(OutTags);
#endif
	}
#endif
	
	Super::GetAssetRegistryTags(OutTags);
}

#if WITH_EDITOR
void USkeletalMesh::GetAssetRegistryTagMetadata(TMap<FName, FAssetRegistryTagMetadata>& OutMetadata) const
{
	Super::GetAssetRegistryTagMetadata(OutMetadata);
	OutMetadata.Add("PhysicsAsset", FAssetRegistryTagMetadata().SetImportantValue(TEXT("None")));
}
#endif

void USkeletalMesh::DebugVerifySkeletalMeshLOD()
{
	// if LOD do not have displayfactor set up correctly
	if (LODInfo.Num() > 1)
	{
		for(int32 i=1; i<LODInfo.Num(); i++)
		{
			if (LODInfo[i].ScreenSize.Default <= 0.1f)
			{
				// too small
				UE_LOG(LogSkeletalMesh, Warning, TEXT("SkelMeshLOD (%s) : ScreenSize for LOD %d may be too small (%0.5f)"), *GetPathName(), i, LODInfo[i].ScreenSize.Default);
			}
		}
	}
	else
	{
		// no LODInfo
		UE_LOG(LogSkeletalMesh, Warning, TEXT("SkelMeshLOD (%s) : LOD does not exist"), *GetPathName());
	}
}

void USkeletalMesh::InitMorphTargetsAndRebuildRenderData()
{
#if WITH_EDITOR
	FScopedSkeletalMeshPostEditChange ScopedPostEditChange(this);
#endif
	
	MarkPackageDirty();
	// need to refresh the map
	InitMorphTargets();

	if (IsInGameThread())
	{
		// reset all morphtarget for all components
		for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
		{
			if (It->SkeletalMesh == this)
			{
				It->RefreshMorphTargets();
			}
		}
	}
}

bool USkeletalMesh::RegisterMorphTarget(UMorphTarget* MorphTarget, bool bInvalidateRenderData)
{
	if ( MorphTarget )
	{
		// if MorphTarget has SkelMesh, make sure you unregister before registering yourself
		if ( MorphTarget->BaseSkelMesh && MorphTarget->BaseSkelMesh!=this )
		{
			MorphTarget->BaseSkelMesh->UnregisterMorphTarget(MorphTarget);
		}

		// if the input morphtarget doesn't have valid data, do not add to the base morphtarget
		ensureMsgf(MorphTarget->HasValidData(), TEXT("RegisterMorphTarget: %s has empty data."), *MorphTarget->GetName());

		MorphTarget->BaseSkelMesh = this;

		bool bRegistered = false;

		for ( int32 Index = 0; Index < GetMorphTargets().Num(); ++Index )
		{
			if (GetMorphTargets()[Index]->GetFName() == MorphTarget->GetFName() )
			{
				UE_LOG( LogSkeletalMesh, Verbose, TEXT("RegisterMorphTarget: %s already exists, replacing"), *MorphTarget->GetName() );
				GetMorphTargets()[Index] = MorphTarget;
				bRegistered = true;
				break;
			}
		}

		if (!bRegistered)
		{
			GetMorphTargets().Add( MorphTarget );
			bRegistered = true;
		}

		if (bRegistered && bInvalidateRenderData)
		{
			InitMorphTargetsAndRebuildRenderData();
		}
		return bRegistered;
	}
	return false;
}


void USkeletalMesh::UnregisterAllMorphTarget()
{
	GetMorphTargets().Empty();
	InitMorphTargetsAndRebuildRenderData();
}

void USkeletalMesh::UnregisterMorphTarget(UMorphTarget* MorphTarget)
{
	if ( MorphTarget )
	{
		// Do not remove with MorphTarget->GetFName(). The name might have changed
		// Search the value, and delete	
		for ( int32 I=0; I< GetMorphTargets().Num(); ++I)
		{
			if (GetMorphTargets()[I] == MorphTarget )
			{
				GetMorphTargets().RemoveAt(I);
				--I;
				InitMorphTargetsAndRebuildRenderData();
				return;
			}
		}
		UE_LOG( LogSkeletalMesh, Log, TEXT("UnregisterMorphTarget: %s not found."), *MorphTarget->GetName() );
	}
}

void USkeletalMesh::InitMorphTargets()
{
	GetMorphTargetIndexMap().Empty();

	for (int32 Index = 0; Index < GetMorphTargets().Num(); ++Index)
	{
		UMorphTarget* MorphTarget = GetMorphTargets()[Index];
		// if we don't have a valid data, just remove it
		if (!MorphTarget->HasValidData())
		{
			GetMorphTargets().RemoveAt(Index);
			--Index;
			continue;
		}

		FName const ShapeName = MorphTarget->GetFName();
		if (GetMorphTargetIndexMap().Find(ShapeName) == nullptr)
		{
			GetMorphTargetIndexMap().Add(ShapeName, Index);

			// register as morphtarget curves
			if (GetSkeleton())
			{
				FSmartName CurveName;
				CurveName.DisplayName = ShapeName;
				
				// verify will make sure it adds to the curve if not found
				// the reason of using this is to make sure it works in editor/non-editor
				GetSkeleton()->VerifySmartName(USkeleton::AnimCurveMappingName, CurveName);
				GetSkeleton()->AccumulateCurveMetaData(ShapeName, false, true);
			}
		}
	}
}

UMorphTarget* USkeletalMesh::FindMorphTarget(FName MorphTargetName) const
{
	int32 Index;
	return FindMorphTargetAndIndex(MorphTargetName, Index);
}

UMorphTarget* USkeletalMesh::FindMorphTargetAndIndex(FName MorphTargetName, int32& OutIndex) const
{
	OutIndex = INDEX_NONE;
	if( MorphTargetName != NAME_None )
	{
		const int32* Found = GetMorphTargetIndexMap().Find(MorphTargetName);
		if (Found)
		{
			OutIndex = *Found;
			return GetMorphTargets()[*Found];
		}
	}

	return nullptr;
}

USkeletalMeshSocket* USkeletalMesh::FindSocket(FName InSocketName) const
{
	int32 DummyIdx;
	return FindSocketAndIndex(InSocketName, DummyIdx);
}

#if !WITH_EDITOR

USkeletalMesh::FSocketInfo::FSocketInfo(const USkeletalMesh* InSkeletalMesh, USkeletalMeshSocket* InSocket, int32 InSocketIndex)
	: SocketLocalTransform(InSocket->GetSocketLocalTransform())
	, Socket(InSocket)
	, SocketIndex(InSocketIndex)
	, SocketBoneIndex(InSkeletalMesh->GetRefSkeleton().FindBoneIndex(InSocket->BoneName))
{}

#endif

USkeletalMeshSocket* USkeletalMesh::FindSocketAndIndex(FName InSocketName, int32& OutIndex) const
{
	OutIndex = INDEX_NONE;
	if (InSocketName == NAME_None)
	{
		return nullptr;
	}

#if !WITH_EDITOR
	check(!HasAnyFlags(RF_NeedPostLoad));

	const FSocketInfo* FoundSocketInfo = SocketMap.Find(InSocketName);
	if (FoundSocketInfo)
	{
		OutIndex = FoundSocketInfo->SocketIndex;
		return FoundSocketInfo->Socket;
	}
	return nullptr;
#endif

	for (int32 i = 0; i < Sockets.Num(); i++)
	{
		USkeletalMeshSocket* Socket = Sockets[i];
		if (Socket && Socket->SocketName == InSocketName)
		{
			OutIndex = i;
			return Socket;
		}
	}

	// If the socket isn't on the mesh, try to find it on the skeleton
	if (GetSkeleton())
	{
		USkeletalMeshSocket* SkeletonSocket = GetSkeleton()->FindSocketAndIndex(InSocketName, OutIndex);
		if (SkeletonSocket != nullptr)
		{
			OutIndex += Sockets.Num();
		}
		return SkeletonSocket;
	}

	return nullptr;
}

USkeletalMeshSocket* USkeletalMesh::FindSocketInfo(FName InSocketName, FTransform& OutTransform, int32& OutBoneIndex, int32& OutIndex) const
{
	OutIndex = INDEX_NONE;
	OutTransform = FTransform::Identity;
	OutBoneIndex = INDEX_NONE;

	if (InSocketName == NAME_None)
	{
		return nullptr;
	}

#if !WITH_EDITOR
	check(!HasAnyFlags(RF_NeedPostLoad));

	const FSocketInfo* FoundSocketInfo = SocketMap.Find(InSocketName);
	if (FoundSocketInfo)
	{
		OutTransform = FoundSocketInfo->SocketLocalTransform;
		OutIndex = FoundSocketInfo->SocketIndex;
		OutBoneIndex = FoundSocketInfo->SocketBoneIndex;
		return FoundSocketInfo->Socket;
	}
	return nullptr;
#endif

	for (int32 i = 0; i < Sockets.Num(); i++)
	{
		USkeletalMeshSocket* Socket = Sockets[i];
		if (Socket && Socket->SocketName == InSocketName)
		{
			OutIndex = i;
			OutTransform = Socket->GetSocketLocalTransform();
			OutBoneIndex = GetRefSkeleton().FindBoneIndex(Socket->BoneName);
			return Socket;
		}
	}

	// If the socket isn't on the mesh, try to find it on the skeleton
	if (GetSkeleton())
	{
		USkeletalMeshSocket* SkeletonSocket = GetSkeleton()->FindSocketAndIndex(InSocketName, OutIndex);
		if (SkeletonSocket != nullptr)
		{
			OutIndex += Sockets.Num();
			OutTransform = SkeletonSocket->GetSocketLocalTransform();
			OutBoneIndex = GetRefSkeleton().FindBoneIndex(SkeletonSocket->BoneName);
		}
		return SkeletonSocket;
	}

	return nullptr;
}

int32 USkeletalMesh::NumSockets() const
{
	return Sockets.Num() + (GetSkeleton() ? GetSkeleton()->Sockets.Num() : 0);
}

USkeletalMeshSocket* USkeletalMesh::GetSocketByIndex(int32 Index) const
{
	const int32 NumMeshSockets = Sockets.Num();
	if (Index < NumMeshSockets)
	{
		return Sockets[Index];
	}

	if (GetSkeleton() && (Index - NumMeshSockets) < GetSkeleton()->Sockets.Num())
	{
		return GetSkeleton()->Sockets[Index - NumMeshSockets];
	}

	return nullptr;
}

TMap<FVector, FColor> USkeletalMesh::GetVertexColorData(const uint32 PaintingMeshLODIndex) const
{
	TMap<FVector, FColor> VertexColorData;
#if WITH_EDITOR
	const FSkeletalMeshModel* SkeletalMeshModel = GetImportedModel();
	if (GetHasVertexColors() && SkeletalMeshModel && SkeletalMeshModel->LODModels.IsValidIndex(PaintingMeshLODIndex))
	{
		const TArray<FSkelMeshSection>& Sections = SkeletalMeshModel->LODModels[PaintingMeshLODIndex].Sections;

		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			const TArray<FSoftSkinVertex>& SoftVertices = Sections[SectionIndex].SoftVertices;
			
			for (int32 VertexIndex = 0; VertexIndex < SoftVertices.Num(); ++VertexIndex)
			{
				FVector Position = SoftVertices[VertexIndex].Position;
				FColor& Color = VertexColorData.FindOrAdd(Position);
				Color = SoftVertices[VertexIndex].Color;
			}
		}
	}
#endif // #if WITH_EDITOR

	return VertexColorData;
}


void USkeletalMesh::RebuildSocketMap()
{
#if !WITH_EDITOR

	check(IsInGameThread());

	SocketMap.Reset();
	SocketMap.Reserve(Sockets.Num() + (GetSkeleton() ? GetSkeleton()->Sockets.Num() : 0));

	int32 SocketIndex;
	for (SocketIndex = 0; SocketIndex < Sockets.Num(); ++SocketIndex)
	{
		USkeletalMeshSocket* Socket = Sockets[SocketIndex];
		SocketMap.Add(Socket->SocketName, FSocketInfo(this, Socket, SocketIndex));
	}

	// If the socket isn't on the mesh, try to find it on the skeleton
	if (GetSkeleton())
	{
		for (SocketIndex = 0; SocketIndex < GetSkeleton()->Sockets.Num(); ++SocketIndex)
		{
			USkeletalMeshSocket* Socket = GetSkeleton()->Sockets[SocketIndex];
			if (!SocketMap.Contains(Socket->SocketName))
			{
				SocketMap.Add(Socket->SocketName, FSocketInfo(this, Socket, Sockets.Num() + SocketIndex));
			}
		}
	}

#endif // !WITH_EDITOR
}


/**
 * This will return detail info about this specific object. (e.g. AudioComponent will return the name of the cue,
 * ParticleSystemComponent will return the name of the ParticleSystem)  The idea here is that in many places
 * you have a component of interest but what you really want is some characteristic that you can use to track
 * down where it came from.  
 */
FString USkeletalMesh::GetDetailedInfoInternal() const
{
	return GetPathName(nullptr);
}


FMatrix USkeletalMesh::GetRefPoseMatrix( int32 BoneIndex ) const
{
 	check( BoneIndex >= 0 && BoneIndex < GetRefSkeleton().GetRawBoneNum() );
	FTransform BoneTransform = GetRefSkeleton().GetRawRefBonePose()[BoneIndex];
	// Make sure quaternion is normalized!
	BoneTransform.NormalizeRotation();
	return BoneTransform.ToMatrixWithScale();
}

FMatrix USkeletalMesh::GetComposedRefPoseMatrix( FName InBoneName ) const
{
	FMatrix LocalPose( FMatrix::Identity );

	if ( InBoneName != NAME_None )
	{
		int32 BoneIndex = GetRefSkeleton().FindBoneIndex(InBoneName);
		if (BoneIndex != INDEX_NONE)
		{
			return GetComposedRefPoseMatrix(BoneIndex);
		}
		else
		{
			USkeletalMeshSocket const* Socket = FindSocket(InBoneName);

			if(Socket != NULL)
			{
				BoneIndex = GetRefSkeleton().FindBoneIndex(Socket->BoneName);

				if(BoneIndex != INDEX_NONE)
				{
					const FRotationTranslationMatrix SocketMatrix(Socket->RelativeRotation, Socket->RelativeLocation);
					LocalPose = SocketMatrix * GetComposedRefPoseMatrix(BoneIndex);
				}
			}
		}
	}

	return LocalPose;
}

FMatrix USkeletalMesh::GetComposedRefPoseMatrix(int32 InBoneIndex) const
{
	return CachedComposedRefPoseMatrices[InBoneIndex];
}

TArray<USkeletalMeshSocket*>& USkeletalMesh::GetMeshOnlySocketList()
{
	return Sockets;
}

const TArray<USkeletalMeshSocket*>& USkeletalMesh::GetMeshOnlySocketList() const
{
	return Sockets;
}

#if WITH_EDITORONLY_DATA
void USkeletalMesh::MoveDeprecatedShadowFlagToMaterials()
{
	// First, the easy case where there's no LOD info (in which case, default to true!)
	if ( LODInfo.Num() == 0 )
	{
		for ( auto Material = GetMaterials().CreateIterator(); Material; ++Material )
		{
			Material->bEnableShadowCasting_DEPRECATED = true;
		}

		return;
	}
	
	TArray<bool> PerLodShadowFlags;
	bool bDifferenceFound = false;

	// Second, detect whether the shadow casting flag is the same for all sections of all lods
	for ( auto LOD = LODInfo.CreateConstIterator(); LOD; ++LOD )
	{
		if ( LOD->bEnableShadowCasting_DEPRECATED.Num() )
		{
			PerLodShadowFlags.Add( LOD->bEnableShadowCasting_DEPRECATED[0] );
		}

		if ( !AreAllFlagsIdentical( LOD->bEnableShadowCasting_DEPRECATED ) )
		{
			// We found a difference in the sections of this LOD!
			bDifferenceFound = true;
			break;
		}
	}

	if ( !bDifferenceFound && !AreAllFlagsIdentical( PerLodShadowFlags ) )
	{
		// Difference between LODs
		bDifferenceFound = true;
	}

	if ( !bDifferenceFound )
	{
		// All the same, so just copy the shadow casting flag to all materials
		for ( auto Material = GetMaterials().CreateIterator(); Material; ++Material )
		{
			Material->bEnableShadowCasting_DEPRECATED = PerLodShadowFlags.Num() ? PerLodShadowFlags[0] : true;
		}
	}
	else
	{
		FSkeletalMeshModel* Resource = GetImportedModel();
		check( Resource->LODModels.Num() == LODInfo.Num() );

		TArray<FSkeletalMaterial> NewMaterialArray;
		TArray<FSkeletalMaterial>& CurrentMaterials = GetMaterials();

		// There was a difference, so we need to build a new material list which has all the combinations of UMaterialInterface and shadow casting flag required
		for ( int32 LODIndex = 0; LODIndex < Resource->LODModels.Num(); ++LODIndex )
		{
			check( Resource->LODModels[LODIndex].Sections.Num() == LODInfo[LODIndex].bEnableShadowCasting_DEPRECATED.Num() );

			for ( int32 i = 0; i < Resource->LODModels[LODIndex].Sections.Num(); ++i )
			{
				NewMaterialArray.Add( FSkeletalMaterial(CurrentMaterials[ Resource->LODModels[LODIndex].Sections[i].MaterialIndex ].MaterialInterface, LODInfo[LODIndex].bEnableShadowCasting_DEPRECATED[i], false, NAME_None, NAME_None ) );
			}
		}

		// Reassign the materials array to the new one
		SetMaterials(NewMaterialArray);
		int32 NewIndex = 0;

		// Remap the existing LODModels to point at the correct new material index
		for ( int32 LODIndex = 0; LODIndex < Resource->LODModels.Num(); ++LODIndex )
		{
			check( Resource->LODModels[LODIndex].Sections.Num() == LODInfo[LODIndex].bEnableShadowCasting_DEPRECATED.Num() );

			for ( int32 i = 0; i < Resource->LODModels[LODIndex].Sections.Num(); ++i )
			{
				Resource->LODModels[LODIndex].Sections[i].MaterialIndex = NewIndex;
				++NewIndex;
			}
		}
	}
}

void USkeletalMesh::MoveMaterialFlagsToSections()
{
	//No LOD we cant set the value
	if (LODInfo.Num() == 0)
	{
		return;
	}

	TArray<FSkeletalMaterial>& CurrentMaterials = GetMaterials();
	for (FSkeletalMeshLODModel &StaticLODModel : ImportedModel->LODModels)
	{
		for (int32 SectionIndex = 0; SectionIndex < StaticLODModel.Sections.Num(); ++SectionIndex)
		{
			FSkelMeshSection &Section = StaticLODModel.Sections[SectionIndex];
			//Prior to FEditorObjectVersion::RefactorMeshEditorMaterials Material index match section index
			if (CurrentMaterials.IsValidIndex(SectionIndex))
			{
				Section.bCastShadow = CurrentMaterials[SectionIndex].bEnableShadowCasting_DEPRECATED;

				Section.bRecomputeTangent = CurrentMaterials[SectionIndex].bRecomputeTangent_DEPRECATED;
			}
			else
			{
				//Default cast shadow to true this is a fail safe code path it should not go here if the data
				//is valid
				Section.bCastShadow = true;
				//Recompute tangent is serialize prior to FEditorObjectVersion::RefactorMeshEditorMaterials
				// We just keep the serialize value
			}
		}
	}
}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
FDelegateHandle USkeletalMesh::RegisterOnClothingChange(const FSimpleMulticastDelegate::FDelegate& InDelegate)
{
	return OnClothingChange.Add(InDelegate);
}

void USkeletalMesh::UnregisterOnClothingChange(const FDelegateHandle& InHandle)
{
	OnClothingChange.Remove(InHandle);
}

#endif

bool USkeletalMesh::AreAllFlagsIdentical( const TArray<bool>& BoolArray ) const
{
	if ( BoolArray.Num() == 0 )
	{
		return true;
	}

	for ( int32 i = 0; i < BoolArray.Num() - 1; ++i )
	{
		if ( BoolArray[i] != BoolArray[i + 1] )
		{
			return false;
		}
	}

	return true;
}

bool operator== ( const FSkeletalMaterial& LHS, const FSkeletalMaterial& RHS )
{
	return ( LHS.MaterialInterface == RHS.MaterialInterface );
}

bool operator== ( const FSkeletalMaterial& LHS, const UMaterialInterface& RHS )
{
	return ( LHS.MaterialInterface == &RHS );
}

bool operator== ( const UMaterialInterface& LHS, const FSkeletalMaterial& RHS )
{
	return ( RHS.MaterialInterface == &LHS );
}

FArchive& operator<<(FArchive& Ar, FMeshUVChannelInfo& ChannelData)
{
	Ar << ChannelData.bInitialized;
	Ar << ChannelData.bOverrideDensities;

	for (int32 CoordIndex = 0; CoordIndex < TEXSTREAM_MAX_NUM_UVCHANNELS; ++CoordIndex)
	{
		Ar << ChannelData.LocalUVDensities[CoordIndex];
	}

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FSkeletalMaterial& Elem)
{
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);
	Ar.UsingCustomVersion(FCoreObjectVersion::GUID);

	Ar << Elem.MaterialInterface;

	//Use the automatic serialization instead of this custom operator
	if (Ar.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::RefactorMeshEditorMaterials)
	{
		Ar << Elem.MaterialSlotName;

		bool bSerializeImportedMaterialSlotName = !Ar.IsCooking() || Ar.CookingTarget()->HasEditorOnlyData();
		if (Ar.CustomVer(FCoreObjectVersion::GUID) >= FCoreObjectVersion::SkeletalMaterialEditorDataStripping)
		{
			Ar << bSerializeImportedMaterialSlotName;
		}
		else if (!FPlatformProperties::HasEditorOnlyData())
		{
			bSerializeImportedMaterialSlotName = false;
		}
		if (bSerializeImportedMaterialSlotName)
		{
#if WITH_EDITORONLY_DATA
			Ar << Elem.ImportedMaterialSlotName;
#else
			FName UnusedImportedMaterialSlotName;
			Ar << UnusedImportedMaterialSlotName;
#endif
		}
	}
#if WITH_EDITORONLY_DATA
	else
	{
		if (Ar.UE4Ver() >= VER_UE4_MOVE_SKELETALMESH_SHADOWCASTING)
		{
			Ar << Elem.bEnableShadowCasting_DEPRECATED;
		}

		Ar.UsingCustomVersion(FRecomputeTangentCustomVersion::GUID);
		if (Ar.CustomVer(FRecomputeTangentCustomVersion::GUID) >= FRecomputeTangentCustomVersion::RuntimeRecomputeTangent)
		{
			Ar << Elem.bRecomputeTangent_DEPRECATED;
		}
	}
#endif
	
	if (!Ar.IsLoading() || Ar.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::TextureStreamingMeshUVChannelData)
	{
		Ar << Elem.UVChannelData;
	}

	return Ar;
}

TArray<USkeletalMeshSocket*> USkeletalMesh::GetActiveSocketList() const
{
	TArray<USkeletalMeshSocket*> ActiveSockets = Sockets;

	// Then the skeleton sockets that aren't in the mesh
	if (GetSkeleton())
	{
		for (auto SkeletonSocketIt = GetSkeleton()->Sockets.CreateConstIterator(); SkeletonSocketIt; ++SkeletonSocketIt)
		{
			USkeletalMeshSocket* Socket = *(SkeletonSocketIt);

			if (!IsSocketOnMesh(Socket->SocketName))
			{
				ActiveSockets.Add(Socket);
			}
		}
	}
	return ActiveSockets;
}

bool USkeletalMesh::IsSocketOnMesh(const FName& InSocketName) const
{
	for(int32 SocketIdx=0; SocketIdx < Sockets.Num(); SocketIdx++)
	{
		USkeletalMeshSocket* Socket = Sockets[SocketIdx];

		if(Socket != NULL && Socket->SocketName == InSocketName)
		{
			return true;
		}
	}

	return false;
}

void USkeletalMesh::AllocateResourceForRendering()
{
	SkeletalMeshRenderData = MakeUnique<FSkeletalMeshRenderData>();
}

#if WITH_EDITOR
void USkeletalMesh::InvalidateDeriveDataCacheGUID()
{
	// Create new DDC guid
	GetImportedModel()->GenerateNewGUID();
}

namespace InternalSkeletalMeshHelper
{
	/**
	 * We want to recreate the LODMaterialMap correctly. The hypothesis is the original section will always be the same when we build the skeletalmesh
	 * Max GPU bone per section which drive the chunking which can generate different number of section but the number of original section will always be the same.
	 * So we simply reset the LODMaterialMap and rebuild it with the backup we took before building the skeletalmesh.
	 */
	void CreateLodMaterialMapBackup(const USkeletalMesh* SkeletalMesh, TMap<int32, TArray<int16>>& BackupSectionsPerLOD)
	{
		if (!ensure(SkeletalMesh != nullptr))
		{
			return;
		}
		BackupSectionsPerLOD.Reset();
		const FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
		if (!ImportedModel)
		{
			return;
		}
		//Create the backup
		for (int32 LODIndex = 0; LODIndex < SkeletalMesh->GetLODNum(); ++LODIndex)
		{
			const FSkeletalMeshLODInfo* LODInfoEntry = SkeletalMesh->GetLODInfo(LODIndex);
			//Do not backup/restore LODMaterialMap if...
			if (!ImportedModel->LODModels.IsValidIndex(LODIndex)
				|| LODInfoEntry == nullptr
				|| LODInfoEntry->LODMaterialMap.Num() == 0 //If there is no LODMaterialMap we have nothing to backup
				|| SkeletalMesh->IsReductionActive(LODIndex) //Reduction will manage the LODMaterialMap, avoid backup restore
				|| !SkeletalMesh->IsLODImportedDataBuildAvailable(LODIndex)) //Legacy asset are not build, avoid backup restore
			{
				continue;
			}
			const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
			TArray<int16>& BackupSections = BackupSectionsPerLOD.FindOrAdd(LODIndex);
			int32 SectionCount = LODModel.Sections.Num();
			BackupSections.Reserve(SectionCount);
			for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
			{
				if (LODModel.Sections[SectionIndex].ChunkedParentSectionIndex == INDEX_NONE)
				{
					BackupSections.Add(LODInfoEntry->LODMaterialMap.IsValidIndex(SectionIndex) ? LODInfoEntry->LODMaterialMap[SectionIndex] : INDEX_NONE);
				}
			}
		}
	}

	void RestoreLodMaterialMapBackup(USkeletalMesh* SkeletalMesh, const TMap<int32, TArray<int16>>& BackupSectionsPerLOD)
	{
		if (!ensure(SkeletalMesh != nullptr))
		{
			return;
		}
		const FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
		if (!ImportedModel)
		{
			return;
		}

		for (int32 LODIndex = 0; LODIndex < SkeletalMesh->GetLODNum(); ++LODIndex)
		{
			FSkeletalMeshLODInfo* LODInfoEntry = SkeletalMesh->GetLODInfo(LODIndex);
			if (!ImportedModel->LODModels.IsValidIndex(LODIndex) || LODInfoEntry == nullptr)
			{
				continue;
			}
			const TArray<int16>* BackupSectionsPtr = BackupSectionsPerLOD.Find(LODIndex);
			if (!BackupSectionsPtr)
			{
				continue;
			}

			const TArray<int16>& BackupSections = *BackupSectionsPtr;
			const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
			LODInfoEntry->LODMaterialMap.Reset();
			const int32 SectionCount = LODModel.Sections.Num();
			for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
			{
				const FSkelMeshSection& Section = LODModel.Sections[SectionIndex];
				int16 NewLODMaterialMapValue = INDEX_NONE;
				if (BackupSections.IsValidIndex(Section.OriginalDataSectionIndex))
				{
					NewLODMaterialMapValue = BackupSections[Section.OriginalDataSectionIndex];
				}
				LODInfoEntry->LODMaterialMap.Add(NewLODMaterialMapValue);
			}
		}
	}
} //namespace InternalSkeletalMeshHelper

void USkeletalMesh::CacheDerivedData()
{
	// Cache derived data for the running platform.
	ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	check(RunningPlatform);

	AllocateResourceForRendering();

	// Warn if the platform support minimal number of per vertex bone influences 
	ValidateBoneWeights(RunningPlatform);

	//LODMaterialMap from LODInfo is store in the uasset and not in the DDC, so we want to fix it here
	//to cover the post load and the post edit change. The build can change the number of section and LODMaterialMap is index per section
	//TODO, move LODMaterialmap functionality into the LODModel UserSectionsData which are index per original section (imported section).
	TMap<int32, TArray<int16>> BackupSectionsPerLOD;
	InternalSkeletalMeshHelper::CreateLodMaterialMapBackup(this, BackupSectionsPerLOD);

	SkeletalMeshRenderData->Cache(RunningPlatform, this);

	InternalSkeletalMeshHelper::RestoreLodMaterialMapBackup(this, BackupSectionsPerLOD);

	PostMeshCached.Broadcast(this);
}


void USkeletalMesh::ValidateBoneWeights(const ITargetPlatform* TargetPlatform)
{
	if (TargetPlatform->SupportsFeature(ETargetPlatformFeatures::MobileRendering))
	{
		if (!ImportedModel)
		{
			return;
		}
		FSkeletalMeshRenderData* SkelMeshRenderData = GetResourceForRendering();

		int32 NumLODs = LODInfo.Num();
		int32 MinFirstLOD = GetMinLod().GetValue();
		int32 MaxNumLODs = FMath::Clamp<int32>(NumLODs - MinFirstLOD, SkelMeshRenderData->NumInlinedLODs, NumLODs);

		for (int32 LODIndex = 0; LODIndex < GetLODNum(); ++LODIndex)
		{
			if (!ImportedModel->LODModels.IsValidIndex(LODIndex))
			{
				continue;
			}
			const FSkeletalMeshLODModel& ImportLODModel = ImportedModel->LODModels[LODIndex];

			int32 MaxBoneInfluences = ImportLODModel.GetMaxBoneInfluences();

			for (int32 SectionIndex = 0; SectionIndex < ImportLODModel.Sections.Num(); ++SectionIndex)
			{
				
				const FSkelMeshSection & Section = ImportLODModel.Sections[SectionIndex];

				int MaxBoneInfluencesSection = Section.MaxBoneInfluences;
				if (MaxBoneInfluences > 12)
				{
					UE_LOG(LogSkeletalMesh, Warning, TEXT("Mesh: %s,has more thatn 12 max bone influences, it has: %d"), *GetFullName(), MaxBoneInfluencesSection);
				}
			}
		}

	}
}


void USkeletalMesh::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	// Make sure to cache platform data so it doesn't happen lazily during serialization of the skeletal mesh
	GetPlatformSkeletalMeshRenderData(this, TargetPlatform);
	ValidateBoneWeights(TargetPlatform);
}


FString USkeletalMesh::GetDerivedDataKey()
{
	// Cache derived data for the running platform.
	ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	check(RunningPlatform);

	return SkeletalMeshRenderData->GetDerivedDataKey(RunningPlatform, this);
}

int32 USkeletalMesh::ValidatePreviewAttachedObjects()
{
	int32 NumBrokenAssets = GetPreviewAttachedAssetContainer().ValidatePreviewAttachedObjects();

	if (NumBrokenAssets > 0)
	{
		MarkPackageDirty();
	}
	return NumBrokenAssets;
}

void USkeletalMesh::RemoveMeshSection(int32 InLodIndex, int32 InSectionIndex)
{
	// Need a mesh resource
	if(!ImportedModel.IsValid())
	{
		UE_LOG(LogSkeletalMesh, Warning, TEXT("Failed to remove skeletal mesh section, ImportedResource is invalid."));
		return;
	}

	// Need a valid LOD
	if(!ImportedModel->LODModels.IsValidIndex(InLodIndex))
	{
		UE_LOG(LogSkeletalMesh, Warning, TEXT("Failed to remove skeletal mesh section, LOD%d does not exist in the mesh"), InLodIndex);
		return;
	}

	FSkeletalMeshLODModel& LodModel = ImportedModel->LODModels[InLodIndex];

	// Need a valid section
	if(!LodModel.Sections.IsValidIndex(InSectionIndex))
	{
		UE_LOG(LogSkeletalMesh, Warning, TEXT("Failed to remove skeletal mesh section, Section %d does not exist in LOD%d."), InSectionIndex, InLodIndex);
		return;
	}

	FSkelMeshSection& SectionToDisable = LodModel.Sections[InSectionIndex];
	
	//Get the UserSectionData
	FSkelMeshSourceSectionUserData& UserSectionToDisableData = LodModel.UserSectionsData.FindChecked(SectionToDisable.OriginalDataSectionIndex);

	if(UserSectionToDisableData.HasClothingData())
	{
		// Can't remove this, clothing currently relies on it
		UE_LOG(LogSkeletalMesh, Warning, TEXT("Failed to remove skeletal mesh section, clothing is currently bound to Lod%d Section %d, unbind clothing before removal."), InLodIndex, InSectionIndex);
		return;
	}

	{
		//Scope a post edit change
		FScopedSkeletalMeshPostEditChange ScopedPostEditChange(this);
		// Valid to disable, dirty the mesh
		Modify();
		PreEditChange(nullptr);
		//Disable the section
		UserSectionToDisableData.bDisabled = true;
		SectionToDisable.bDisabled = true;
	}
}

#endif // #if WITH_EDITOR

void USkeletalMesh::ReleaseCPUResources()
{
	FSkeletalMeshRenderData* SkelMeshRenderData = GetResourceForRendering();
	if (SkelMeshRenderData)
	{
		for(int32 Index = 0; Index < SkelMeshRenderData->LODRenderData.Num(); ++Index)
		{
			if (!NeedCPUData(Index))
			{
				SkelMeshRenderData->LODRenderData[Index].ReleaseCPUResources();
			}
		}
	}
}

/** Allocate and initialise bone mirroring table for this skeletal mesh. Default is source = destination for each bone. */
void USkeletalMesh::InitBoneMirrorInfo()
{
	TArray<FBoneMirrorInfo>& LocalSkelMirrorTable = GetSkelMirrorTable();
	LocalSkelMirrorTable.Empty(GetRefSkeleton().GetNum());
	LocalSkelMirrorTable.AddZeroed(GetRefSkeleton().GetNum());

	// By default, no bone mirroring, and source is ourself.
	for(int32 i=0; i< LocalSkelMirrorTable.Num(); i++)
	{
		LocalSkelMirrorTable[i].SourceIndex = i;
	}
}

/** Utility for copying and converting a mirroring table from another SkeletalMesh. */
void USkeletalMesh::CopyMirrorTableFrom(USkeletalMesh* SrcMesh)
{
	TArray<FBoneMirrorInfo>& SrcSkelMirrorTable = SrcMesh->GetSkelMirrorTable();
	TArray<FBoneMirrorInfo>& LocalSkelMirrorTable = GetSkelMirrorTable();
	// Do nothing if no mirror table in source mesh
	if(SrcSkelMirrorTable.Num() == 0)
	{
		return;
	}

	// First, allocate and default mirroring table.
	InitBoneMirrorInfo();

	// Keep track of which entries in the source we have already copied
	TArray<bool> EntryCopied;
	EntryCopied.AddZeroed(SrcSkelMirrorTable.Num() );

	// Mirror table must always be size of ref skeleton.
	check(SrcSkelMirrorTable.Num() == SrcMesh->GetRefSkeleton().GetNum());

	// Iterate over each entry in the source mesh mirror table.
	// We assume that the src table is correct, and don't check for errors here (ie two bones using the same one as source).
	for(int32 i=0; i< SrcSkelMirrorTable.Num(); i++)
	{
		if(!EntryCopied[i])
		{
			// Get name of source and dest bone for this entry in the source table.
			FName DestBoneName = SrcMesh->GetRefSkeleton().GetBoneName(i);
			int32 SrcBoneIndex = SrcSkelMirrorTable[i].SourceIndex;
			FName SrcBoneName = SrcMesh->GetRefSkeleton().GetBoneName(SrcBoneIndex);
			EAxis::Type FlipAxis = SrcSkelMirrorTable[i].BoneFlipAxis;

			// Look up bone names in target mesh (this one)
			int32 DestBoneIndexTarget = GetRefSkeleton().FindBoneIndex(DestBoneName);
			int32 SrcBoneIndexTarget = GetRefSkeleton().FindBoneIndex(SrcBoneName);

			// If both bones found, copy data to this mesh's mirror table.
			if( DestBoneIndexTarget != INDEX_NONE && SrcBoneIndexTarget != INDEX_NONE )
			{
				LocalSkelMirrorTable[DestBoneIndexTarget].SourceIndex = SrcBoneIndexTarget;
				LocalSkelMirrorTable[DestBoneIndexTarget].BoneFlipAxis = FlipAxis;


				LocalSkelMirrorTable[SrcBoneIndexTarget].SourceIndex = DestBoneIndexTarget;
				LocalSkelMirrorTable[SrcBoneIndexTarget].BoneFlipAxis = FlipAxis;

				// Flag entries as copied, so we don't try and do it again.
				EntryCopied[i] = true;
				EntryCopied[SrcBoneIndex] = true;
			}
		}
	}
}

/** Utility for copying and converting a mirroring table from another SkeletalMesh. */
void USkeletalMesh::ExportMirrorTable(TArray<FBoneMirrorExport> &MirrorExportInfo) const
{
	const TArray<FBoneMirrorInfo>& LocalSkelMirrorTable = GetSkelMirrorTable();
	// Do nothing if no mirror table in source mesh
	if(LocalSkelMirrorTable.Num() == 0 )
	{
		return;
	}
	
	// Mirror table must always be size of ref skeleton.
	check(LocalSkelMirrorTable.Num() == GetRefSkeleton().GetNum());

	MirrorExportInfo.Empty(LocalSkelMirrorTable.Num());
	MirrorExportInfo.AddZeroed(LocalSkelMirrorTable.Num());

	// Iterate over each entry in the source mesh mirror table.
	// We assume that the src table is correct, and don't check for errors here (ie two bones using the same one as source).
	for(int32 i=0; i< LocalSkelMirrorTable.Num(); i++)
	{
		MirrorExportInfo[i].BoneName		= GetRefSkeleton().GetBoneName(i);
		MirrorExportInfo[i].SourceBoneName	= GetRefSkeleton().GetBoneName(LocalSkelMirrorTable[i].SourceIndex);
		MirrorExportInfo[i].BoneFlipAxis	= LocalSkelMirrorTable[i].BoneFlipAxis;
	}
}


/** Utility for copying and converting a mirroring table from another SkeletalMesh. */
void USkeletalMesh::ImportMirrorTable(const TArray<FBoneMirrorExport> &MirrorExportInfo)
{
	// Do nothing if no mirror table in source mesh
	if( MirrorExportInfo.Num() == 0 )
	{
		return;
	}

	// First, allocate and default mirroring table.
	InitBoneMirrorInfo();

	// Keep track of which entries in the source we have already copied
	TArray<bool> EntryCopied;
	EntryCopied.AddZeroed(GetRefSkeleton().GetNum() );

	TArray<FBoneMirrorInfo>& LocalSkelMirrorTable = GetSkelMirrorTable();
	// Mirror table must always be size of ref skeleton.
	check(LocalSkelMirrorTable.Num() == GetRefSkeleton().GetNum());

	// Iterate over each entry in the source mesh mirror table.
	// We assume that the src table is correct, and don't check for errors here (ie two bones using the same one as source).
	for(int32 i=0; i<MirrorExportInfo.Num(); i++)
	{
		FName DestBoneName	= MirrorExportInfo[i].BoneName;
		int32 DestBoneIndex	= GetRefSkeleton().FindBoneIndex(DestBoneName);

		if( DestBoneIndex != INDEX_NONE && !EntryCopied[DestBoneIndex] )
		{
			FName SrcBoneName	= MirrorExportInfo[i].SourceBoneName;
			int32 SrcBoneIndex	= GetRefSkeleton().FindBoneIndex(SrcBoneName);
			EAxis::Type FlipAxis		= MirrorExportInfo[i].BoneFlipAxis;

			// If both bones found, copy data to this mesh's mirror table.
			if( SrcBoneIndex != INDEX_NONE )
			{
				LocalSkelMirrorTable[DestBoneIndex].SourceIndex = SrcBoneIndex;
				LocalSkelMirrorTable[DestBoneIndex].BoneFlipAxis = FlipAxis;

				LocalSkelMirrorTable[SrcBoneIndex].SourceIndex = DestBoneIndex;
				LocalSkelMirrorTable[SrcBoneIndex].BoneFlipAxis = FlipAxis;

				// Flag entries as copied, so we don't try and do it again.
				EntryCopied[DestBoneIndex]	= true;
				EntryCopied[SrcBoneIndex]	= true;
			}
		}
	}
}

/** 
 *	Utility for checking that the bone mirroring table of this mesh is good.
 *	Return true if mirror table is OK, false if there are problems.
 *	@param	ProblemBones	Output string containing information on bones that are currently bad.
 */
bool USkeletalMesh::MirrorTableIsGood(FString& ProblemBones) const
{
	TArray<int32>	BadBoneMirror;

	const TArray<FBoneMirrorInfo>& LocalSkelMirrorTable = GetSkelMirrorTable();

	for(int32 i=0; i< LocalSkelMirrorTable.Num(); i++)
	{
		int32 SrcIndex = LocalSkelMirrorTable[i].SourceIndex;
		if(LocalSkelMirrorTable[SrcIndex].SourceIndex != i)
		{
			BadBoneMirror.Add(i);
		}
	}

	if(BadBoneMirror.Num() > 0)
	{
		for(int32 i=0; i<BadBoneMirror.Num(); i++)
		{
			int32 BoneIndex = BadBoneMirror[i];
			FName BoneName = GetRefSkeleton().GetBoneName(BoneIndex);

			ProblemBones += FString::Printf( TEXT("%s (%d)\n"), *BoneName.ToString(), BoneIndex );
		}

		return false;
	}
	else
	{
		return true;
	}
}

void USkeletalMesh::CreateBodySetup()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (BodySetup == nullptr)
	{
		SetBodySetup(NewObject<UBodySetup>(this));
		BodySetup->bSharedCookedData = true;
		BodySetup->AddToCluster(this);
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

#if WITH_EDITOR
void USkeletalMesh::BuildPhysicsData()
{
	CreateBodySetup();
	const USkeletalMesh* ConstThis = this;
	UBodySetup* LocalBodySetup = ConstThis->GetBodySetup();
	LocalBodySetup->CookedFormatData.FlushData();	//we need to force a re-cook because we're essentially re-creating the bodysetup so that it swaps whether or not it has a trimesh
	LocalBodySetup->InvalidatePhysicsData();
	LocalBodySetup->CreatePhysicsMeshes();
}
#endif

bool USkeletalMesh::ContainsPhysicsTriMeshData(bool InUseAllTriData) const
{
	return GetEnablePerPolyCollision();
}

bool USkeletalMesh::GetPhysicsTriMeshData(FTriMeshCollisionData* CollisionData, bool bInUseAllTriData)
{
#if WITH_EDITORONLY_DATA

	// Fail if no mesh or not per poly collision
	if (!GetResourceForRendering() || !GetEnablePerPolyCollision())
	{
		return false;
	}

	FSkeletalMeshRenderData* SkelMeshRenderData = GetResourceForRendering();
	const FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[0];

	const TArray<int32>* MaterialMapPtr = nullptr;
	if (const FSkeletalMeshLODInfo* LODZeroInfo = GetLODInfo(0))
	{
		MaterialMapPtr = &LODZeroInfo->LODMaterialMap;
	}
	// Copy all verts into collision vertex buffer.
	CollisionData->Vertices.Empty();
	CollisionData->Vertices.AddUninitialized(LODData.GetNumVertices());

	for (uint32 VertIdx = 0; VertIdx < LODData.GetNumVertices(); ++VertIdx)
	{
		CollisionData->Vertices[VertIdx] = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertIdx);
	}

	{
		// Copy indices into collision index buffer
		const FMultiSizeIndexContainer& IndexBufferContainer = LODData.MultiSizeIndexContainer;

		TArray<uint32> Indices;
		IndexBufferContainer.GetIndexBuffer(Indices);

		const uint32 NumTris = Indices.Num() / 3;
		CollisionData->Indices.Empty();
		CollisionData->Indices.Reserve(NumTris);

		FTriIndices TriIndex;
		for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); ++SectionIndex)
		{
			const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
			const uint32 OnePastLastIndex = Section.BaseIndex + Section.NumTriangles * 3;
			uint16 MaterialIndex = Section.MaterialIndex;
			if (MaterialMapPtr)
			{
				if (MaterialMapPtr->IsValidIndex(SectionIndex))
				{
					const uint16 RemapMaterialIndex = static_cast<uint16>((*MaterialMapPtr)[SectionIndex]);
					if (GetMaterials().IsValidIndex(RemapMaterialIndex))
					{
						MaterialIndex = RemapMaterialIndex;
					}
				}
			}

			for (uint32 i = Section.BaseIndex; i < OnePastLastIndex; i += 3)
			{
				TriIndex.v0 = Indices[i];
				TriIndex.v1 = Indices[i + 1];
				TriIndex.v2 = Indices[i + 2];

				CollisionData->Indices.Add(TriIndex);
				CollisionData->MaterialIndices.Add(MaterialIndex);
			}
		}
	}

	CollisionData->bFlipNormals = true;
	CollisionData->bDeformableMesh = true;

	// We only have a valid TriMesh if the CollisionData has vertices AND indices. For meshes with disabled section collision, it
	// can happen that the indices will be empty, in which case we do not want to consider that as valid trimesh data
	return CollisionData->Vertices.Num() > 0 && CollisionData->Indices.Num() > 0;
#else // #if WITH_EDITORONLY_DATA
	return false;
#endif // #if WITH_EDITORONLY_DATA
}

void USkeletalMesh::AddAssetUserData(UAssetUserData* InUserData)
{
	if (InUserData != NULL)
	{
		UAssetUserData* ExistingData = GetAssetUserDataOfClass(InUserData->GetClass());
		if (ExistingData != NULL)
		{
			AssetUserData.Remove(ExistingData);
		}
		AssetUserData.Add(InUserData);
	}
}

UAssetUserData* USkeletalMesh::GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (int32 DataIdx = 0; DataIdx < AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if (Datum != NULL && Datum->IsA(InUserDataClass))
		{
			return Datum;
		}
	}
	return NULL;
}

void USkeletalMesh::RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (int32 DataIdx = 0; DataIdx < AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if (Datum != NULL && Datum->IsA(InUserDataClass))
		{
			AssetUserData.RemoveAt(DataIdx);
			return;
		}
	}
}

const TArray<UAssetUserData*>* USkeletalMesh::GetAssetUserDataArray() const
{
	return &AssetUserData;
}

////// SKELETAL MESH THUMBNAIL SUPPORT ////////

/** 
 * Returns a one line description of an object for viewing in the thumbnail view of the generic browser
 */
FString USkeletalMesh::GetDesc()
{
	FString DescString;

	FSkeletalMeshRenderData* Resource = GetResourceForRendering();
	if (Resource)
	{
		check(Resource->LODRenderData.Num() > 0);
		DescString = FString::Printf(TEXT("%d Triangles, %d Bones"), Resource->LODRenderData[0].GetTotalFaces(), GetRefSkeleton().GetRawBoneNum());
	}
	return DescString;
}

bool USkeletalMesh::IsSectionUsingCloth(int32 InSectionIndex, bool bCheckCorrespondingSections) const
{
	FSkeletalMeshRenderData* SkelMeshRenderData = GetResourceForRendering();
	if(SkelMeshRenderData)
	{
		for (FSkeletalMeshLODRenderData& LodData : SkelMeshRenderData->LODRenderData)
		{
			if(LodData.RenderSections.IsValidIndex(InSectionIndex))
			{
				FSkelMeshRenderSection* SectionToCheck = &LodData.RenderSections[InSectionIndex];
				return SectionToCheck->HasClothingData();
			}
		}
	}

	return false;
}

#if WITH_EDITOR
void USkeletalMesh::AddBoneToReductionSetting(int32 LODIndex, const TArray<FName>& BoneNames)
{
	if (LODInfo.IsValidIndex(LODIndex))
	{
		for (auto& BoneName : BoneNames)
		{
			LODInfo[LODIndex].BonesToRemove.AddUnique(BoneName);
		}
	}
}
void USkeletalMesh::AddBoneToReductionSetting(int32 LODIndex, FName BoneName)
{
	if (LODInfo.IsValidIndex(LODIndex))
	{
		LODInfo[LODIndex].BonesToRemove.AddUnique(BoneName);
	}
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
void USkeletalMesh::ConvertLegacyLODScreenSize()
{
	if (LODInfo.Num() == 1)
	{
		// Only one LOD
		LODInfo[0].ScreenSize = 1.0f;
	}
	else
	{
		// Use 1080p, 90 degree FOV as a default, as this should not cause runtime regressions in the common case.
		// LODs will appear different in Persona, however.
		const float HalfFOV = PI * 0.25f;
		const float ScreenWidth = 1920.0f;
		const float ScreenHeight = 1080.0f;
		const FPerspectiveMatrix ProjMatrix(HalfFOV, ScreenWidth, ScreenHeight, 1.0f);
		FBoxSphereBounds Bounds = GetBounds();

		// Multiple models, we should have LOD screen area data.
		for (int32 LODIndex = 0; LODIndex < LODInfo.Num(); ++LODIndex)
		{
			FSkeletalMeshLODInfo& LODInfoEntry = LODInfo[LODIndex];

			if (GetRequiresLODScreenSizeConversion())
			{
				if (LODInfoEntry.ScreenSize.Default == 0.0f)
				{
					LODInfoEntry.ScreenSize.Default = 1.0f;
				}
				else
				{
					// legacy screen size was scaled by a fixed constant of 320.0f, so its kinda arbitrary. Convert back to distance based metric first.
					const float ScreenDepth = FMath::Max(ScreenWidth / 2.0f * ProjMatrix.M[0][0], ScreenHeight / 2.0f * ProjMatrix.M[1][1]) * Bounds.SphereRadius / (LODInfoEntry.ScreenSize.Default * 320.0f);

					// Now convert using the query function
					LODInfoEntry.ScreenSize.Default = ComputeBoundsScreenSize(FVector::ZeroVector, Bounds.SphereRadius, FVector(0.0f, 0.0f, ScreenDepth), ProjMatrix);
				}
			}

			if (GetRequiresLODHysteresisConversion())
			{
				if (LODInfoEntry.LODHysteresis != 0.0f)
				{
					// Also convert the hysteresis as if it was a screen size topo
					const float ScreenHysteresisDepth = FMath::Max(ScreenWidth / 2.0f * ProjMatrix.M[0][0], ScreenHeight / 2.0f * ProjMatrix.M[1][1]) * Bounds.SphereRadius / (LODInfoEntry.LODHysteresis * 320.0f);
					LODInfoEntry.LODHysteresis = ComputeBoundsScreenSize(FVector::ZeroVector, Bounds.SphereRadius, FVector(0.0f, 0.0f, ScreenHysteresisDepth), ProjMatrix);
				}
			}
		}
	}
}
#endif

class UNodeMappingContainer* USkeletalMesh::GetNodeMappingContainer(class UBlueprint* SourceAsset) const
{
	const TArray<UNodeMappingContainer*>& LocalNodeMappingData = GetNodeMappingData();
	for (int32 Index = 0; Index < LocalNodeMappingData.Num(); ++Index)
	{
		UNodeMappingContainer* Iter = LocalNodeMappingData[Index];
		if (Iter && Iter->GetSourceAssetSoftObjectPtr() == TSoftObjectPtr<UObject>(SourceAsset))
		{
			return Iter;
		}
	}

	return nullptr;
}

const UAnimSequence* USkeletalMesh::GetBakePose(int32 LODIndex) const
{
	const FSkeletalMeshLODInfo* LOD = GetLODInfo(LODIndex);
	if (LOD)
	{
		if (LOD->BakePoseOverride && GetSkeleton() == LOD->BakePoseOverride->GetSkeleton())
		{
			return LOD->BakePoseOverride;
		}

		// we make sure bake pose uses same skeleton
		if (LOD->BakePose && GetSkeleton() == LOD->BakePose->GetSkeleton())
		{
			return LOD->BakePose;
		}
	}

	return nullptr;
}

const USkeletalMeshLODSettings* USkeletalMesh::GetDefaultLODSetting() const
{ 
#if WITH_EDITORONLY_DATA
	if (GetLODSettings())
	{
		return GetLODSettings();
	}
#endif // WITH_EDITORONLY_DATA

	return GetDefault<USkeletalMeshLODSettings>();
}

void USkeletalMesh::ReleaseSkinWeightProfileResources()
{
	// This assumes that skin weights buffers are not used anywhere
	if (FSkeletalMeshRenderData* RenderData = GetResourceForRendering())
	{
		for (FSkeletalMeshLODRenderData& LODData : RenderData->LODRenderData)
		{
			LODData.SkinWeightProfilesData.ReleaseResources();
		}
	}
}

FSkeletalMeshLODInfo& USkeletalMesh::AddLODInfo()
{
	int32 NewIndex = LODInfo.AddDefaulted(1);

	check(NewIndex != INDEX_NONE);

	const USkeletalMeshLODSettings* DefaultSetting = GetDefaultLODSetting();
	// if failed to get setting, that means, we don't have proper setting 
	// in that case, use last index setting
	if (!DefaultSetting->SetLODSettingsToMesh(this, NewIndex))
	{
		FSkeletalMeshLODInfo& NewLODInfo = LODInfo[NewIndex];
		if (NewIndex > 0)
		{
			// copy previous copy
			const int32 LastIndex = NewIndex - 1;
			NewLODInfo.ScreenSize.Default = LODInfo[LastIndex].ScreenSize.Default * 0.5f;
			NewLODInfo.LODHysteresis = LODInfo[LastIndex].LODHysteresis;
			NewLODInfo.BakePose = LODInfo[LastIndex].BakePose;
			NewLODInfo.BakePoseOverride = LODInfo[LastIndex].BakePoseOverride;
			NewLODInfo.BonesToRemove = LODInfo[LastIndex].BonesToRemove;
			NewLODInfo.BonesToPrioritize = LODInfo[LastIndex].BonesToPrioritize;
			// now find reduction setting
			for (int32 SubLOD = LastIndex; SubLOD >= 0; --SubLOD)
			{
				if (LODInfo[SubLOD].bHasBeenSimplified)
				{
					// copy from previous index of LOD info reduction setting
					// this may not match with previous copy - as we're only looking for simplified version
					NewLODInfo.ReductionSettings = LODInfo[SubLOD].ReductionSettings;
					// and make it 50 % of that
					NewLODInfo.ReductionSettings.NumOfTrianglesPercentage = FMath::Clamp(NewLODInfo.ReductionSettings.NumOfTrianglesPercentage * 0.5f, 0.f, 1.f);
					// increase maxdeviation, 1.5 is random number
					NewLODInfo.ReductionSettings.MaxDeviationPercentage = FMath::Clamp(NewLODInfo.ReductionSettings.MaxDeviationPercentage * 1.5f, 0.f, 1.f);
					break;
				}
			}

		}
		// if this is the first LOD, then just use default setting of the struct
	}

	return LODInfo[NewIndex];
}

void USkeletalMesh::RemoveLODInfo(int32 Index)
{
	if (LODInfo.IsValidIndex(Index))
	{
#if WITH_EDITOR
		if (IsMeshEditorDataValid())
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			MeshEditorDataObject->RemoveLODImportedData(Index);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
		if (GetImportedModel() && GetImportedModel()->OriginalReductionSourceMeshData.IsValidIndex(Index))
		{
			GetImportedModel()->OriginalReductionSourceMeshData.RemoveAt(Index);
		}
#endif // WITH_EDITOR
		LODInfo.RemoveAt(Index);
	}
}

void USkeletalMesh::ResetLODInfo()
{
	LODInfo.Reset();
}

#if WITH_EDITOR
bool USkeletalMesh::GetSupportsLODStreaming(const ITargetPlatform* TargetPlatform) const
{
	check(TargetPlatform);
	if (NeverStream)
	{
		return false;
	}
	const FName PlatformGroupName = TargetPlatform->GetPlatformInfo().PlatformGroupName;
	const FName VanillaPlatformName = TargetPlatform->GetPlatformInfo().VanillaPlatformName;
	if (GetOverrideLODStreamingSettings())
	{
		return GetSupportLODStreaming().GetValueForPlatformIdentifiers(PlatformGroupName, VanillaPlatformName);
	}
	else
	{
		return GetDefault<URendererSettings>()->bStreamSkeletalMeshLODs.GetValueForPlatformIdentifiers(PlatformGroupName, VanillaPlatformName);
	}
}

int32 USkeletalMesh::GetMaxNumStreamedLODs(const ITargetPlatform* TargetPlatform) const
{
	check(TargetPlatform);
	if (GetOverrideLODStreamingSettings())
	{
		return GetMaxNumStreamedLODs().GetValueForPlatformIdentifiers(
			TargetPlatform->GetPlatformInfo().PlatformGroupName,
			TargetPlatform->GetPlatformInfo().VanillaPlatformName);
	}
	else
	{
		return MAX_MESH_LOD_COUNT;
	}
}

int32 USkeletalMesh::GetMaxNumOptionalLODs(const ITargetPlatform* TargetPlatform) const
{
	check(TargetPlatform);
	const FName PlatformGroupName = TargetPlatform->GetPlatformInfo().PlatformGroupName;
	const FName VanillaPlatformName = TargetPlatform->GetPlatformInfo().VanillaPlatformName;
	if (GetOverrideLODStreamingSettings())
	{
		return GetMaxNumOptionalLODs().GetValueForPlatformIdentifiers(PlatformGroupName, VanillaPlatformName) <= 0 ? 0 : MAX_MESH_LOD_COUNT;
	}
	else
	{
		return GetDefault<URendererSettings>()->bDiscardSkeletalMeshOptionalLODs.GetValueForPlatformIdentifiers(PlatformGroupName, VanillaPlatformName) ? 0 : MAX_MESH_LOD_COUNT;
	}
}
#endif

void USkeletalMesh::SetLODSettings(USkeletalMeshLODSettings* InLODSettings)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
#if WITH_EDITORONLY_DATA
	LODSettings = InLODSettings;
	if (LODSettings)
	{
		LODSettings->SetLODSettingsToMesh(this);
	}
#endif // WITH_EDITORONLY_DATA
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void USkeletalMesh::SetDefaultAnimatingRig(TSoftObjectPtr<UObject> InAnimatingRig)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
#if WITH_EDITORONLY_DATA
	DefaultAnimatingRig = InAnimatingRig;
#endif // WITH_EDITORONLY_DATA
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

TSoftObjectPtr<UObject> USkeletalMesh::GetDefaultAnimatingRig() const
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
#if WITH_EDITORONLY_DATA
	return DefaultAnimatingRig;
#else // WITH_EDITORONLY_DATA
	return nullptr;
#endif // WITH_EDITORONLY_DATA
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void USkeletalMesh::GetMappableNodeData(TArray<FName>& OutNames, TArray<FNodeItem>& OutNodeItems) const
{
	TArray<FTransform> ComponentSpaceRefPose;
#if WITH_EDITORONLY_DATA
	FAnimationRuntime::FillUpComponentSpaceTransformsRetargetBasePose(this, ComponentSpaceRefPose);
#else
	// hasn't tested this route, but we don't have retarget base pose if not editor, wonder we should to non-editor soon
	ensure(false);
	FAnimationRuntime::FillUpComponentSpaceTransforms(GetRefSkeleton(), GetRefSkeleton().GetRefBonePose(), ComponentSpaceRefPose);
#endif // 

	const int32 NumJoint = GetRefSkeleton().GetNum();
	// allocate buffer
	OutNames.Reset(NumJoint);
	OutNodeItems.Reset(NumJoint);

	if (NumJoint > 0)
	{
		OutNames.AddDefaulted(NumJoint);
		OutNodeItems.AddDefaulted(NumJoint);

		const TArray<FMeshBoneInfo> MeshBoneInfo = GetRefSkeleton().GetRefBoneInfo();
		for (int32 NodeIndex = 0; NodeIndex < NumJoint; ++NodeIndex)
		{
			OutNames[NodeIndex] = MeshBoneInfo[NodeIndex].Name;
			if (MeshBoneInfo[NodeIndex].ParentIndex != INDEX_NONE)
			{
				OutNodeItems[NodeIndex] = FNodeItem(MeshBoneInfo[MeshBoneInfo[NodeIndex].ParentIndex].Name, ComponentSpaceRefPose[NodeIndex]);
			}
			else
			{
				OutNodeItems[NodeIndex] = FNodeItem(NAME_None, ComponentSpaceRefPose[NodeIndex]);
			}
		}
	}
}

#if WITH_EDITORONLY_DATA
FText USkeletalMesh::GetSourceFileLabelFromIndex(int32 SourceFileIndex)
{
	int32 RealSourceFileIndex = SourceFileIndex == INDEX_NONE ? 0 : SourceFileIndex;
	return RealSourceFileIndex == 0 ? NSSkeletalMeshSourceFileLabels::GeoAndSkinningText() : RealSourceFileIndex == 1 ? NSSkeletalMeshSourceFileLabels::GeometryText() : NSSkeletalMeshSourceFileLabels::SkinningText();
}

static void SerializeReductionSettingsForDDC(FArchive& Ar, FSkeletalMeshOptimizationSettings& ReductionSettings)
{
	check(Ar.IsSaving());
	// Note: this serializer is only used to build the mesh DDC key, no versioning is required
	FArchive_Serialize_BitfieldBool(Ar, ReductionSettings.bRemapMorphTargets);
	FArchive_Serialize_BitfieldBool(Ar, ReductionSettings.bRecalcNormals);
	FArchive_Serialize_BitfieldBool(Ar, ReductionSettings.bEnforceBoneBoundaries);
	FArchive_Serialize_BitfieldBool(Ar, ReductionSettings.bLockEdges);
	FArchive_Serialize_BitfieldBool(Ar, ReductionSettings.bLockColorBounaries);
	Ar << ReductionSettings.TerminationCriterion;
	Ar << ReductionSettings.NumOfTrianglesPercentage;
	Ar << ReductionSettings.NumOfVertPercentage;
	Ar << ReductionSettings.MaxNumOfTriangles;
	Ar << ReductionSettings.MaxNumOfVerts;

	// Keep old DDC keys if these are not set
	if (ReductionSettings.MaxNumOfTrianglesPercentage != MAX_uint32 || 
		ReductionSettings.MaxNumOfVertsPercentage != MAX_uint32)
	{
		uint32 AvoidCachePoisoningFromOldBug = 0;
		Ar << AvoidCachePoisoningFromOldBug;
		Ar << ReductionSettings.MaxNumOfTrianglesPercentage;
		Ar << ReductionSettings.MaxNumOfVertsPercentage;
	}

	Ar << ReductionSettings.MaxDeviationPercentage;
	Ar << ReductionSettings.ReductionMethod;
	Ar << ReductionSettings.SilhouetteImportance;
	Ar << ReductionSettings.TextureImportance;
	Ar << ReductionSettings.ShadingImportance;
	Ar << ReductionSettings.SkinningImportance;
	Ar << ReductionSettings.WeldingThreshold;
	Ar << ReductionSettings.NormalsThreshold;
	Ar << ReductionSettings.MaxBonesPerVertex;
	Ar << ReductionSettings.VolumeImportance;
	Ar << ReductionSettings.BaseLOD;
}

static void SerializeBuildSettingsForDDC(FArchive& Ar, FSkeletalMeshBuildSettings& BuildSettings)
{
	check(Ar.IsSaving());
	// Note: this serializer is only used to build the mesh DDC key, no versioning is required
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bRecomputeNormals);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bRecomputeTangents);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bUseMikkTSpace);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bComputeWeightedNormals);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bRemoveDegenerates);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bUseFullPrecisionUVs);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bUseHighPrecisionTangentBasis);
	Ar << BuildSettings.ThresholdPosition;
	Ar << BuildSettings.ThresholdTangentNormal;
	Ar << BuildSettings.ThresholdUV;
	Ar << BuildSettings.MorphThresholdPosition;
}


FGuid FSkeletalMeshLODInfo::ComputeDeriveDataCacheKey(const FSkeletalMeshLODGroupSettings* SkeletalMeshLODGroupSettings)
{
	const bool bIs16BitfloatBufferSupported = GVertexElementTypeSupport.IsSupported(VET_Half2);

	// Serialize the LOD info members, the BuildSettings and the ReductionSettings into a temporary array.
	TArray<uint8> TempBytes;
	TempBytes.Reserve(64);
	//The archive is flagged as persistent so that machines of different endianness produce identical binary results.
	FMemoryWriter Ar(TempBytes, /*bIsPersistent=*/ true);

	Ar << BonesToRemove;
	Ar << BonesToPrioritize;
	Ar << WeightOfPrioritization;

	//TODO: Ask the derivedata key of the UObject reference by FSoftObjectPath. So if someone change the UObject, this LODs will get dirty
	//and will be rebuild.
	if (BakePose != nullptr)
	{
		FString BakePosePath = BakePose->GetFullName();
		Ar << BakePosePath;
	}
	if (BakePoseOverride != nullptr)
	{
		FString BakePoseOverridePath = BakePoseOverride->GetFullName();
		Ar << BakePoseOverridePath;
	}
	FArchive_Serialize_BitfieldBool(Ar, bAllowCPUAccess);
	FArchive_Serialize_BitfieldBool(Ar, bSupportUniformlyDistributedSampling);

	//Use the LOD settings asset if there is one
	FSkeletalMeshOptimizationSettings RealReductionSettings = ReductionSettings;
	if (SkeletalMeshLODGroupSettings != nullptr)
	{
		RealReductionSettings = SkeletalMeshLODGroupSettings->GetReductionSettings();
	}

	if (!BuildSettings.bUseFullPrecisionUVs && !bIs16BitfloatBufferSupported)
	{
		BuildSettings.bUseFullPrecisionUVs = true;
	}
	SerializeBuildSettingsForDDC(Ar, BuildSettings);
	SerializeReductionSettingsForDDC(Ar, RealReductionSettings);

	FSHA1 Sha;
	Sha.Update(TempBytes.GetData(), TempBytes.Num() * TempBytes.GetTypeSize());
	Sha.Final();
	// Retrieve the hash and use it to construct a pseudo-GUID.
	uint32 Hash[5];
	Sha.GetHash((uint8*)Hash);
	FGuid Guid = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);
	return Guid;
}

#endif //WITH_EDITOR


TArray<FString> USkeletalMesh::K2_GetAllMorphTargetNames() const
{
	TArray<FString> Names;
	for (UMorphTarget* MorphTarget : GetMorphTargets())
	{
		Names.Add(MorphTarget->GetFName().ToString());
	}
	return Names;
}

/*-----------------------------------------------------------------------------
USkeletalMeshSocket
-----------------------------------------------------------------------------*/
USkeletalMeshSocket::USkeletalMeshSocket(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bForceAlwaysAnimated(true)
{
	RelativeScale = FVector(1.0f, 1.0f, 1.0f);
}

void USkeletalMeshSocket::InitializeSocketFromLocation(const class USkeletalMeshComponent* SkelComp, FVector WorldLocation, FVector WorldNormal)
{
	if (ensureAsRuntimeWarning(SkelComp))
	{
		BoneName = SkelComp->FindClosestBone(WorldLocation);
		if (BoneName != NAME_None)
		{
			SkelComp->TransformToBoneSpace(BoneName, WorldLocation, WorldNormal.Rotation(), RelativeLocation, RelativeRotation);
		}
	}
}

FVector USkeletalMeshSocket::GetSocketLocation(const class USkeletalMeshComponent* SkelComp) const
{
	if (ensureAsRuntimeWarning(SkelComp))
	{
		FMatrix SocketMatrix;
		if (GetSocketMatrix(SocketMatrix, SkelComp))
		{
			return SocketMatrix.GetOrigin();
		}

		// Fall back to MeshComp origin, so it's visible in case of failure.
		return SkelComp->GetComponentLocation();
	}
	return FVector(0.f);
}

bool USkeletalMeshSocket::GetSocketMatrix(FMatrix& OutMatrix, const class USkeletalMeshComponent* SkelComp) const
{
	const int32 BoneIndex = SkelComp ? SkelComp->GetBoneIndex(BoneName) : INDEX_NONE;
	if(BoneIndex != INDEX_NONE)
	{
		FMatrix BoneMatrix = SkelComp->GetBoneMatrix(BoneIndex);
		FScaleRotationTranslationMatrix RelSocketMatrix( RelativeScale, RelativeRotation, RelativeLocation );
		OutMatrix = RelSocketMatrix * BoneMatrix;
		return true;
	}

	return false;
}

FTransform USkeletalMeshSocket::GetSocketLocalTransform() const
{
	return FTransform(RelativeRotation, RelativeLocation, RelativeScale);
}

FTransform USkeletalMeshSocket::GetSocketTransform(const class USkeletalMeshComponent* SkelComp) const
{
	FTransform OutTM;

	const int32 BoneIndex = SkelComp ? SkelComp->GetBoneIndex(BoneName) : INDEX_NONE;
	if(BoneIndex != INDEX_NONE)
	{
		FTransform BoneTM = SkelComp->GetBoneTransform(BoneIndex);
		FTransform RelSocketTM( RelativeRotation, RelativeLocation, RelativeScale );
		OutTM = RelSocketTM * BoneTM;
	}

	return OutTM;
}

bool USkeletalMeshSocket::GetSocketMatrixWithOffset(FMatrix& OutMatrix, class USkeletalMeshComponent* SkelComp, const FVector& InOffset, const FRotator& InRotation) const
{
	const int32 BoneIndex = SkelComp ? SkelComp->GetBoneIndex(BoneName) : INDEX_NONE;
	if(BoneIndex != INDEX_NONE)
	{
		FMatrix BoneMatrix = SkelComp->GetBoneMatrix(BoneIndex);
		FScaleRotationTranslationMatrix RelSocketMatrix(RelativeScale, RelativeRotation, RelativeLocation);
		FRotationTranslationMatrix RelOffsetMatrix(InRotation, InOffset);
		OutMatrix = RelOffsetMatrix * RelSocketMatrix * BoneMatrix;
		return true;
	}

	return false;
}


bool USkeletalMeshSocket::GetSocketPositionWithOffset(FVector& OutPosition, class USkeletalMeshComponent* SkelComp, const FVector& InOffset, const FRotator& InRotation) const
{
	const int32 BoneIndex = SkelComp ? SkelComp->GetBoneIndex(BoneName) : INDEX_NONE;
	if(BoneIndex != INDEX_NONE)
	{
		FMatrix BoneMatrix = SkelComp->GetBoneMatrix(BoneIndex);
		FScaleRotationTranslationMatrix RelSocketMatrix(RelativeScale, RelativeRotation, RelativeLocation);
		FRotationTranslationMatrix RelOffsetMatrix(InRotation, InOffset);
		FMatrix SocketMatrix = RelOffsetMatrix * RelSocketMatrix * BoneMatrix;
		OutPosition = SocketMatrix.GetOrigin();
		return true;
	}

	return false;
}

/** 
 *	Utility to associate an actor with a socket
 *	
 *	@param	Actor			The actor to attach to the socket
 *	@param	SkelComp		The skeletal mesh component that the socket comes from
 *
 *	@return	bool			true if successful, false if not
 */
bool USkeletalMeshSocket::AttachActor(AActor* Actor, class USkeletalMeshComponent* SkelComp) const
{
	bool bAttached = false;
	if (ensureAlways(SkelComp))
	{
		// Don't support attaching to own socket
		if ((Actor != SkelComp->GetOwner()) && Actor->GetRootComponent())
		{
			FMatrix SocketTM;
			if (GetSocketMatrix(SocketTM, SkelComp))
			{
				Actor->Modify();

				Actor->SetActorLocation(SocketTM.GetOrigin(), false);
				Actor->SetActorRotation(SocketTM.Rotator());
				Actor->GetRootComponent()->AttachToComponent(SkelComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);

	#if WITH_EDITOR
				if (GIsEditor)
				{
					Actor->PreEditChange(NULL);
					Actor->PostEditChange();
				}
	#endif // WITH_EDITOR

				bAttached = true;
			}
		}
	}
	return bAttached;
}

#if WITH_EDITOR
void USkeletalMeshSocket::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		ChangedEvent.Broadcast(this, PropertyChangedEvent.MemberProperty);
	}
}

void USkeletalMeshSocket::CopyFrom(const class USkeletalMeshSocket* OtherSocket)
{
	if (OtherSocket)
	{
		SocketName = OtherSocket->SocketName;
		BoneName = OtherSocket->BoneName;
		RelativeLocation = OtherSocket->RelativeLocation;
		RelativeRotation = OtherSocket->RelativeRotation;
		RelativeScale = OtherSocket->RelativeScale;
		bForceAlwaysAnimated = OtherSocket->bForceAlwaysAnimated;
	}
}

#endif

void USkeletalMeshSocket::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	if(Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::MeshSocketScaleUtilization)
	{
		// Set the relative scale to 1.0. As it was not used before this should allow existing data
		// to work as expected.
		RelativeScale = FVector(1.0f, 1.0f, 1.0f);
	}
}


/*-----------------------------------------------------------------------------
FSkeletalMeshSceneProxy
-----------------------------------------------------------------------------*/
#include "Engine/LevelStreaming.h"
#include "LevelUtils.h"

const FQuat SphylBasis(FVector(1.0f / FMath::Sqrt(2.0f), 0.0f, 1.0f / FMath::Sqrt(2.0f)), PI);

/** 
 * Constructor. 
 * @param	Component - skeletal mesh primitive being added
 */
FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(const USkinnedMeshComponent* Component, FSkeletalMeshRenderData* InSkelMeshRenderData)
		:	FPrimitiveSceneProxy(Component, Component->SkeletalMesh->GetFName())
		,	Owner(Component->GetOwner())
		,	MeshObject(Component->MeshObject)
		,	SkeletalMeshRenderData(InSkelMeshRenderData)
		,	SkeletalMeshForDebug(Component->SkeletalMesh)
		,	PhysicsAssetForDebug(Component->GetPhysicsAsset())
#if RHI_RAYTRACING
		,	bAnySegmentUsesWorldPositionOffset(false)
#endif
		,	bForceWireframe(Component->bForceWireframe)
		,	bCanHighlightSelectedSections(Component->bCanHighlightSelectedSections)
		,	bRenderStatic(Component->bRenderStatic)
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		,	bDrawDebugSkeleton(Component->ShouldDrawDebugSkeleton())
#endif
		,	FeatureLevel(GetScene().GetFeatureLevel())
		,	bMaterialsNeedMorphUsage_GameThread(false)
		,	MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		,	DebugDrawColor(Component->GetDebugDrawColor())
#endif
#if WITH_EDITORONLY_DATA
		,	StreamingDistanceMultiplier(FMath::Max(0.0f, Component->StreamingDistanceMultiplier))
#endif
{
	check(MeshObject);
	check(SkeletalMeshRenderData);
	check(SkeletalMeshForDebug);

	bIsCPUSkinned = MeshObject->IsCPUSkinned();

	bCastCapsuleDirectShadow = Component->bCastDynamicShadow && Component->CastShadow && Component->bCastCapsuleDirectShadow;
	bCastsDynamicIndirectShadow = Component->bCastDynamicShadow && Component->CastShadow && Component->bCastCapsuleIndirectShadow;

	DynamicIndirectShadowMinVisibility = FMath::Clamp(Component->CapsuleIndirectShadowMinVisibility, 0.0f, 1.0f);

	// Force inset shadows if capsule shadows are requested, as they can't be supported with full scene shadows
	bCastInsetShadow = bCastInsetShadow || bCastCapsuleDirectShadow;

	// Get the pre-skinned local bounds
	Component->GetPreSkinnedLocalBounds(PreSkinnedLocalBounds);

	const USkinnedMeshComponent* SkinnedMeshComponent = Cast<const USkinnedMeshComponent>(Component);
	if(SkinnedMeshComponent && SkinnedMeshComponent->bPerBoneMotionBlur)
	{
		bAlwaysHasVelocity = true;
	}

	// setup materials and performance classification for each LOD.
	extern bool GForceDefaultMaterial;
	bool bCastShadow = Component->CastShadow;
	bool bAnySectionCastsShadow = false;
	LODSections.Reserve(SkeletalMeshRenderData->LODRenderData.Num());
	LODSections.AddZeroed(SkeletalMeshRenderData->LODRenderData.Num());
	for(int32 LODIdx=0; LODIdx < SkeletalMeshRenderData->LODRenderData.Num(); LODIdx++)
	{
		const FSkeletalMeshLODRenderData& LODData = SkeletalMeshRenderData->LODRenderData[LODIdx];
		const FSkeletalMeshLODInfo& Info = *(Component->SkeletalMesh->GetLODInfo(LODIdx));

		FLODSectionElements& LODSection = LODSections[LODIdx];

		// Presize the array
		LODSection.SectionElements.Empty(LODData.RenderSections.Num() );
		for(int32 SectionIndex = 0;SectionIndex < LODData.RenderSections.Num();SectionIndex++)
		{
			const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];

			// If we are at a dropped LOD, route material index through the LODMaterialMap in the LODInfo struct.
			int32 UseMaterialIndex = Section.MaterialIndex;			
			{
				if(SectionIndex < Info.LODMaterialMap.Num() && Component->SkeletalMesh->GetMaterials().IsValidIndex(Info.LODMaterialMap[SectionIndex]))
				{
					UseMaterialIndex = Info.LODMaterialMap[SectionIndex];
					UseMaterialIndex = FMath::Clamp( UseMaterialIndex, 0, Component->SkeletalMesh->GetMaterials().Num() );
				}
			}

			// If Section is hidden, do not cast shadow
			bool bSectionHidden = MeshObject->IsMaterialHidden(LODIdx,UseMaterialIndex);

			// If the material is NULL, or isn't flagged for use with skeletal meshes, it will be replaced by the default material.
			UMaterialInterface* Material = Component->GetMaterial(UseMaterialIndex);
			if (GForceDefaultMaterial && Material && !IsTranslucentBlendMode(Material->GetBlendMode()))
			{
				Material = UMaterial::GetDefaultMaterial(MD_Surface);
				MaterialRelevance |= Material->GetRelevance(FeatureLevel);
			}

			// if this is a clothing section, then enabled and will be drawn but the corresponding original section should be disabled
			bool bClothSection = Section.HasClothingData();

			bool bValidUsage = Material && Material->CheckMaterialUsage_Concurrent(MATUSAGE_SkeletalMesh);
			if (bClothSection)
			{
				bValidUsage &= Material && Material->CheckMaterialUsage_Concurrent(MATUSAGE_Clothing);
			}

			if(!Material || !bValidUsage)
			{
				UE_CLOG(Material && !bValidUsage, LogSkeletalMesh, Error,
					TEXT("Material with missing usage flag was applied to skeletal mesh %s"),
					*Component->SkeletalMesh->GetPathName());

				Material = UMaterial::GetDefaultMaterial(MD_Surface);
				MaterialRelevance |= Material->GetRelevance(FeatureLevel);
			}

			const bool bRequiresAdjacencyInformation = RequiresAdjacencyInformation( Material, &TGPUSkinVertexFactory<GPUSkinBoneInfluenceType::DefaultBoneInfluence>::StaticType, FeatureLevel );
			if ( bRequiresAdjacencyInformation && LODData.AdjacencyMultiSizeIndexContainer.IsIndexBufferValid() == false )
			{
				UE_LOG(LogSkeletalMesh, Warning, 
					TEXT("Material %s requires adjacency information, but skeletal mesh %s does not have adjacency information built. The mesh must be rebuilt to be used with this material. The mesh will be rendered with DefaultMaterial."), 
					*Material->GetPathName(), 
					*Component->SkeletalMesh->GetPathName() )
				Material = UMaterial::GetDefaultMaterial(MD_Surface);
				MaterialRelevance |= UMaterial::GetDefaultMaterial(MD_Surface)->GetRelevance(FeatureLevel);
			}

			bool bSectionCastsShadow = !bSectionHidden && bCastShadow &&
				(Component->SkeletalMesh->GetMaterials().IsValidIndex(UseMaterialIndex) == false || Section.bCastShadow);

			bAnySectionCastsShadow |= bSectionCastsShadow;

#if RHI_RAYTRACING
			bAnySegmentUsesWorldPositionOffset |= MaterialRelevance.bUsesWorldPositionOffset;
#endif

			LODSection.SectionElements.Add(
				FSectionElementInfo(
					Material,
					bSectionCastsShadow,
					UseMaterialIndex
					));
			MaterialsInUse_GameThread.Add(Material);
		}
	}

	bCastDynamicShadow = bCastDynamicShadow && bAnySectionCastsShadow;

	// Try to find a color for level coloration.
	if( Owner )
	{
		ULevel* Level = Owner->GetLevel();
		ULevelStreaming* LevelStreaming = FLevelUtils::FindStreamingLevel( Level );
		if ( LevelStreaming )
		{
			SetLevelColor(LevelStreaming->LevelColor);
		}
	}

	// Get a color for property coloration
	FColor NewPropertyColor;
	GEngine->GetPropertyColorationColor( (UObject*)Component, NewPropertyColor );
	SetPropertyColor(NewPropertyColor);

	// Copy out shadow physics asset data
	if(SkinnedMeshComponent)
	{
		UPhysicsAsset* ShadowPhysicsAsset = SkinnedMeshComponent->SkeletalMesh->GetShadowPhysicsAsset();

		if (ShadowPhysicsAsset
			&& SkinnedMeshComponent->CastShadow
			&& (SkinnedMeshComponent->bCastCapsuleDirectShadow || SkinnedMeshComponent->bCastCapsuleIndirectShadow))
		{
			for (int32 BodyIndex = 0; BodyIndex < ShadowPhysicsAsset->SkeletalBodySetups.Num(); BodyIndex++)
			{
				UBodySetup* BodySetup = ShadowPhysicsAsset->SkeletalBodySetups[BodyIndex];
				int32 BoneIndex = SkinnedMeshComponent->GetBoneIndex(BodySetup->BoneName);

				if (BoneIndex != INDEX_NONE)
				{
					const FMatrix& RefBoneMatrix = SkinnedMeshComponent->SkeletalMesh->GetComposedRefPoseMatrix(BoneIndex);

					const int32 NumSpheres = BodySetup->AggGeom.SphereElems.Num();
					for (int32 SphereIndex = 0; SphereIndex < NumSpheres; SphereIndex++)
					{
						const FKSphereElem& SphereShape = BodySetup->AggGeom.SphereElems[SphereIndex];
						ShadowCapsuleData.Emplace(BoneIndex, FCapsuleShape(RefBoneMatrix.TransformPosition(SphereShape.Center), SphereShape.Radius, FVector(0.0f, 0.0f, 1.0f), 0.0f));
					}

					const int32 NumCapsules = BodySetup->AggGeom.SphylElems.Num();
					for (int32 CapsuleIndex = 0; CapsuleIndex < NumCapsules; CapsuleIndex++)
					{
						const FKSphylElem& SphylShape = BodySetup->AggGeom.SphylElems[CapsuleIndex];
						ShadowCapsuleData.Emplace(BoneIndex, FCapsuleShape(RefBoneMatrix.TransformPosition(SphylShape.Center), SphylShape.Radius, RefBoneMatrix.TransformVector((SphylShape.Rotation.Quaternion() * SphylBasis).Vector()), SphylShape.Length));
					}

					if (NumSpheres > 0 || NumCapsules > 0)
					{
						ShadowCapsuleBoneIndices.AddUnique(BoneIndex);
					}
				}
			}
		}
	}

	// Sort to allow merging with other bone hierarchies
	if (ShadowCapsuleBoneIndices.Num())
	{
		ShadowCapsuleBoneIndices.Sort();
	}

	// Skip primitive uniform buffer if we will be using local vertex factory which gets it's data from GPUScene.
	bVFRequiresPrimitiveUniformBuffer = !((bIsCPUSkinned || bRenderStatic) && UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel));

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		if (bRenderStatic)
		{
			RayTracingGeometries.AddDefaulted(SkeletalMeshRenderData->LODRenderData.Num());
			for (int32 LODIndex = 0; LODIndex < SkeletalMeshRenderData->LODRenderData.Num(); LODIndex++)
			{
				ensure(SkeletalMeshRenderData->LODRenderData[LODIndex].NumReferencingStaticSkeletalMeshObjects > 0);
				RayTracingGeometries[LODIndex] = &SkeletalMeshRenderData->LODRenderData[LODIndex].StaticRayTracingGeometry;
			}
		}
	}
#endif
}


// FPrimitiveSceneProxy interface.

/** 
 * Iterates over sections,chunks,elements based on current instance weight usage 
 */
class FSkeletalMeshSectionIter
{
public:
	FSkeletalMeshSectionIter(const int32 InLODIdx, const FSkeletalMeshObject& InMeshObject, const FSkeletalMeshLODRenderData& InLODData, const FSkeletalMeshSceneProxy::FLODSectionElements& InLODSectionElements)
		: SectionIndex(0)
		, MeshObject(InMeshObject)
		, LODSectionElements(InLODSectionElements)
		, Sections(InLODData.RenderSections)
#if WITH_EDITORONLY_DATA
		, SectionIndexPreview(InMeshObject.SectionIndexPreview)
		, MaterialIndexPreview(InMeshObject.MaterialIndexPreview)
#endif
	{
		while (NotValidPreviewSection())
		{
			SectionIndex++;
		}
	}
	FORCEINLINE FSkeletalMeshSectionIter& operator++()
	{
		do 
		{
		SectionIndex++;
		} while (NotValidPreviewSection());
		return *this;
	}
	FORCEINLINE explicit operator bool() const
	{
		return ((SectionIndex < Sections.Num()) && LODSectionElements.SectionElements.IsValidIndex(GetSectionElementIndex()));
	}
	FORCEINLINE const FSkelMeshRenderSection& GetSection() const
	{
		return Sections[SectionIndex];
	}
	FORCEINLINE const int32 GetSectionElementIndex() const
	{
		return SectionIndex;
	}
	FORCEINLINE const FSkeletalMeshSceneProxy::FSectionElementInfo& GetSectionElementInfo() const
	{
		int32 SectionElementInfoIndex = GetSectionElementIndex();
		return LODSectionElements.SectionElements[SectionElementInfoIndex];
	}
	FORCEINLINE bool NotValidPreviewSection()
	{
#if WITH_EDITORONLY_DATA
		if (MaterialIndexPreview == INDEX_NONE)
		{
			int32 ActualPreviewSectionIdx = SectionIndexPreview;

			return	(SectionIndex < Sections.Num()) &&
				((ActualPreviewSectionIdx >= 0) && (ActualPreviewSectionIdx != SectionIndex));
		}
		else
		{
			int32 ActualPreviewMaterialIdx = MaterialIndexPreview;
			int32 ActualPreviewSectionIdx = INDEX_NONE;
			if (ActualPreviewMaterialIdx != INDEX_NONE && Sections.IsValidIndex(SectionIndex))
			{
				const FSkeletalMeshSceneProxy::FSectionElementInfo& SectionInfo = LODSectionElements.SectionElements[SectionIndex];
				if (SectionInfo.UseMaterialIndex == ActualPreviewMaterialIdx)
				{
					ActualPreviewSectionIdx = SectionIndex;
				}
			}

			return	(SectionIndex < Sections.Num()) &&
				((ActualPreviewMaterialIdx >= 0) && (ActualPreviewSectionIdx != SectionIndex));
		}
#else
		return false;
#endif
	}
private:
	int32 SectionIndex;
	const FSkeletalMeshObject& MeshObject;
	const FSkeletalMeshSceneProxy::FLODSectionElements& LODSectionElements;
	const TArray<FSkelMeshRenderSection>& Sections;
#if WITH_EDITORONLY_DATA
	const int32 SectionIndexPreview;
	const int32 MaterialIndexPreview;
#endif
};

#if WITH_EDITOR
HHitProxy* FSkeletalMeshSceneProxy::CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies)
{
	if ( Component->GetOwner() )
	{
		if ( LODSections.Num() > 0 )
		{
			for ( int32 LODIndex = 0; LODIndex < SkeletalMeshRenderData->LODRenderData.Num(); LODIndex++ )
			{
				const FSkeletalMeshLODRenderData& LODData = SkeletalMeshRenderData->LODRenderData[LODIndex];

				FLODSectionElements& LODSection = LODSections[LODIndex];

				check(LODSection.SectionElements.Num() == LODData.RenderSections.Num());

				for ( int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); SectionIndex++ )
				{
					HHitProxy* ActorHitProxy;

					int32 MaterialIndex = LODData.RenderSections[SectionIndex].MaterialIndex;
					if ( Component->GetOwner()->IsA(ABrush::StaticClass()) && Component->IsA(UBrushComponent::StaticClass()) )
					{
						ActorHitProxy = new HActor(Component->GetOwner(), Component, HPP_Wireframe, SectionIndex, MaterialIndex);
					}
					else
					{
						ActorHitProxy = new HActor(Component->GetOwner(), Component, Component->HitProxyPriority, SectionIndex, MaterialIndex);
					}

					// Set the hitproxy.
					check(LODSection.SectionElements[SectionIndex].HitProxy == NULL);
					LODSection.SectionElements[SectionIndex].HitProxy = ActorHitProxy;
					OutHitProxies.Add(ActorHitProxy);
				}
			}
		}
		else
		{
			return FPrimitiveSceneProxy::CreateHitProxies(Component, OutHitProxies);
		}
	}

	return NULL;
}
#endif

void FSkeletalMeshSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
{
	if (!MeshObject || !bRenderStatic)
	{
		return;
	}

	if (!HasViewDependentDPG())
	{
		uint8 PrimitiveDPG = GetStaticDepthPriorityGroup();
		bool bUseSelectedMaterial = false;

		int32 NumLODs = SkeletalMeshRenderData->LODRenderData.Num();
		int32 ClampedMinLOD = 0; // TODO: MinLOD, Bias?

		for (int32 LODIndex = ClampedMinLOD; LODIndex < NumLODs; ++LODIndex)
		{
			const FSkeletalMeshLODRenderData& LODData = SkeletalMeshRenderData->LODRenderData[LODIndex];
			
			if (LODSections.Num() > 0 && LODData.GetNumVertices() > 0)
			{
				float ScreenSize = MeshObject->GetScreenSize(LODIndex);
				const FLODSectionElements& LODSection = LODSections[LODIndex];
				check(LODSection.SectionElements.Num() == LODData.RenderSections.Num());

				for (FSkeletalMeshSectionIter Iter(LODIndex, *MeshObject, LODData, LODSection); Iter; ++Iter)
				{
					const FSkelMeshRenderSection& Section = Iter.GetSection();
					const int32 SectionIndex = Iter.GetSectionElementIndex();
					const FSectionElementInfo& SectionElementInfo = Iter.GetSectionElementInfo();
					const FVertexFactory* VertexFactory = MeshObject->GetSkinVertexFactory(nullptr, LODIndex, SectionIndex);
				
					// If hidden skip the draw
					if (MeshObject->IsMaterialHidden(LODIndex, SectionElementInfo.UseMaterialIndex))
					{
						continue;
					}
					
					if (!VertexFactory)
					{
						// hide this part
						continue;
					}

				#if WITH_EDITOR
					if (GIsEditor)
					{
						bUseSelectedMaterial = (MeshObject->SelectedEditorSection == SectionIndex);
						PDI->SetHitProxy(SectionElementInfo.HitProxy);
					}
				#endif // WITH_EDITOR
								
					FMeshBatch MeshElement;
					FMeshBatchElement& BatchElement = MeshElement.Elements[0];
					MeshElement.DepthPriorityGroup = PrimitiveDPG;
					MeshElement.VertexFactory = MeshObject->GetSkinVertexFactory(nullptr, LODIndex, SectionIndex);
					MeshElement.MaterialRenderProxy = SectionElementInfo.Material->GetRenderProxy();
					MeshElement.ReverseCulling = IsLocalToWorldDeterminantNegative();
					MeshElement.CastShadow = SectionElementInfo.bEnableShadowCasting;
				#if RHI_RAYTRACING
					MeshElement.CastRayTracedShadow = MeshElement.CastShadow && bCastDynamicShadow;
				#endif
					MeshElement.Type = PT_TriangleList;
					MeshElement.LODIndex = LODIndex;
					MeshElement.SegmentIndex = SectionIndex;
						
					BatchElement.FirstIndex = Section.BaseIndex;
					BatchElement.MinVertexIndex = Section.BaseVertexIndex;
					BatchElement.MaxVertexIndex = LODData.GetNumVertices() - 1;
					BatchElement.NumPrimitives = Section.NumTriangles;
					BatchElement.IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer();
													
					PDI->DrawMesh(MeshElement, ScreenSize);
				}
			}
		}
	}
}

void FSkeletalMeshSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FSkeletalMeshSceneProxy_GetMeshElements);
	GetMeshElementsConditionallySelectable(Views, ViewFamily, true, VisibilityMap, Collector);
}

void FSkeletalMeshSceneProxy::GetMeshElementsConditionallySelectable(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, bool bInSelectable, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	if( !MeshObject )
	{
		return;
	}	
	MeshObject->PreGDMECallback(ViewFamily.Scene->GetGPUSkinCache(), ViewFamily.FrameNumber);

	const FEngineShowFlags& EngineShowFlags = ViewFamily.EngineShowFlags;

	int32 FirstLODIdx = SkeletalMeshRenderData->GetFirstValidLODIdx(FMath::Max(SkeletalMeshRenderData->PendingFirstLODIdx, SkeletalMeshRenderData->CurrentFirstLODIdx));
	if (FirstLODIdx == INDEX_NONE)
	{
#if DO_CHECK
		UE_LOG(LogSkeletalMesh, Warning, TEXT("Skeletal mesh %s has no valid LODs for rendering."), *GetResourceName().ToString());
#endif
	}
	else
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];
				MeshObject->UpdateMinDesiredLODLevel(View, GetBounds(), ViewFamily.FrameNumber, FirstLODIdx);
			}
		}

		const int32 LODIndex = MeshObject->GetLOD();
		check(LODIndex < SkeletalMeshRenderData->LODRenderData.Num());
		const FSkeletalMeshLODRenderData& LODData = SkeletalMeshRenderData->LODRenderData[LODIndex];

		if (LODSections.Num() > 0 && LODIndex >= FirstLODIdx)
		{
			check(SkeletalMeshRenderData->LODRenderData[LODIndex].GetNumVertices() > 0);

			const FLODSectionElements& LODSection = LODSections[LODIndex];

			check(LODSection.SectionElements.Num() == LODData.RenderSections.Num());

			for (FSkeletalMeshSectionIter Iter(LODIndex, *MeshObject, LODData, LODSection); Iter; ++Iter)
			{
				const FSkelMeshRenderSection& Section = Iter.GetSection();
				const int32 SectionIndex = Iter.GetSectionElementIndex();
				const FSectionElementInfo& SectionElementInfo = Iter.GetSectionElementInfo();

				bool bSectionSelected = false;

#if WITH_EDITORONLY_DATA
				// TODO: This is not threadsafe! A render command should be used to propagate SelectedEditorSection to the scene proxy.
				if (MeshObject->SelectedEditorMaterial != INDEX_NONE)
				{
					bSectionSelected = (MeshObject->SelectedEditorMaterial == SectionElementInfo.UseMaterialIndex);
				}
				else
				{
					bSectionSelected = (MeshObject->SelectedEditorSection == SectionIndex);
				}
			
#endif
				// If hidden skip the draw
				if (MeshObject->IsMaterialHidden(LODIndex, SectionElementInfo.UseMaterialIndex) || Section.bDisabled)
				{
					continue;
				}

				GetDynamicElementsSection(Views, ViewFamily, VisibilityMap, LODData, LODIndex, SectionIndex, bSectionSelected, SectionElementInfo, bInSelectable, Collector);
			}
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			if( PhysicsAssetForDebug )
			{
				DebugDrawPhysicsAsset(ViewIndex, Collector, ViewFamily.EngineShowFlags);
			}

			if (EngineShowFlags.MassProperties && DebugMassData.Num() > 0)
			{
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				if (MeshObject->GetComponentSpaceTransforms())
				{
					const TArray<FTransform>& ComponentSpaceTransforms = *MeshObject->GetComponentSpaceTransforms();

					for (const FDebugMassData& DebugMass : DebugMassData)
					{
						if (ComponentSpaceTransforms.IsValidIndex(DebugMass.BoneIndex))
						{
							const FTransform BoneToWorld = ComponentSpaceTransforms[DebugMass.BoneIndex] * FTransform(GetLocalToWorld());
							DebugMass.DrawDebugMass(PDI, BoneToWorld);
						}
					}
				}
			}

			if (ViewFamily.EngineShowFlags.SkeletalMeshes)
			{
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
			}

			if (ViewFamily.EngineShowFlags.Bones || bDrawDebugSkeleton)
			{
				DebugDrawSkeleton(ViewIndex, Collector, ViewFamily.EngineShowFlags);
			}
		}
	}
#endif
}

void FSkeletalMeshSceneProxy::CreateBaseMeshBatch(const FSceneView* View, const FSkeletalMeshLODRenderData& LODData, const int32 LODIndex, const int32 SectionIndex, const FSectionElementInfo& SectionElementInfo, FMeshBatch& Mesh) const
{
	Mesh.VertexFactory = MeshObject->GetSkinVertexFactory(View, LODIndex, SectionIndex);
	Mesh.MaterialRenderProxy = SectionElementInfo.Material->GetRenderProxy();
#if RHI_RAYTRACING
	Mesh.SegmentIndex = SectionIndex;
	Mesh.CastRayTracedShadow = SectionElementInfo.bEnableShadowCasting && bCastDynamicShadow;
#endif

	FMeshBatchElement& BatchElement = Mesh.Elements[0];
	BatchElement.FirstIndex = LODData.RenderSections[SectionIndex].BaseIndex;
	BatchElement.IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer();
	BatchElement.MinVertexIndex = LODData.RenderSections[SectionIndex].GetVertexBufferIndex();
	BatchElement.MaxVertexIndex = LODData.RenderSections[SectionIndex].GetVertexBufferIndex() + LODData.RenderSections[SectionIndex].GetNumVertices() - 1;
	BatchElement.VertexFactoryUserData = FGPUSkinCache::GetFactoryUserData(MeshObject->SkinCacheEntry, SectionIndex);
	BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
	BatchElement.NumPrimitives = LODData.RenderSections[SectionIndex].NumTriangles;
}

uint8 FSkeletalMeshSceneProxy::GetCurrentFirstLODIdx_Internal() const
{
	return SkeletalMeshRenderData->CurrentFirstLODIdx;
}

void FSkeletalMeshSceneProxy::GetDynamicElementsSection(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, 
	const FSkeletalMeshLODRenderData& LODData, const int32 LODIndex, const int32 SectionIndex, bool bSectionSelected,
	const FSectionElementInfo& SectionElementInfo, bool bInSelectable, FMeshElementCollector& Collector ) const
{
	const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];

	//// If hidden skip the draw
	//if (Section.bDisabled || MeshObject->IsMaterialHidden(LODIndex,SectionElementInfo.UseMaterialIndex))
	//{
	//	return;
	//}

#if !WITH_EDITOR
	const bool bIsSelected = false;
#else // #if !WITH_EDITOR
	bool bIsSelected = IsSelected();

	// if the mesh isn't selected but the mesh section is selected in the AnimSetViewer, find the mesh component and make sure that it can be highlighted (ie. are we rendering for the AnimSetViewer or not?)
	if( !bIsSelected && bSectionSelected && bCanHighlightSelectedSections )
	{
		bIsSelected = true;
	}
#endif // #if WITH_EDITOR

	const bool bIsWireframe = ViewFamily.EngineShowFlags.Wireframe;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];

			FMeshBatch& Mesh = Collector.AllocateMesh();

			CreateBaseMeshBatch(View, LODData, LODIndex, SectionIndex, SectionElementInfo, Mesh);
			
			if(!Mesh.VertexFactory)
			{
				// hide this part
				continue;
			}

			Mesh.bWireframe |= bForceWireframe;
			Mesh.Type = PT_TriangleList;
			Mesh.bSelectable = bInSelectable;

			FMeshBatchElement& BatchElement = Mesh.Elements[0];
			const bool bRequiresAdjacencyInformation = RequiresAdjacencyInformation( SectionElementInfo.Material, Mesh.VertexFactory->GetType(), ViewFamily.GetFeatureLevel() );
			if ( bRequiresAdjacencyInformation )
			{
				check(LODData.AdjacencyMultiSizeIndexContainer.IsIndexBufferValid() );
				BatchElement.IndexBuffer = LODData.AdjacencyMultiSizeIndexContainer.GetIndexBuffer();
				Mesh.Type = PT_12_ControlPointPatchList;
				BatchElement.FirstIndex *= 4;
			}

		#if WITH_EDITOR
			Mesh.BatchHitProxyId = SectionElementInfo.HitProxy ? SectionElementInfo.HitProxy->Id : FHitProxyId();

			if (bSectionSelected && bCanHighlightSelectedSections)
			{
				Mesh.bUseSelectionOutline = true;
			}
			else
			{
				Mesh.bUseSelectionOutline = !bCanHighlightSelectedSections && bIsSelected;
			}
		#endif

#if WITH_EDITORONLY_DATA
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (bIsSelected)
			{
				if (ViewFamily.EngineShowFlags.VertexColors && AllowDebugViewmodes())
				{
					// Override the mesh's material with our material that draws the vertex colors
					UMaterial* VertexColorVisualizationMaterial = NULL;
					switch (GVertexColorViewMode)
					{
					case EVertexColorViewMode::Color:
						VertexColorVisualizationMaterial = GEngine->VertexColorViewModeMaterial_ColorOnly;
						break;

					case EVertexColorViewMode::Alpha:
						VertexColorVisualizationMaterial = GEngine->VertexColorViewModeMaterial_AlphaAsColor;
						break;

					case EVertexColorViewMode::Red:
						VertexColorVisualizationMaterial = GEngine->VertexColorViewModeMaterial_RedOnly;
						break;

					case EVertexColorViewMode::Green:
						VertexColorVisualizationMaterial = GEngine->VertexColorViewModeMaterial_GreenOnly;
						break;

					case EVertexColorViewMode::Blue:
						VertexColorVisualizationMaterial = GEngine->VertexColorViewModeMaterial_BlueOnly;
						break;
					}
					check(VertexColorVisualizationMaterial != NULL);

					auto VertexColorVisualizationMaterialInstance = new FColoredMaterialRenderProxy(
						VertexColorVisualizationMaterial->GetRenderProxy(),
						GetSelectionColor(FLinearColor::White, bSectionSelected, IsHovered())
					);

					Collector.RegisterOneFrameMaterialProxy(VertexColorVisualizationMaterialInstance);
					Mesh.MaterialRenderProxy = VertexColorVisualizationMaterialInstance;
				}
			}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif // WITH_EDITORONLY_DATA

			BatchElement.MinVertexIndex = Section.BaseVertexIndex;
			Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
			Mesh.CastShadow = SectionElementInfo.bEnableShadowCasting;
			Mesh.bCanApplyViewModeOverrides = true;
			Mesh.bUseWireframeSelectionColoring = bIsSelected;

		#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			BatchElement.VisualizeElementIndex = SectionIndex;
			Mesh.VisualizeLODIndex = LODIndex;
		#endif

			if (ensureMsgf(Mesh.MaterialRenderProxy, TEXT("GetDynamicElementsSection with invalid MaterialRenderProxy. Owner:%s LODIndex:%d UseMaterialIndex:%d"), *GetOwnerName().ToString(), LODIndex, SectionElementInfo.UseMaterialIndex))
			{
				Collector.AddMesh(ViewIndex, Mesh);
			}

			const int32 NumVertices = Section.GetNumVertices();
			INC_DWORD_STAT_BY(STAT_GPUSkinVertices,(uint32)(bIsCPUSkinned ? 0 : NumVertices));
			INC_DWORD_STAT_BY(STAT_SkelMeshTriangles,Mesh.GetNumPrimitives());
			INC_DWORD_STAT(STAT_SkelMeshDrawCalls);
		}
	}
}

#if RHI_RAYTRACING
void FSkeletalMeshSceneProxy::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext & Context, TArray<struct FRayTracingInstance>& OutRayTracingInstances)
{
	if (!CVarRayTracingSkeletalMeshes.GetValueOnRenderThread())
	{
		return;
	}

	// GetRayTracingGeometry()->IsInitialized() is checked as a workaround for UE-92634. FSkeletalMeshSceneProxy's resources may have already been released, but proxy has not removed yet)
	if (MeshObject->GetRayTracingGeometry() && MeshObject->GetRayTracingGeometry()->IsInitialized() )
	{
		// #dxr: the only case where RayTracingGeometryRHI is invalid is the very first frame - if that's not the case we have a bug somewhere else
		if (MeshObject->GetRayTracingGeometry()->RayTracingGeometryRHI.IsValid())
		{
			check(MeshObject->GetRayTracingGeometry()->Initializer.IndexBuffer.IsValid());
			
			FRayTracingInstance RayTracingInstance;
			RayTracingInstance.Geometry = MeshObject->GetRayTracingGeometry();

				// Setup materials for each segment
				const int32 LODIndex = MeshObject->GetLOD();
				check(LODIndex < SkeletalMeshRenderData->LODRenderData.Num());
				const FSkeletalMeshLODRenderData& LODData = SkeletalMeshRenderData->LODRenderData[LODIndex];

			if (LODIndex < SkeletalMeshRenderData->CurrentFirstLODIdx)
			{
				// According to GetMeshElementsConditionallySelectable(), non-resident LODs should just be skipped
				return;
			}

				ensure(LODSections.Num() > 0);
				const FLODSectionElements& LODSection = LODSections[LODIndex];
				check(LODSection.SectionElements.Num() == LODData.RenderSections.Num());
				
				//#dxr_todo: verify why this condition is not fulfilled sometimes
				verify(LODSection.SectionElements.Num() == MeshObject->GetRayTracingGeometry()->Initializer.Segments.Num());
				if(LODSection.SectionElements.Num() != MeshObject->GetRayTracingGeometry()->Initializer.Segments.Num())
				{
					return;
				}

			#if WITH_EDITORONLY_DATA
				int32 SectionIndexPreview = MeshObject->SectionIndexPreview;
				int32 MaterialIndexPreview = MeshObject->MaterialIndexPreview;
				MeshObject->SectionIndexPreview = INDEX_NONE;
				MeshObject->MaterialIndexPreview = INDEX_NONE;
			#endif
				for (FSkeletalMeshSectionIter Iter(LODIndex, *MeshObject, LODData, LODSection); Iter; ++Iter)
				{
					const FSkelMeshRenderSection& Section = Iter.GetSection();
					const int32 SectionIndex = Iter.GetSectionElementIndex();
					const FSectionElementInfo& SectionElementInfo = Iter.GetSectionElementInfo();

					FMeshBatch MeshBatch;
					CreateBaseMeshBatch(Context.ReferenceView, LODData, LODIndex, SectionIndex, SectionElementInfo, MeshBatch);

					RayTracingInstance.Materials.Add(MeshBatch);
				}
			#if WITH_EDITORONLY_DATA
				MeshObject->SectionIndexPreview = SectionIndexPreview;
				MeshObject->MaterialIndexPreview = MaterialIndexPreview;
			#endif
				if (bAnySegmentUsesWorldPositionOffset)
				{
					RayTracingInstance.InstanceTransforms.Add(FMatrix::Identity);
				}
				else
				{
					RayTracingInstance.InstanceTransforms.Add(GetLocalToWorld());
				}

				if (bAnySegmentUsesWorldPositionOffset)
				{
					TArray<FRayTracingGeometrySegment> GeometrySections;
					GeometrySections.Reserve(LODData.RenderSections.Num());
					for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
					{
						FRayTracingGeometrySegment Segment;
						Segment.FirstPrimitive = Section.BaseIndex / 3;
						Segment.NumPrimitives = Section.NumTriangles;
						Segment.bEnabled = !Section.bDisabled;
						GeometrySections.Add(Segment);
					}
					MeshObject->GetRayTracingGeometry()->Initializer.Segments = GeometrySections;

					Context.DynamicRayTracingGeometriesToUpdate.Add(
						FRayTracingDynamicGeometryUpdateParams
						{
							RayTracingInstance.Materials,
							false,
							LODData.GetNumVertices(),
							LODData.GetNumVertices() * (uint32)sizeof(FVector),
							MeshObject->GetRayTracingGeometry()->Initializer.TotalPrimitiveCount,
							MeshObject->GetRayTracingGeometry(),
							MeshObject->GetRayTracingDynamicVertexBuffer(),
							true
						}
					);
				}

			RayTracingInstance.BuildInstanceMaskAndFlags();

			OutRayTracingInstances.Add(RayTracingInstance);
		}
	}
}
#endif // RHI_RAYTRACING

SIZE_T FSkeletalMeshSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

bool FSkeletalMeshSceneProxy::HasDynamicIndirectShadowCasterRepresentation() const
{
	return CastsDynamicShadow() && CastsDynamicIndirectShadow();
}

void FSkeletalMeshSceneProxy::GetShadowShapes(TArray<FCapsuleShape>& CapsuleShapes) const 
{
	SCOPE_CYCLE_COUNTER(STAT_GetShadowShapes);

	const TArray<FMatrix>& ReferenceToLocalMatrices = MeshObject->GetReferenceToLocalMatrices();
	const FMatrix& ProxyLocalToWorld = GetLocalToWorld();

	int32 CapsuleIndex = CapsuleShapes.Num();
	CapsuleShapes.SetNum(CapsuleShapes.Num() + ShadowCapsuleData.Num(), false);

	for(const TPair<int32, FCapsuleShape>& CapsuleData : ShadowCapsuleData)
	{
		FMatrix ReferenceToWorld = ReferenceToLocalMatrices[CapsuleData.Key] * ProxyLocalToWorld;
		const float MaxScale = ReferenceToWorld.GetScaleVector().GetMax();

		FCapsuleShape& NewCapsule = CapsuleShapes[CapsuleIndex++];

		NewCapsule.Center = ReferenceToWorld.TransformPosition(CapsuleData.Value.Center);
		NewCapsule.Radius = CapsuleData.Value.Radius * MaxScale;
		NewCapsule.Orientation = ReferenceToWorld.TransformVector(CapsuleData.Value.Orientation).GetSafeNormal();
		NewCapsule.Length = CapsuleData.Value.Length * MaxScale;
	}
}

/**
 * Returns the world transform to use for drawing.
 * @param OutLocalToWorld - Will contain the local-to-world transform when the function returns.
 * @param OutWorldToLocal - Will contain the world-to-local transform when the function returns.
 */
bool FSkeletalMeshSceneProxy::GetWorldMatrices( FMatrix& OutLocalToWorld, FMatrix& OutWorldToLocal ) const
{
	OutLocalToWorld = GetLocalToWorld();
	if (OutLocalToWorld.GetScaledAxis(EAxis::X).IsNearlyZero(SMALL_NUMBER) &&
		OutLocalToWorld.GetScaledAxis(EAxis::Y).IsNearlyZero(SMALL_NUMBER) &&
		OutLocalToWorld.GetScaledAxis(EAxis::Z).IsNearlyZero(SMALL_NUMBER))
	{
		return false;
	}
	OutWorldToLocal = GetLocalToWorld().InverseFast();
	return true;
}

/**
 * Relevance is always dynamic for skel meshes unless they are disabled
 */
FPrimitiveViewRelevance FSkeletalMeshSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View) && View->Family->EngineShowFlags.SkeletalMeshes;
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bStaticRelevance = bRenderStatic && !IsRichView(*View->Family);
	Result.bDynamicRelevance = !Result.bStaticRelevance;
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
	
	MaterialRelevance.SetPrimitiveViewRelevance(Result);

#if !UE_BUILD_SHIPPING
	Result.bSeparateTranslucency |= View->Family->EngineShowFlags.Constraints;
#endif

#if WITH_EDITOR
	//only check these in the editor
	if (Result.bStaticRelevance)
	{
		Result.bEditorStaticSelectionRelevance = (IsSelected() || IsHovered());
	}
#endif

	Result.bVelocityRelevance = IsMovable() && Result.bOpaque && Result.bRenderInMainPass;

	return Result;
}

bool FSkeletalMeshSceneProxy::CanBeOccluded() const
{
	return !MaterialRelevance.bDisableDepthTest && !ShouldRenderCustomDepth();
}

bool FSkeletalMeshSceneProxy::IsUsingDistanceCullFade() const
{
	return MaterialRelevance.bUsesDistanceCullFade;
}

/** Util for getting LOD index currently used by this SceneProxy. */
int32 FSkeletalMeshSceneProxy::GetCurrentLODIndex()
{
	if(MeshObject)
	{
		return MeshObject->GetLOD();
	}
	else
	{
		return 0;
	}
}


/** 
 * Render physics asset for debug display
 */
void FSkeletalMeshSceneProxy::DebugDrawPhysicsAsset(int32 ViewIndex, FMeshElementCollector& Collector, const FEngineShowFlags& EngineShowFlags) const
{
	FMatrix ProxyLocalToWorld, WorldToLocal;
	if (!GetWorldMatrices(ProxyLocalToWorld, WorldToLocal))
	{
		return; // Cannot draw this, world matrix not valid
	}

	FMatrix ScalingMatrix = ProxyLocalToWorld;
	FVector TotalScale = ScalingMatrix.ExtractScaling();

	// Only if valid
	if( !TotalScale.IsNearlyZero() )
	{
		FTransform LocalToWorldTransform(ProxyLocalToWorld);

		TArray<FTransform>* BoneSpaceBases = MeshObject->GetComponentSpaceTransforms();
		if(BoneSpaceBases)
		{
			//TODO: These data structures are not double buffered. This is not thread safe!
			check(PhysicsAssetForDebug);
			if (EngineShowFlags.Collision && IsCollisionEnabled())
			{
				PhysicsAssetForDebug->GetCollisionMesh(ViewIndex, Collector, SkeletalMeshForDebug, *BoneSpaceBases, LocalToWorldTransform, TotalScale);
			}
			if (EngineShowFlags.Constraints)
			{
				PhysicsAssetForDebug->DrawConstraints(ViewIndex, Collector, SkeletalMeshForDebug, *BoneSpaceBases, LocalToWorldTransform, TotalScale.X);
			}
		}
	}
}

void FSkeletalMeshSceneProxy::DebugDrawSkeleton(int32 ViewIndex, FMeshElementCollector& Collector, const FEngineShowFlags& EngineShowFlags) const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	FMatrix ProxyLocalToWorld, WorldToLocal;
	if (!GetWorldMatrices(ProxyLocalToWorld, WorldToLocal))
	{
		return; // Cannot draw this, world matrix not valid
	}

	FTransform LocalToWorldTransform(ProxyLocalToWorld);

	auto MakeRandomColorForSkeleton = [](uint32 InUID)
	{
		FRandomStream Stream((int32)InUID);
		const uint8 Hue = (uint8)(Stream.FRand()*255.f);
		return FLinearColor::MakeFromHSV8(Hue, 255, 255);
	};

	FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
	TArray<FTransform>& ComponentSpaceTransforms = *MeshObject->GetComponentSpaceTransforms();

	for (int32 Index = 0; Index < ComponentSpaceTransforms.Num(); ++Index)
	{
		const int32 ParentIndex = SkeletalMeshForDebug->GetRefSkeleton().GetParentIndex(Index);
		FVector Start, End;
		
		FLinearColor LineColor = DebugDrawColor.Get(MakeRandomColorForSkeleton(GetPrimitiveComponentId().PrimIDValue));
		const FTransform Transform = ComponentSpaceTransforms[Index] * LocalToWorldTransform;

		if (ParentIndex >= 0)
		{
			Start = (ComponentSpaceTransforms[ParentIndex] * LocalToWorldTransform).GetLocation();
			End = Transform.GetLocation();
		}
		else
		{
			Start = LocalToWorldTransform.GetLocation();
			End = Transform.GetLocation();
		}

		if(EngineShowFlags.Bones || bDrawDebugSkeleton)
		{
			if(CVarDebugDrawSimpleBones.GetValueOnRenderThread() != 0)
			{
				PDI->DrawLine(Start, End, LineColor, SDPG_Foreground, 0.0f, 1.0f);
			}
			else
			{
				SkeletalDebugRendering::DrawWireBone(PDI, Start, End, LineColor, SDPG_Foreground);
			}

			if(CVarDebugDrawBoneAxes.GetValueOnRenderThread() != 0)
			{
				SkeletalDebugRendering::DrawAxes(PDI, Transform, SDPG_Foreground);
			}
		}
	}
#endif
}

/**
* Updates morph material usage for materials referenced by each LOD entry
*
* @param bNeedsMorphUsage - true if the materials used by this skeletal mesh need morph target usage
*/
void FSkeletalMeshSceneProxy::UpdateMorphMaterialUsage_GameThread(TArray<UMaterialInterface*>& MaterialUsingMorphTarget)
{
	bool bNeedsMorphUsage = MaterialUsingMorphTarget.Num() > 0;
	if( bNeedsMorphUsage != bMaterialsNeedMorphUsage_GameThread )
	{
		// keep track of current morph material usage for the proxy
		bMaterialsNeedMorphUsage_GameThread = bNeedsMorphUsage;

		TSet<UMaterialInterface*> MaterialsToSwap;
		for (auto It = MaterialsInUse_GameThread.CreateConstIterator(); It; ++It)
		{
			UMaterialInterface* Material = *It;
			if (Material)
			{
				const bool bCheckSkelUsage = Material->CheckMaterialUsage_Concurrent(MATUSAGE_SkeletalMesh);
				if (!bCheckSkelUsage)
				{
					MaterialsToSwap.Add(Material);
				}
				else if(MaterialUsingMorphTarget.Contains(Material))
				{
					const bool bCheckMorphUsage = !bMaterialsNeedMorphUsage_GameThread || (bMaterialsNeedMorphUsage_GameThread && Material->CheckMaterialUsage_Concurrent(MATUSAGE_MorphTargets));
					// make sure morph material usage and default skeletal usage are both valid
					if (!bCheckMorphUsage)
					{
						MaterialsToSwap.Add(Material);
					}
				}
			}
		}

		// update the new LODSections on the render thread proxy
		if (MaterialsToSwap.Num())
		{
			TSet<UMaterialInterface*> InMaterialsToSwap = MaterialsToSwap;
			UMaterialInterface* DefaultMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
			ERHIFeatureLevel::Type InFeatureLevel = GetScene().GetFeatureLevel();
			FSkeletalMeshSceneProxy* SkelMeshSceneProxy = this;
			FMaterialRelevance DefaultRelevance = DefaultMaterial->GetRelevance(InFeatureLevel);
			ENQUEUE_RENDER_COMMAND(UpdateSkelProxyLODSectionElementsCmd)(
				[InMaterialsToSwap, DefaultMaterial, DefaultRelevance, InFeatureLevel, SkelMeshSceneProxy](FRHICommandList& RHICmdList)
				{
					for( int32 LodIdx=0; LodIdx < SkelMeshSceneProxy->LODSections.Num(); LodIdx++ )
					{
						FLODSectionElements& LODSection = SkelMeshSceneProxy->LODSections[LodIdx];
						for( int32 SectIdx=0; SectIdx < LODSection.SectionElements.Num(); SectIdx++ )
						{
							FSectionElementInfo& SectionElement = LODSection.SectionElements[SectIdx];
							if( InMaterialsToSwap.Contains(SectionElement.Material) )
							{
								// fallback to default material if needed
								SectionElement.Material = DefaultMaterial;
							}
						}
					}
					SkelMeshSceneProxy->MaterialRelevance |= DefaultRelevance;
				});
		}
	}
}

#if WITH_EDITORONLY_DATA

bool FSkeletalMeshSceneProxy::GetPrimitiveDistance(int32 LODIndex, int32 SectionIndex, const FVector& ViewOrigin, float& PrimitiveDistance) const
{

	if (FPrimitiveSceneProxy::GetPrimitiveDistance(LODIndex, SectionIndex, ViewOrigin, PrimitiveDistance))
	{
		const float OneOverDistanceMultiplier = 1.f / FMath::Max<float>(SMALL_NUMBER, StreamingDistanceMultiplier);
		PrimitiveDistance *= OneOverDistanceMultiplier;
		return true;
	}
	return false;
}

bool FSkeletalMeshSceneProxy::GetMeshUVDensities(int32 LODIndex, int32 SectionIndex, FVector4& WorldUVDensities) const
{
	if (LODSections.IsValidIndex(LODIndex) && LODSections[LODIndex].SectionElements.IsValidIndex(SectionIndex))
	{
		// The LOD-section data is stored per material index as it is only used for texture streaming currently.
		const int32 MaterialIndex = LODSections[LODIndex].SectionElements[SectionIndex].UseMaterialIndex;
		if (SkeletalMeshRenderData && SkeletalMeshRenderData->UVChannelDataPerMaterial.IsValidIndex(MaterialIndex))
		{
			const float TransformScale = GetLocalToWorld().GetMaximumAxisScale();
			const float* LocalUVDensities = SkeletalMeshRenderData->UVChannelDataPerMaterial[MaterialIndex].LocalUVDensities;

			WorldUVDensities.Set(
				LocalUVDensities[0] * TransformScale,
				LocalUVDensities[1] * TransformScale,
				LocalUVDensities[2] * TransformScale,
				LocalUVDensities[3] * TransformScale);
			
			return true;
	}
		}
	return FPrimitiveSceneProxy::GetMeshUVDensities(LODIndex, SectionIndex, WorldUVDensities);
	}

bool FSkeletalMeshSceneProxy::GetMaterialTextureScales(int32 LODIndex, int32 SectionIndex, const FMaterialRenderProxy* MaterialRenderProxy, FVector4* OneOverScales, FIntVector4* UVChannelIndices) const
	{
	if (LODSections.IsValidIndex(LODIndex) && LODSections[LODIndex].SectionElements.IsValidIndex(SectionIndex))
	{
		const UMaterialInterface* Material = LODSections[LODIndex].SectionElements[SectionIndex].Material;
		if (Material)
		{
			// This is thread safe because material texture data is only updated while the renderthread is idle.
			for (const FMaterialTextureInfo& TextureData : Material->GetTextureStreamingData())
			{
				const int32 TextureIndex = TextureData.TextureIndex;
				if (TextureData.IsValid(true))
				{
					OneOverScales[TextureIndex / 4][TextureIndex % 4] = 1.f / TextureData.SamplingScale;
					UVChannelIndices[TextureIndex / 4][TextureIndex % 4] = TextureData.UVChannelIndex;
				}
			}
			for (const FMaterialTextureInfo& TextureData : Material->TextureStreamingDataMissingEntries)
			{
				const int32 TextureIndex = TextureData.TextureIndex;
				if (TextureIndex >= 0 && TextureIndex < TEXSTREAM_MAX_NUM_TEXTURES_PER_MATERIAL)
				{
					OneOverScales[TextureIndex / 4][TextureIndex % 4] = 1.f;
					UVChannelIndices[TextureIndex / 4][TextureIndex % 4] = 0;
				}
			}
			return true;
		}
	}
	return false;
}
#endif

void FSkeletalMeshSceneProxy::OnTransformChanged()
{
	// OnTransformChanged is called on the following frame after FSkeletalMeshObject::Update(), thus omit '+ 1' to frame number.
	MeshObject->RefreshClothingTransforms(GetLocalToWorld(), GetScene().GetFrameNumber());
}



FSkinnedMeshComponentRecreateRenderStateContext::FSkinnedMeshComponentRecreateRenderStateContext(USkeletalMesh* InSkeletalMesh, bool InRefreshBounds /*= false*/)
	: bRefreshBounds(InRefreshBounds)
{
	for (TObjectIterator<USkinnedMeshComponent> It; It; ++It)
	{
		if (It->SkeletalMesh == InSkeletalMesh)
		{
			checkf(!It->IsUnreachable(), TEXT("%s"), *It->GetFullName());

			if (It->IsRenderStateCreated())
			{
				check(It->IsRegistered());
				It->DestroyRenderState_Concurrent();
				MeshComponents.Add(*It);
			}
		}
	}

	// Flush the rendering commands generated by the detachments.
	// The static mesh scene proxies reference the UStaticMesh, and this ensures that they are cleaned up before the UStaticMesh changes.
	FlushRenderingCommands();
}

FSkinnedMeshComponentRecreateRenderStateContext::~FSkinnedMeshComponentRecreateRenderStateContext()
{
	const int32 ComponentCount = MeshComponents.Num();
	for (int32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
	{
		USkinnedMeshComponent* Component = MeshComponents[ComponentIndex];

		if (bRefreshBounds)
		{
			Component->UpdateBounds();
		}

		if (Component->IsRegistered() && !Component->IsRenderStateCreated())
		{
			Component->CreateRenderState_Concurrent(nullptr);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

FVector GetRefVertexLocationTyped(
	const USkeletalMesh* Mesh,
	const FSkelMeshRenderSection& Section,
	const FPositionVertexBuffer& PositionBuffer,
	const FSkinWeightVertexBuffer& SkinWeightVertexBuffer,
	const int32 VertIndex
)
{
	FVector SkinnedPos(0, 0, 0);

	// Do soft skinning for this vertex.
	int32 BufferVertIndex = Section.GetVertexBufferIndex() + VertIndex;
	int32 MaxBoneInfluences = SkinWeightVertexBuffer.GetMaxBoneInfluences();

#if !PLATFORM_LITTLE_ENDIAN
	// uint8[] elements in LOD.VertexBufferGPUSkin have been swapped for VET_UBYTE4 vertex stream use
	for (int32 InfluenceIndex = MAX_INFLUENCES - 1; InfluenceIndex >= MAX_INFLUENCES - MaxBoneInfluences; InfluenceIndex--)
#else
	for (int32 InfluenceIndex = 0; InfluenceIndex < MaxBoneInfluences; InfluenceIndex++)
#endif
	{
		const int32 MeshBoneIndex = Section.BoneMap[SkinWeightVertexBuffer.GetBoneIndex(BufferVertIndex, InfluenceIndex)];
		const float	Weight = (float)SkinWeightVertexBuffer.GetBoneWeight(BufferVertIndex, InfluenceIndex) / 255.0f;
		{
			const FMatrix BoneTransformMatrix = FMatrix::Identity;//Mesh->GetComposedRefPoseMatrix(MeshBoneIndex);
			const FMatrix RefToLocal = Mesh->GetRefBasesInvMatrix()[MeshBoneIndex] * BoneTransformMatrix;

			SkinnedPos += BoneTransformMatrix.TransformPosition(PositionBuffer.VertexPosition(BufferVertIndex)) * Weight;
		}
	}

	return SkinnedPos;
}

FVector GetSkeletalMeshRefVertLocation(const USkeletalMesh* Mesh, const FSkeletalMeshLODRenderData& LODData, const FSkinWeightVertexBuffer& SkinWeightVertexBuffer, const int32 VertIndex)
{
	int32 SectionIndex;
	int32 VertIndexInChunk;
	LODData.GetSectionFromVertexIndex(VertIndex, SectionIndex, VertIndexInChunk);
	const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
	return GetRefVertexLocationTyped(Mesh, Section, LODData.StaticVertexBuffers.PositionVertexBuffer, SkinWeightVertexBuffer, VertIndexInChunk);
}

//GetRefTangentBasisTyped
void GetRefTangentBasisTyped(const USkeletalMesh* Mesh, const FSkelMeshRenderSection& Section, const FStaticMeshVertexBuffer& StaticVertexBuffer, const FSkinWeightVertexBuffer& SkinWeightVertexBuffer, const int32 VertIndex, FVector& OutTangentX, FVector& OutTangentY, FVector& OutTangentZ)
{
	OutTangentX = FVector::ZeroVector;
	OutTangentY = FVector::ZeroVector;
	OutTangentZ = FVector::ZeroVector;

	// Do soft skinning for this vertex.
	const int32 BufferVertIndex = Section.GetVertexBufferIndex() + VertIndex;
	const int32 MaxBoneInfluences = SkinWeightVertexBuffer.GetMaxBoneInfluences();

	const FVector VertexTangentX = StaticVertexBuffer.VertexTangentX(BufferVertIndex);
	const FVector VertexTangentY = StaticVertexBuffer.VertexTangentY(BufferVertIndex);
	const FVector VertexTangentZ = StaticVertexBuffer.VertexTangentZ(BufferVertIndex);

#if !PLATFORM_LITTLE_ENDIAN
	// uint8[] elements in LOD.VertexBufferGPUSkin have been swapped for VET_UBYTE4 vertex stream use
	for (int32 InfluenceIndex = MAX_INFLUENCES - 1; InfluenceIndex >= MAX_INFLUENCES - MaxBoneInfluences; InfluenceIndex--)
#else
	for (int32 InfluenceIndex = 0; InfluenceIndex < MaxBoneInfluences; InfluenceIndex++)
#endif
	{
		const int32 MeshBoneIndex = Section.BoneMap[SkinWeightVertexBuffer.GetBoneIndex(BufferVertIndex, InfluenceIndex)];
		const float	Weight = (float)SkinWeightVertexBuffer.GetBoneWeight(BufferVertIndex, InfluenceIndex) / 255.0f;
		const FMatrix BoneTransformMatrix = FMatrix::Identity;//Mesh->GetComposedRefPoseMatrix(MeshBoneIndex);
		//const FMatrix RefToLocal = Mesh->RefBasesInvMatrix[MeshBoneIndex] * BoneTransformMatrix;
		OutTangentX += BoneTransformMatrix.TransformVector(VertexTangentX) * Weight;
		OutTangentY += BoneTransformMatrix.TransformVector(VertexTangentY) * Weight;
		OutTangentZ += BoneTransformMatrix.TransformVector(VertexTangentZ) * Weight;
	}
}

void GetSkeletalMeshRefTangentBasis(const USkeletalMesh* Mesh, const FSkeletalMeshLODRenderData& LODData, const FSkinWeightVertexBuffer& SkinWeightVertexBuffer, const int32 VertIndex, FVector& OutTangentX, FVector& OutTangentY, FVector& OutTangentZ)
{
	int32 SectionIndex;
	int32 VertIndexInChunk;
	LODData.GetSectionFromVertexIndex(VertIndex, SectionIndex, VertIndexInChunk);
	const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
	GetRefTangentBasisTyped(Mesh, Section, LODData.StaticVertexBuffers.StaticMeshVertexBuffer, SkinWeightVertexBuffer, VertIndexInChunk, OutTangentX, OutTangentY, OutTangentZ);
}

#undef LOCTEXT_NAMESPACE
