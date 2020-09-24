// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomAsset.h"
#include "EngineUtils.h"
#include "GroomAssetImportData.h"
#include "GroomBuilder.h"
#include "HairCardsBuilder.h"
#include "GroomImportOptions.h"
#include "GroomSettings.h"
#include "RenderingThread.h"
#include "Engine/AssetUserData.h"
#include "HairStrandsVertexFactory.h"
#include "Misc/Paths.h"
#include "Serialization/LargeMemoryReader.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/AnimObjectVersion.h"
#include "UObject/PhysicsObjectVersion.h"
#include "UObject/ReleaseObjectVersion.h"
#include "NiagaraSystem.h"
#include "GroomComponent.h"
#include "Math/Box.h"
#include "Engine/StaticMesh.h"


#if WITH_EDITORONLY_DATA
#include "DerivedDataCacheInterface.h"
#include "EditorFramework/AssetImportData.h"
#endif

// Disabled pending some questions with the cards texture
#define ENABLE_CARDS_SERIALIZATION 0

static int32 GHairStrandsLoadAsset = 1;
static FAutoConsoleVariableRef CVarHairStrandsLoadAsset(TEXT("r.HairStrands.LoadAsset"), GHairStrandsLoadAsset, TEXT("Allow groom asset to be loaded"));

bool IsHairStrandsAssetLoadingEnable()
{
	return GHairStrandsLoadAsset > 0;
}

static float ComputeGroomBoundRadius(const TArray<FHairGroupData>& HairGroupsData)
{
	// Compute the bounding box of all the groups. This is used for scaling LOD sceensize 
		// for each group & cluster respectively to their relative size
	FVector GroomBoundMin(FLT_MAX);
	FVector GroomBoundMax(-FLT_MAX);
	for (const FHairGroupData& LocalGroupData : HairGroupsData)
	{
		GroomBoundMin.X = FMath::Min(GroomBoundMin.X, LocalGroupData.Strands.Data.BoundingBox.Min.X);
		GroomBoundMin.Y = FMath::Min(GroomBoundMin.Y, LocalGroupData.Strands.Data.BoundingBox.Min.Y);
		GroomBoundMin.Z = FMath::Min(GroomBoundMin.Z, LocalGroupData.Strands.Data.BoundingBox.Min.Z);

		GroomBoundMax.X = FMath::Max(GroomBoundMax.X, LocalGroupData.Strands.Data.BoundingBox.Max.X);
		GroomBoundMax.Y = FMath::Max(GroomBoundMax.Y, LocalGroupData.Strands.Data.BoundingBox.Max.Y);
		GroomBoundMax.Z = FMath::Max(GroomBoundMax.Z, LocalGroupData.Strands.Data.BoundingBox.Max.Z);
	}

	const float GroomBoundRadius = FVector::Distance(GroomBoundMax, GroomBoundMin) * 0.5f;
	return GroomBoundRadius;
}

/////////////////////////////////////////////////////////////////////////////////////////

void UGroomAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID); // Needed to support MeshDescription AttributesSet serialization

	if (Ar.CustomVer(FPhysicsObjectVersion::GUID) >= FPhysicsObjectVersion::GroomWithDescription)
	{
		FStripDataFlags StripFlags(Ar);
		if (StripFlags.IsEditorDataStripped() || (Ar.IsSaving() && !CanRebuildFromDescription()))
		{
			// When cooking data or serializing old format to new format,
			// serialize the computed groom data
			Ar << HairGroupsData;
		}
#if WITH_EDITORONLY_DATA
		else
		{
			// When serializing data for editor, serialize the HairDescription as bulk data
			// The computed groom data is fetched from the Derived Data Cache
			if (!HairDescriptionBulkData)
			{
				// When loading, bulk data can be null so instantiate a new one to serialize into
				HairDescriptionBulkData = MakeUnique<FHairDescriptionBulkData>();
			}

			HairDescriptionBulkData->Serialize(Ar, this);

			// Serialize the HairGroupsData directly into the asset if it couldn't be cached in the DDC
			if (!bIsCacheable)
			{
				Ar << HairGroupsData;
			}
		}
#endif // WITH_EDITORONLY_DATA
	}
	else
	{
		// Old format serialized the computed groom data directly
		Ar << HairGroupsData;
	}
}

UGroomAsset::UGroomAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsInitialized = false;
}

bool UGroomAsset::HasGeometryType(uint32 GroupIndex, EGroomGeometryType Type) const
{
	check(GroupIndex < uint32(HairGroupsLOD.Num()));
	const uint32 LODCount = HairGroupsLOD[GroupIndex].LODs.Num();
	for (uint32 LODIt=0;LODIt<LODCount;++LODIt)
	{
		if (HairGroupsLOD[GroupIndex].LODs[LODIt].GeometryType == Type)
			return true;
	}
	return false;
}

bool UGroomAsset::HasGeometryType(EGroomGeometryType Type) const
{
	const uint32 GroupCount = HairGroupsLOD.Num();
	for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
	{
		if (HasGeometryType(GroupIt, Type))
			return true;
	}
	return false;
}

void UGroomAsset::InitResource()
{
	bIsInitialized = true;
	for (uint32 GroupIndex = 0, GroupCount = GetNumHairGroups(); GroupIndex < GroupCount; ++GroupIndex)
	{
		check(GroupIndex < uint32(GetNumHairGroups()));
		FHairGroupData& GroupData = HairGroupsData[GroupIndex];

		GroupData.Strands.RestResource = new FHairStrandsRestResource(GroupData.Strands.Data.RenderData, GroupData.Strands.Data.BoundingBox.GetCenter());
		BeginInitResource(GroupData.Strands.RestResource);

		GroupData.Guides.RestResource = new FHairStrandsRestResource(GroupData.Guides.Data.RenderData, GroupData.Guides.Data.BoundingBox.GetCenter());
		BeginInitResource(GroupData.Guides.RestResource);

		GroupData.Strands.ClusterCullingResource = new FHairStrandsClusterCullingResource(GroupData.Strands.Data, ComputeGroomBoundRadius(HairGroupsData), HairGroupsLOD[GroupIndex]);
		BeginInitResource(GroupData.Strands.ClusterCullingResource);

		GroupData.Strands.InterpolationResource = new FHairStrandsInterpolationResource(GroupData.Strands.InterpolationData.RenderData, GroupData.Guides.Data);
		BeginInitResource(GroupData.Strands.InterpolationResource);
	}

#if WITH_EDITORONLY_DATA
	InitCardsResources();
#endif
}

enum EGroomAssetChangeType
{
	GroomChangeType_Interpolation = 1,
	GroomChangeType_Cards = 2,
	GroomChangeType_Meshes = 4,
	GroomChangeType_LOD = 8
};


inline void InternalUpdateResource(FRenderResource* Resource)
{
	if (Resource)
	{
		BeginUpdateResourceRHI(Resource);
	}
}

template<typename T>
inline void InternalReleaseResource(T*& Resource)
{
	if (Resource)
	{
		T* InResource = Resource;
		ENQUEUE_RENDER_COMMAND(ReleaseHairResourceCommand)(
			[InResource](FRHICommandList& RHICmdList)
			{
				InResource->ReleaseResource();
				delete InResource;
			});
		Resource = nullptr;
	}
}

void UGroomAsset::UpdateResource()
{
#if WITH_EDITOR
	const float GroomBoundRadius = ComputeGroomBoundRadius(HairGroupsData);

	uint32 AllChangeType = 0;
	for (uint32 GroupIndex = 0, GroupCount = GetNumHairGroups(); GroupIndex < GroupCount; ++GroupIndex)
	{
		uint32 ChangeType = (CachedHairGroupsLOD[GroupIndex] == HairGroupsLOD[GroupIndex] ? 0 : GroomChangeType_LOD);
		AllChangeType = AllChangeType | ChangeType;

		check(GroupIndex < uint32(GetNumHairGroups()));
		FHairGroupData& GroupData = HairGroupsData[GroupIndex];
		InternalUpdateResource(GroupData.Strands.RestResource);
		InternalUpdateResource(GroupData.Guides.RestResource);
		InternalUpdateResource(GroupData.Strands.InterpolationResource);

		if (ChangeType & GroomChangeType_LOD)
		{
			GroupData.Strands.ClusterCullingResource = new FHairStrandsClusterCullingResource(GroupData.Strands.Data, GroomBoundRadius, HairGroupsLOD[GroupIndex]);
			BeginInitResource(GroupData.Strands.ClusterCullingResource);
		}
		else
		{
			InternalUpdateResource(GroupData.Strands.ClusterCullingResource);
		}
	}

	if (BuildCardsGeometry())  { AllChangeType |= GroomChangeType_Cards;  }
	if (BuildMeshesGeometry()) { AllChangeType |= GroomChangeType_Meshes; }

	if (AllChangeType & (GroomChangeType_LOD | GroomChangeType_Cards | GroomChangeType_Meshes))
	{
		OnGroomAssetResourcesChanged.Broadcast();
	}

	UpdateHairGroupsInfo();
	UpdateCachedSettings();
#endif // #if WITH_EDITOR
}

void UGroomAsset::ReleaseResource()
{
	bIsInitialized = false;
	for (uint32 GroupIndex = 0, GroupCount = GetNumHairGroups(); GroupIndex < GroupCount; ++GroupIndex)
	{
		FHairGroupData& GroupData = HairGroupsData[GroupIndex];
		InternalReleaseResource(GroupData.Strands.RestResource);
		InternalReleaseResource(GroupData.Guides.RestResource);
		InternalReleaseResource(GroupData.Strands.ClusterCullingResource);
		InternalReleaseResource(GroupData.Strands.InterpolationResource);

		for (FHairGroupData::FCards::FLOD& LOD : GroupData.Cards.LODs)
		{
			InternalReleaseResource(LOD.RestResource);
			InternalReleaseResource(LOD.ProceduralResource);
		}
		for (FHairGroupData::FMeshes::FLOD& LOD : GroupData.Meshes.LODs)
		{
			InternalReleaseResource(LOD.RestResource);
		}
	}

}

void UGroomAsset::UpdateHairGroupsInfo()
{
	const uint32 GroupCount = GetNumHairGroups();
	const bool bForceReset = HairGroupsInfo.Num() != GroupCount;
	HairGroupsInfo.SetNum(GroupCount);

	uint32 GroupIndex = 0;
	for (FHairGroupData& Data: HairGroupsData)
	{
		FHairGroupInfoWithVisibility& Info = HairGroupsInfo[GroupIndex];
		Info.GroupID = GroupIndex;
		Info.NumCurves = Data.Strands.Data.GetNumCurves();
		Info.NumGuides = Data.Guides.Data.GetNumCurves();
		Info.NumCurveVertices = Data.Strands.Data.GetNumPoints();
		Info.NumGuideVertices = Data.Guides.Data.GetNumPoints();
		if (bForceReset)
		{
			Info.bIsVisible = true;
		}
		++GroupIndex;
	}
}

void UGroomAsset::PostLoad()
{
	Super::PostLoad();

	// Legacy asset are missing rendering or interpolation settings
#if WITH_EDITORONLY_DATA
	const bool bIsLegacyAsset = HairGroupsInterpolation.Num() == 0;
	if (bIsLegacyAsset)
	{
		if (HairDescriptionBulkData)
		{
			FProcessedHairDescription ProcessedHairDescription;
			if (!HairDescription)
			{
				HairDescription = MakeUnique<FHairDescription>();
				HairDescriptionBulkData->LoadHairDescription(*HairDescription);
			}
			const bool bValidDescription = FGroomBuilder::ProcessHairDescription(*HairDescription, ProcessedHairDescription);
			check(bValidDescription);

			const uint32 GroupCount = ProcessedHairDescription.HairGroups.Num();
			SetNumGroup(GroupCount);
		}
		else
		{
			const uint32 GroupCount = HairGroupsData.Num();
			SetNumGroup(GroupCount, false);
		}
	}
#endif

#if WITH_EDITORONLY_DATA
	bool bSucceed = bIsCacheable;
	if (bIsCacheable)
	{
		// Interpolation settings are used for building the interpolation data, and per se defined the number of groups
		const uint32 GroupCount = HairGroupsInterpolation.Num();
		if (uint32(GetNumHairGroups()) != GroupCount)
		{
			SetNumGroup(GroupCount);
		}

		bSucceed = UGroomAsset::CacheDerivedDatas();
	}
#else
	bool bSucceed = false;
#endif

	check(GetNumHairGroups() > 0);

	// Build hair strands if needed
	if (!bSucceed)
	{
		const uint32 GroupCount = HairGroupsInterpolation.Num();
		for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
		{
			const bool bNeedToBuildData = 
				HairGroupsData[GroupIndex].Guides.Data.GetNumCurves() == 0 || 
				HairGroupsData[GroupIndex].Strands.InterpolationData.Num() == 0;
			if (bNeedToBuildData)
			{
				FGroomBuilder::BuildData(HairGroupsData[GroupIndex], HairGroupsInterpolation[GroupIndex], GroupIndex );
			}
		}
	}

	if (!IsTemplate() && IsHairStrandsAssetLoadingEnable())
	{
		InitResource();
#if WITH_EDITORONLY_DATA
#if !ENABLE_CARDS_SERIALIZATION
		BuildCardsGeometry();
#endif
		BuildMeshesGeometry();
#endif // #if WITH_EDITORONLY_DATA
	}

	check(AreGroupsValid());

	UpdateHairGroupsInfo();
#if WITH_EDITORONLY_DATA
	UpdateCachedSettings();
#endif // #if WITH_EDITORONLY_DATA
}

void UGroomAsset::PreSave(const class ITargetPlatform* TargetPlatform)
{
#if WITH_EDITORONLY_DATA
	check(AreGroupsValid());

	const uint32 GroupCount = GetNumHairGroups();
	uint32 ChangeType = 0;

	for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
	{
		if (!(CachedHairGroupsInterpolation[GroupIt] == HairGroupsInterpolation[GroupIt]))
		{
			ChangeType = ChangeType | GroomChangeType_Interpolation;
			break;
		}
	}

	for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
	{
		if (!(CachedHairGroupsLOD[GroupIt] == HairGroupsLOD[GroupIt]))
		{
			ChangeType = ChangeType | GroomChangeType_LOD;
			break;
		}
	}

	if (ChangeType & GroomChangeType_Interpolation)
	{
		if (!HairDescription)
		{
			HairDescription = MakeUnique<FHairDescription>();
			HairDescriptionBulkData->LoadHairDescription(*HairDescription);
		}
		FProcessedHairDescription ProcessedHairDescription;
		const bool bValidDescription = FGroomBuilder::ProcessHairDescription(*HairDescription, ProcessedHairDescription);
		check(bValidDescription);

		FGroomComponentRecreateRenderStateContext RecreateRenderContext(this);
		for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
		{
			const bool bHasChanged = !(CachedHairGroupsInterpolation[GroupIt] == HairGroupsInterpolation[GroupIt]);
			if (bHasChanged)
			{
				FGroomBuilder::BuildGroom(ProcessedHairDescription, this, GroupIt);
			}
		}
		InitResource();
	}
	else if (ChangeType & GroomChangeType_LOD)
	{
		InitResource();
	}

	BuildCardsGeometry();
	BuildMeshesGeometry();
	UpdateHairGroupsInfo();
	UpdateCachedSettings();
#endif
}
void UGroomAsset::BeginDestroy()
{
	ReleaseResource();
	Super::BeginDestroy();
}

#if WITH_EDITOR
void UGroomAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	const FName PropertyName = PropertyThatChanged ? PropertyThatChanged->GetFName() : NAME_None;
	const bool bGeometryTypeChanged = PropertyName == GET_MEMBER_NAME_CHECKED(FHairLODSettings, GeometryType);
	if (bGeometryTypeChanged)
	{
		// If the asset didn't have any mesh or cards, we need to create/allocate the cards/mesh groups
		const uint32 GroupCount = HairGroupsData.Num();
		if (IsHairCardsEnable() && HasGeometryType(EGroomGeometryType::Cards) && HairGroupsCards.Num() == 0)
		{
			HairGroupsCards.Init(FHairGroupsCardsSourceDescription(), GroupCount);
			FHairGroupsCardsSourceDescription Dirty;
			Dirty.ProceduralSettings.ClusterSettings.ClusterDecimation = 0;
			CachedHairGroupsCards.Init(Dirty, GroupCount);
		}

		if (IsHairMeshesEnable() && HasGeometryType(EGroomGeometryType::Meshes) && HairGroupsMeshes.Num() == 0)
		{
			HairGroupsMeshes.Init(FHairGroupsMeshesSourceDescription(), GroupCount);
			FHairGroupsMeshesSourceDescription Dirty;
			Dirty.ImportedMesh = nullptr;
			CachedHairGroupsMeshes.Init(Dirty, GroupCount);
		}
	}

	UpdateResource();

	if (bGeometryTypeChanged)
	{
		OnGroomAssetResourcesChanged.Broadcast();
	}
	else
	{
		OnGroomAssetChanged.Broadcast();
	}
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
void UGroomAsset::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	if (AssetImportData)
	{
		OutTags.Add(FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden));
	}

	Super::GetAssetRegistryTags(OutTags);
}

void UGroomAsset::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}

	Super::PostInitProperties();
}
#endif

int32 UGroomAsset::GetNumHairGroups() const
{
	return HairGroupsData.Num();
}

#if ENABLE_CARDS_SERIALIZATION
FArchive& operator<<(FArchive& Ar, FHairGroupData::FCards::FLOD& CardLODData)
{
	Ar << CardLODData.Data;
	Ar << CardLODData.ProceduralData;
	Ar << CardLODData.InterpolationData;

	CardLODData.Guides.Data.Serialize(Ar);
	CardLODData.Guides.InterpolationData.Serialize(Ar);

	return Ar;
}
#endif

FArchive& operator<<(FArchive& Ar, FHairGroupData& GroupData)
{
#if ENABLE_CARDS_SERIALIZATION
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);
#endif

	GroupData.Strands.Data.Serialize(Ar);
	GroupData.Guides.Data.Serialize(Ar);
	GroupData.Strands.InterpolationData.Serialize(Ar);

#if ENABLE_CARDS_SERIALIZATION
	if (Ar.CustomVer(FAnimObjectVersion::GUID) >= FAnimObjectVersion::SerializeGroomCards)
	{
		Ar << GroupData.Cards.LODs;
	}
#endif

	return Ar;
}

void UGroomAsset::AddAssetUserData(UAssetUserData* InUserData)
{
	if (InUserData != nullptr)
	{
		UAssetUserData* ExistingData = GetAssetUserDataOfClass(InUserData->GetClass());
		if (ExistingData != nullptr)
		{
			AssetUserData.Remove(ExistingData);
		}
		AssetUserData.Add(InUserData);
	}
}

UAssetUserData* UGroomAsset::GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (int32 DataIdx = 0; DataIdx < AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if (Datum != nullptr && Datum->IsA(InUserDataClass))
		{
			return Datum;
		}
	}
	return NULL;
}

void UGroomAsset::RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (int32 DataIdx = 0; DataIdx < AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if (Datum != nullptr && Datum->IsA(InUserDataClass))
		{
			AssetUserData.RemoveAt(DataIdx);
			return;
		}
	}
}

const TArray<UAssetUserData*>* UGroomAsset::GetAssetUserDataArray() const
{
	return &AssetUserData;
}

bool UGroomAsset::CanRebuildFromDescription() const
{
#if WITH_EDITORONLY_DATA
	return HairDescriptionBulkData.IsValid() && !HairDescriptionBulkData->IsEmpty();
#else
	return false;
#endif
}

bool UGroomAsset::AreGroupsValid() const
{
	const uint32 GroupCount = HairGroupsInterpolation.Num();
	return
		GroupCount > 0 &&
		HairGroupsData.Num() == GroupCount &&
		HairGroupsPhysics.Num() == GroupCount &&
		HairGroupsRendering.Num() == GroupCount &&
		HairGroupsLOD.Num() == GroupCount;
}

void UGroomAsset::SetNumGroup(uint32 InGroupCount, bool bResetGroupData)
{
	ReleaseResource();
	if (bResetGroupData)
	{
		HairGroupsData.Reset();
	}

	// In order to preserve the existing asset settings, the settings are only reset if the group count has changed.
	if (InGroupCount != HairGroupsData.Num())
	{
		HairGroupsData.Init(FHairGroupData(), InGroupCount);
	}

	if (InGroupCount != HairGroupsPhysics.Num())
	{
		HairGroupsPhysics.Init(FHairGroupsPhysics(), InGroupCount);
	}

	if (InGroupCount != HairGroupsRendering.Num())
	{
		HairGroupsRendering.Init(FHairGroupsRendering(), InGroupCount);
	}

	if (InGroupCount != HairGroupsInterpolation.Num())
	{
		HairGroupsInterpolation.Init(FHairGroupsInterpolation(), InGroupCount);
	}

	if (InGroupCount != HairGroupsLOD.Num())
	{
		HairGroupsLOD.Init(FHairGroupsLOD(), InGroupCount);

		// Insure that each group has at least one LOD
		for (FHairGroupsLOD& GroupLOD : HairGroupsLOD)
		{
			FHairLODSettings& S = GroupLOD.LODs.AddDefaulted_GetRef();
			S.ScreenSize = 1;
			S.CurveDecimation = 1;
		}
	}
}

// If groom derived data needs to be rebuilt (new format, serialization
// differences, etc.) replace the version GUID below with a new one.
// In case of merge conflicts with DDC versions, you MUST generate a new GUID
// and set this new GUID as the version.
#define GROOM_DERIVED_DATA_VERSION TEXT("7AA52FFE75B54EC7A36CC989E5854A29")

#if WITH_EDITORONLY_DATA

namespace GroomDerivedDataCacheUtils
{
	const FString& GetGroomDerivedDataVersion()
	{
		static FString CachedVersionString(GROOM_DERIVED_DATA_VERSION);
		return CachedVersionString;
	}

	FString BuildGroomDerivedDataKey(const FString& KeySuffix)
	{
		return FDerivedDataCacheInterface::BuildCacheKey(TEXT("GROOM"), *GetGroomDerivedDataVersion(), *KeySuffix);
	}

	void SerializeHairInterpolationSettingsForDDC(FArchive& Ar, uint32 GroupIndex, FHairGroupsInterpolation& BuildSettings)
	{
		// Note: this serializer is only used to build the groom DDC key, no versioning is required
		Ar << GroupIndex;
		Ar << BuildSettings.DecimationSettings.CurveDecimation;
		Ar << BuildSettings.DecimationSettings.VertexDecimation;
		Ar << BuildSettings.InterpolationSettings.bOverrideGuides;
		Ar << BuildSettings.InterpolationSettings.HairToGuideDensity;
		Ar << BuildSettings.InterpolationSettings.InterpolationQuality;
		Ar << BuildSettings.InterpolationSettings.InterpolationDistance;
		Ar << BuildSettings.InterpolationSettings.bRandomizeGuide;
		Ar << BuildSettings.InterpolationSettings.bUseUniqueGuide;
	}
}

FString UGroomAsset::BuildDerivedDataKeySuffix(uint32 GroupIndex, const FHairGroupsInterpolation& BuildSettings)
{
	// Serialize the build settings into a temporary array
	// The archive is flagged as persistent so that machines of different endianness produce identical binary results.
	TArray<uint8> TempBytes;
	TempBytes.Reserve(64);
	FMemoryWriter Ar(TempBytes, /*bIsPersistent=*/ true);

	GroomDerivedDataCacheUtils::SerializeHairInterpolationSettingsForDDC(Ar, GroupIndex, const_cast<FHairGroupsInterpolation&>(BuildSettings));

	FString KeySuffix;
	if (HairDescriptionBulkData)
	{
		// Reserve twice the size of TempBytes because of ByteToHex below + 3 for "ID" and \0
		KeySuffix.Reserve(HairDescriptionBulkData->GetIdString().Len() + TempBytes.Num() * 2 + 3);
		KeySuffix += TEXT("ID");
		KeySuffix += HairDescriptionBulkData->GetIdString();
	}
	else
	{
		KeySuffix.Reserve(TempBytes.Num() * 2 + 1);
	}

	// Now convert the raw bytes to a string
	const uint8* SettingsAsBytes = TempBytes.GetData();
	for (int32 ByteIndex = 0; ByteIndex < TempBytes.Num(); ++ByteIndex)
	{
		ByteToHex(SettingsAsBytes[ByteIndex], KeySuffix);
	}

	return KeySuffix;
}

void UGroomAsset::CommitHairDescription(FHairDescription&& InHairDescription)
{
	HairDescription = MakeUnique<FHairDescription>(InHairDescription);

	if (!HairDescriptionBulkData)
	{
		HairDescriptionBulkData = MakeUnique<FHairDescriptionBulkData>();
	}
	HairDescriptionBulkData->SaveHairDescription(*HairDescription);
}

FHairDescription UGroomAsset::GetHairDescription() const
{
	FHairDescription OutHairDescription;
	if (HairDescriptionBulkData)
	{
		HairDescriptionBulkData->LoadHairDescription(OutHairDescription);
	}
	return MoveTemp(OutHairDescription);
}

bool UGroomAsset::CacheDerivedDatas()
{
	FGroomComponentRecreateRenderStateContext RecreateContext(this);

	FProcessedHairDescription ProcessedHairDescription;
	const uint32 GroupCount = HairGroupsInterpolation.Num();
	for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
	{
		bool bSucceed = CacheDerivedData(GroupIndex, ProcessedHairDescription);
		if (!bSucceed)
			return false;
	}
	UpdateHairGroupsInfo();
	InitResource();
	return true;
}

bool UGroomAsset::CacheDerivedData(uint32 GroupIndex, FProcessedHairDescription& ProcessedHairDescription)
{
	// Check if the asset correctly initialized prior building
	if (!AreGroupsValid())
	{
		return false;
	}

	if (!HairDescriptionBulkData)
	{
		return false;
	}
	
	const uint32 GroupCount = HairGroupsInterpolation.Num();
	check(GroupIndex < GroupCount);
	if (GroupIndex >= GroupCount)
	{
		return false;
	}
	const FHairGroupsInterpolation& BuildSettings = HairGroupsInterpolation[GroupIndex];

	bool bSuccess = true;

	const FString KeySuffix = BuildDerivedDataKeySuffix(GroupIndex, BuildSettings);
	const FString DerivedDataKey = GroomDerivedDataCacheUtils::BuildGroomDerivedDataKey(KeySuffix);

	TArray<uint8> DerivedData;
	if (GetDerivedDataCacheRef().GetSynchronous(*DerivedDataKey, DerivedData, GetPathName()))
	{
		FMemoryReader Ar(DerivedData, /*bIsPersistent=*/ true);

		int64 UncompressedSize = 0;
		Ar << UncompressedSize;

		uint8* DecompressionBuffer = reinterpret_cast<uint8*>(FMemory::Malloc(UncompressedSize));
		Ar.SerializeCompressed(DecompressionBuffer, 0, NAME_Zlib);

		FHairGroupData& HairGroupData = HairGroupsData[GroupIndex];
		FLargeMemoryReader LargeMemReader(DecompressionBuffer, UncompressedSize, ELargeMemoryReaderFlags::Persistent | ELargeMemoryReaderFlags::TakeOwnership);
		LargeMemReader << HairGroupData;
	}
	else
	{
		// Load the HairDescription from the bulk data if needed
		if (!HairDescription)
		{
			HairDescription = MakeUnique<FHairDescription>();
			HairDescriptionBulkData->LoadHairDescription(*HairDescription);
		}

		if (!ProcessedHairDescription.IsValid())
		{
			if (!FGroomBuilder::ProcessHairDescription(*HairDescription, ProcessedHairDescription))
			{
				return false;
			}
		}

		// Build groom data with the new build settings
		bSuccess = FGroomBuilder::BuildGroom(ProcessedHairDescription, this, GroupIndex);

		FHairGroupData& HairGroupData = HairGroupsData[GroupIndex];
		if (bSuccess)
		{
#if ENABLE_CARDS_SERIALIZATION
			// Build cards geometry here so that it gets serialized and cached in the DDC
			BuildCardsGeometry(GroupIndex);
#endif

			// Using a LargeMemoryWriter for serialization since the data can be bigger than 2 GB
			FLargeMemoryWriter LargeMemWriter(0, /*bIsPersistent=*/ true);
			LargeMemWriter << HairGroupData;

			int64 UncompressedSize = LargeMemWriter.TotalSize();

			// Then the content of the LargeMemWriter is compressed into a MemoryWriter
			// Compression ratio can reach about 5:2 depending on the data
			// Since the DDC doesn't support data bigger than 2 GB
			// we can compute a size threshold to skip the caching when
			// the uncompressed size exceeds the threshold
			static constexpr const int64 SizeThreshold = (int64)MAX_int32 * 2.5;
			bIsCacheable = UncompressedSize < SizeThreshold;
			if (bIsCacheable)
			{
				FMemoryWriter CompressedArchive(DerivedData, true);

				CompressedArchive << UncompressedSize; // needed for allocating decompression buffer
				CompressedArchive.SerializeCompressed(LargeMemWriter.GetData(), UncompressedSize, NAME_Zlib);

				GetDerivedDataCacheRef().Put(*DerivedDataKey, DerivedData, GetPathName());
			}
		}
	}

#if !ENABLE_CARDS_SERIALIZATION
	BuildCardsGeometry();
#endif
	BuildMeshesGeometry();
	UpdateCachedSettings();

	return bSuccess;
}

template<typename T>
static const T* GetSourceDescription(const TArray<T>& InHairGroups,	uint32 GroupIndex, uint32 LODIndex, int32& SourceIndex)
{
	SourceIndex = 0;
	for (const T& SourceDesc : InHairGroups)
	{
		if (SourceDesc.GroupIndex == GroupIndex && SourceDesc.LODIndex == LODIndex)
		{
			return &SourceDesc;
		}
		++SourceIndex;
	}
	SourceIndex = -1;
	return nullptr;
}

bool UGroomAsset::BuildCardsGeometry(uint32 GroupIndex)
{
	if (!IsHairCardsEnable() || HairGroupsCards.Num() == 0)
	{
		return false;
	}

	bool bHasChanged = HairGroupsCards.Num() != CachedHairGroupsCards.Num();

	check(GroupIndex < uint32(GetNumHairGroups()));
	FHairGroupData& GroupData = HairGroupsData[GroupIndex];

	// The settings might have been previously cached without the data having been built
	bool bIsAlreadyBuilt = false;
	const uint32 LODCount = HairGroupsLOD[GroupIndex].LODs.Num();
	for (uint32 LODIt = 0; LODIt < LODCount; ++LODIt)
	{
		int32 SourceIt = 0;
		if (const FHairGroupsCardsSourceDescription* Desc = GetSourceDescription(HairGroupsCards, GroupIndex, LODIt, SourceIt))
		{
			bIsAlreadyBuilt = GroupData.Strands.Data.GetNumPoints() > 0 && Desc->CardsInfo.NumCardVertices > 0;
			break;
		}
	}

	bHasChanged |= !bIsAlreadyBuilt;

	if (!bHasChanged)
	{
		for (uint32 SourceIt = 0, SourceCount = HairGroupsCards.Num(); SourceIt < SourceCount; ++SourceIt)
		{
			const bool bEquals = CachedHairGroupsCards[SourceIt] == HairGroupsCards[SourceIt];
			if (!bEquals)
			{
				bHasChanged = true;
				break;
			}
		}
	}

	if (!bHasChanged)
	{
		return false;
	}

	GroupData.Cards.LODs.SetNum(LODCount);
	for (uint32 LODIt = 0; LODIt < LODCount; ++LODIt)
	{
		int32 SourceIt = 0;
		if (const FHairGroupsCardsSourceDescription* Desc = GetSourceDescription(HairGroupsCards, GroupIndex, LODIt, SourceIt))
		{
			// #hair_todo: add finer culling granularity to rebuild only what is necessary
			const FHairGroupsCardsSourceDescription* CachedDesc = SourceIt < CachedHairGroupsCards.Num() ? &CachedHairGroupsCards[SourceIt] : nullptr;
			const bool bLODHasChanged = CachedDesc == nullptr || !(*CachedDesc == *Desc);
			if (!bLODHasChanged && bIsAlreadyBuilt)
			{
				continue;
			}

			FHairGroupData::FCards::FLOD& LOD = GroupData.Cards.LODs[LODIt];

			InternalReleaseResource(LOD.RestResource);
			InternalReleaseResource(LOD.ProceduralResource);

			if (Desc->SourceType == EHairCardsSourceType::Procedural &&
				GroupData.Strands.Data.GetNumPoints() > 0		// hair group data might not have been built yet
				)
			{
				FHairCardsBuilder::BuildGeometry(
					GroupData.Strands.Data,
					GroupData.Guides.Data,
					Desc->ProceduralSettings,
					LOD.ProceduralData,
					LOD.Guides.Data,
					LOD.InterpolationData);
				FHairCardsBuilder::Convert(LOD.ProceduralData, LOD.Data);

				LOD.RestResource = new FHairCardsRestResource(
					LOD.Data.RenderData,
					LOD.Data.Cards.GetNumVertices(),
					LOD.Data.Cards.GetNumTriangles());
				BeginInitResource(LOD.RestResource);

				LOD.ProceduralResource = new FHairCardsProceduralResource(
					LOD.ProceduralData.RenderData,
					LOD.ProceduralData.Atlas.Resolution,
					LOD.ProceduralData.Voxels);
				BeginInitResource(LOD.ProceduralResource);

				LOD.InterpolationResource = new FHairCardsInterpolationResource(LOD.InterpolationData.RenderData);
				BeginInitResource(LOD.InterpolationResource);

				FHairCardsBuilder::BuildTextureAtlas(&LOD.ProceduralData, LOD.RestResource, LOD.ProceduralResource);

				// Create own interpolation settings for cards.
				// Force closest guides as this is the most relevant matching metric for cards, due to their coarse geometry
				FHairInterpolationSettings CardsInterpolationSettings = HairGroupsInterpolation[GroupIndex].InterpolationSettings;
				CardsInterpolationSettings.bOverrideGuides = false;
				CardsInterpolationSettings.bUseUniqueGuide = true;
				CardsInterpolationSettings.bRandomizeGuide = false;
				CardsInterpolationSettings.InterpolationDistance = EHairInterpolationWeight::Parametric;
				CardsInterpolationSettings.InterpolationQuality = EHairInterpolationQuality::Low;

				FGroomBuilder::BuildData(
					LOD.Guides.Data,
					GroupData.Guides.Data,
					LOD.Guides.InterpolationData,
					CardsInterpolationSettings,
					true,
					false,
					true,
					GroupIndex);

				LOD.Guides.RestResource = new FHairStrandsRestResource(LOD.Guides.Data.RenderData, LOD.Guides.Data.BoundingBox.GetCenter());
				BeginInitResource(LOD.Guides.RestResource);

				LOD.Guides.InterpolationResource = new FHairStrandsInterpolationResource(LOD.Guides.InterpolationData.RenderData, GroupData.Guides.Data);
				BeginInitResource(LOD.Guides.InterpolationResource);

				// Update card stats to display
				Desc->CardsInfo.NumCardVertices = LOD.Data.Cards.GetNumVertices();
				Desc->CardsInfo.NumCards = LOD.ProceduralData.Cards.GetNum();
			}
			else if (Desc->SourceType == EHairCardsSourceType::Imported)
			{
				// TODO
				Desc->CardsInfo.NumCardVertices = 0;
				Desc->CardsInfo.NumCards = 0;
			}
		}
	}

	return true;
}

bool UGroomAsset::BuildCardsGeometry()
{
	for (uint32 GroupIndex = 0, GroupCount = GetNumHairGroups(); GroupIndex < GroupCount; ++GroupIndex)
	{
		BuildCardsGeometry(GroupIndex);
	}

	return true;
}

void UGroomAsset::InitCardsResources()
{
#if ENABLE_CARDS_SERIALIZATION
	if (!IsHairCardsEnable() || HairGroupsCards.Num() == 0)
	{
		return;
	}

	for (uint32 GroupIndex = 0, GroupCount = GetNumHairGroups(); GroupIndex < GroupCount; ++GroupIndex)
	{
		FHairGroupData& GroupData = HairGroupsData[GroupIndex];

		const uint32 LODCount = HairGroupsLOD[GroupIndex].LODs.Num();
		GroupData.Cards.LODs.SetNum(LODCount);
		for (uint32 LODIt = 0; LODIt < LODCount; ++LODIt)
		{
			int32 SourceIt = 0;
			if (const FHairGroupsCardsSourceDescription* Desc = GetSourceDescription(HairGroupsCards, GroupIndex, LODIt, SourceIt))
			{
				FHairGroupData::FCards::FLOD& LOD = GroupData.Cards.LODs[LODIt];

				if (Desc->SourceType == EHairCardsSourceType::Procedural &&
					LOD.RestResource == nullptr &&			// don't initialize again if they were previously initialized during the BuildCardsGeometry
					LOD.Data.Cards.GetNumVertices() > 0)	
				{
					LOD.RestResource = new FHairCardsRestResource(
						LOD.Data.RenderData,
						LOD.Data.Cards.GetNumVertices(),
						LOD.Data.Cards.GetNumTriangles());
					BeginInitResource(LOD.RestResource);

					LOD.ProceduralResource = new FHairCardsProceduralResource(
						LOD.ProceduralData.RenderData,
						LOD.ProceduralData.Atlas.Resolution,
						LOD.ProceduralData.Voxels);
					BeginInitResource(LOD.ProceduralResource);

					LOD.InterpolationResource = new FHairCardsInterpolationResource(LOD.InterpolationData.RenderData);
					BeginInitResource(LOD.InterpolationResource);

					FHairCardsBuilder::BuildTextureAtlas(&LOD.ProceduralData, LOD.RestResource, LOD.ProceduralResource);

					LOD.Guides.RestResource = new FHairStrandsRestResource(LOD.Guides.Data.RenderData, LOD.Guides.Data.BoundingBox.GetCenter());
					BeginInitResource(LOD.Guides.RestResource);

					LOD.Guides.InterpolationResource = new FHairStrandsInterpolationResource(LOD.Guides.InterpolationData.RenderData, GroupData.Guides.Data);
					BeginInitResource(LOD.Guides.InterpolationResource);

					// Update card stats to display
					Desc->CardsInfo.NumCardVertices = LOD.Data.Cards.GetNumVertices();
					Desc->CardsInfo.NumCards = LOD.ProceduralData.GetNum();
				}
			}
		}
	}
#endif
}

bool UGroomAsset::BuildMeshesGeometry()
{
	if (!IsHairMeshesEnable() || HairGroupsMeshes.Num() == 0)
	{
		return false;
	}

	bool bHasChanged = HairGroupsMeshes.Num() != CachedHairGroupsMeshes.Num();
	if (!bHasChanged)
	{
		for (uint32 SourceIt = 0, SourceCount = HairGroupsMeshes.Num(); SourceIt < SourceCount; ++SourceIt)
		{
			const bool bEquals = CachedHairGroupsMeshes[SourceIt] == HairGroupsMeshes[SourceIt];
			if (!bEquals)
			{
				bHasChanged = true;
				break;
			}
		}
	}

	if (!bHasChanged)
	{
		return false;
	}

	for (uint32 GroupIndex = 0, GroupCount = GetNumHairGroups(); GroupIndex < GroupCount; ++GroupIndex)
	{
		const uint32 LODCount = HairGroupsLOD[GroupIndex].LODs.Num();
		check(GroupIndex < uint32(GetNumHairGroups()));
		FHairGroupData& GroupData = HairGroupsData[GroupIndex];

		GroupData.Meshes.LODs.SetNum(LODCount);
		for (uint32 LODIt = 0; LODIt < LODCount; ++LODIt)
		{
			int32 SourceIt = 0;
			if (const FHairGroupsMeshesSourceDescription* Desc = GetSourceDescription(HairGroupsMeshes, GroupIndex, LODIt, SourceIt))
			{
				const FHairGroupsMeshesSourceDescription* CachedDesc = SourceIt < CachedHairGroupsMeshes.Num() ? &CachedHairGroupsMeshes[SourceIt] : nullptr;
				const bool bLODHasChanged = CachedDesc == nullptr || !(*CachedDesc == *Desc);
				if (!bLODHasChanged)
				{
					continue;
				}

				FHairGroupData::FMeshes::FLOD& LOD = GroupData.Meshes.LODs[LODIt];
				InternalReleaseResource(LOD.RestResource);

				if (Desc->ImportedMesh)
				{
					Desc->ImportedMesh->ConditionalPostLoad();
					FHairMeshesBuilder::ImportGeometry(
						Desc->ImportedMesh,
						LOD.Data);
				}
				else
				{
					// Build a default box
					FHairMeshesBuilder::BuildGeometry(
						GroupData.Strands.Data,
						GroupData.Guides.Data,
						LOD.Data);
				}

				LOD.RestResource = new FHairMeshesResource(
					LOD.Data.RenderData,
					LOD.Data.Meshes.GetNumVertices(),
					LOD.Data.Meshes.GetNumTriangles());
				BeginInitResource(LOD.RestResource);
			}
		}
	}

	return true;
}

void UGroomAsset::UpdateCachedSettings()
{
#if WITH_EDITORONLY_DATA
	CachedHairGroupsRendering		= HairGroupsRendering;
	CachedHairGroupsPhysics			= HairGroupsPhysics;
	CachedHairGroupsInterpolation	= HairGroupsInterpolation;
	CachedHairGroupsLOD				= HairGroupsLOD;
	CachedHairGroupsCards			= HairGroupsCards;
	CachedHairGroupsMeshes			= HairGroupsMeshes;
#endif
}
#endif // WITH_EDITORONLY_DATA

bool FProcessedHairDescription::IsValid() const
{
	for (TPair<int32, FProcessedHairDescription::FHairGroup> HairGroupIt : HairGroups)
	{
		const FProcessedHairDescription::FHairGroup& Group = HairGroupIt.Value;
		const FHairGroupInfo& GroupInfo = Group.Key;
		if (GroupInfo.NumCurves == 0)
		{
			return false;
		}
	}

	return 	HairGroups.Num() > 0;
}

int32 UGroomAsset::GetLODCount() const
{
	int32 MaxLODCount = -1;
	for (const FHairGroupsLOD& S : HairGroupsLOD)
	{
		MaxLODCount = FMath::Max(MaxLODCount, S.LODs.Num());
	}
	return MaxLODCount;
}

void UGroomAsset::StripLODs(const TArray<int32>& LODsToKeep, bool bRebuildResources)
{
	// Assume that the LOD are ordered from 0 ... Max
	// Export all LODs if the list is empty or has the same number of LODs
	if (LODsToKeep.Num() == GetLODCount() || LODsToKeep.Num() == 0)
	{
		return;
	}

	const int32 GroupCount = HairGroupsLOD.Num();
	int32 LODsTOKeepIndex = LODsToKeep.Num()-1;

	// Remove the LOD settings prior to rebuild the LOD data
	const int32 LODCount = GetLODCount();
	for (int32 LODIt = LODCount-1; LODIt > 0; --LODIt)
	{
		if (LODIt == LODsToKeep[LODsTOKeepIndex])
		{
			continue;
		}

		for (int32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
		{
			HairGroupsLOD[GroupIt].LODs.RemoveAt(LODIt);
		}

		--LODsTOKeepIndex;
	}

	// Rebuild the LOD data
	if (bRebuildResources)
	{
		const float GroomBoundRadius = ComputeGroomBoundRadius(HairGroupsData);
		for (int32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
		{
			FHairGroupData& GroupData = HairGroupsData[GroupIt];
			GroupData.Strands.ClusterCullingResource = new FHairStrandsClusterCullingResource(GroupData.Strands.Data, GroomBoundRadius, HairGroupsLOD[GroupIt]);
			BeginInitResource(GroupData.Strands.ClusterCullingResource);
		}
	}
}

bool UGroomAsset::HasDebugData() const
{
	for (uint32 GroupIndex = 0, GroupCount = GetNumHairGroups(); GroupIndex < GroupCount; ++GroupIndex)
	{
		if (!HairGroupsData[GroupIndex].Debug.Data.IsValid())
		{
			return false;
		}
	}

	return true;
}

void UGroomAsset::CreateDebugData()
{
	for (uint32 GroupIndex = 0, GroupCount = GetNumHairGroups(); GroupIndex < GroupCount; ++GroupIndex)
	{
		FHairGroupData& GroupData = HairGroupsData[GroupIndex];
		CreateHairStrandsDebugDatas(GroupData.Strands.Data, 1.f, GroupData.Debug.Data);

		if (GroupData.Debug.Data.IsValid())
		{
			GroupData.Debug.Resource = new FHairStrandsDebugDatas::FResources();

			FHairStrandsDebugDatas* InData = &GroupData.Debug.Data;
			FHairStrandsDebugDatas::FResources* InResource = GroupData.Debug.Resource;
			ENQUEUE_RENDER_COMMAND(HairStrandsDebugResourceCommand)(
				[InData, InResource](FRHICommandListImmediate& RHICmdList)
				{
					FRDGBuilder GraphBuilder(RHICmdList);
					CreateHairStrandsDebugResources(GraphBuilder, InData, InResource);
					GraphBuilder.Execute();
				});
		}
	}
}
