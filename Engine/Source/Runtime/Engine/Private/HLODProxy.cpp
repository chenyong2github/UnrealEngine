// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/HLODProxy.h"
#include "Engine/LODActor.h"
#include "GameFramework/WorldSettings.h"

#if WITH_EDITOR
#include "Misc/ConfigCacheIni.h"
#include "Serialization/ArchiveObjectCrc32.h"
#include "HierarchicalLOD.h"
#include "ObjectTools.h"
#endif
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/Texture.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "LevelUtils.h"
#include "Engine/LevelStreaming.h"
#include "Math/UnrealMathUtility.h"

#if WITH_EDITOR

void UHLODProxy::SetMap(const UWorld* InMap)
{
	OwningMap = InMap;
}

TSoftObjectPtr<UWorld> UHLODProxy::GetMap() const
{
	return OwningMap;
}

UHLODProxyDesc* UHLODProxy::AddLODActor(ALODActor* InLODActor)
{
	check(InLODActor->ProxyDesc == nullptr);

	// Create a new HLODProxyDesc and populate it from the provided InLODActor.
	UHLODProxyDesc* HLODProxyDesc = NewObject<UHLODProxyDesc>(this);
	HLODProxyDesc->UpdateFromLODActor(InLODActor);

	InLODActor->Proxy = this;
	InLODActor->ProxyDesc = HLODProxyDesc;
	InLODActor->bBuiltFromHLODDesc = true;

	HLODActors.Emplace(HLODProxyDesc);

	MarkPackageDirty();

	return HLODProxyDesc;
}

void UHLODProxy::AddMesh(ALODActor* InLODActor, UStaticMesh* InStaticMesh, const FName& InKey)
{
	// If the Save LOD Actors to HLOD packages feature is enabled, ensure that if a LODActor hasn't been rebuilt yet with
	// the feature on that we can still update its mesh properly.
	if (GetDefault<UHierarchicalLODSettings>()->bSaveLODActorsToHLODPackages && HLODActors.Contains(InLODActor->ProxyDesc))
	{
		check(InLODActor->Proxy == this);
		HLODActors[InLODActor->ProxyDesc] = FHLODProxyMesh(InStaticMesh, InKey);
		InLODActor->UpdateProxyDesc();
	}
	else
	{
		InLODActor->Proxy = this;
		FHLODProxyMesh NewProxyMesh(InLODActor, InStaticMesh, InKey);
		ProxyMeshes.AddUnique(NewProxyMesh);
	}
}

void UHLODProxy::Clean()
{
	// The level we reference must be loaded to clean this package
	check(OwningMap.IsNull() || OwningMap.ToSoftObjectPath().ResolveObject() != nullptr);

	// Remove all entries that have invalid actors
	int32 NumRemoved = ProxyMeshes.RemoveAll([this](const FHLODProxyMesh& InProxyMesh)
	{ 
		TLazyObjectPtr<ALODActor> LODActor = InProxyMesh.GetLODActor();

		bool bRemoveEntry = false;

		// Invalid actor means that it has been deleted so we shouldnt hold onto its data
		if(!LODActor.IsValid())
		{
			bRemoveEntry = true;
		}
		else if(LODActor.Get()->Proxy == nullptr)
		{
			// No proxy means we are also invalid
			bRemoveEntry = true;
		}
		else if(!LODActor.Get()->Proxy->ContainsDataForActor(LODActor.Get()))
		{
			// actor and proxy are valid, but key differs (unbuilt)
			bRemoveEntry = true;
		}

		if (bRemoveEntry)
		{
			RemoveAssets(InProxyMesh);
		}

		return bRemoveEntry;
	});

	// Ensure the HLOD descs are up to date.
	if (GetDefault<UHierarchicalLODSettings>()->bSaveLODActorsToHLODPackages)
	{
		UWorld* World = Cast<UWorld>(OwningMap.ToSoftObjectPath().ResolveObject());
		if (World)
		{
			UpdateHLODDescs(World->PersistentLevel);
		}
	}
	else if (HLODActors.Num() > 0)
	{
		for (TMap<UHLODProxyDesc*, FHLODProxyMesh>::TIterator ItHLODActor = HLODActors.CreateIterator(); ItHLODActor; ++ItHLODActor)
		{
			RemoveAssets(ItHLODActor.Value());
		}

		HLODActors.Empty();
		Modify();
	}
}

bool UHLODProxy::IsEmpty() const
{
	return HLODActors.Num() == 0 && ProxyMeshes.Num() == 0;
}

void UHLODProxy::DeletePackage()
{
	UPackage* Package = GetOutermost();

	// Must not destroy objects during iteration, so gather a list
	TArray<UObject*> ObjectsToDestroy;
	ForEachObjectWithOuter(Package, [&ObjectsToDestroy](UObject* InObject)
	{
		ObjectsToDestroy.Add(InObject);
	});

	// Perform destruction
	for (UObject* ObjectToDestroy : ObjectsToDestroy)
	{
		DestroyObject(ObjectToDestroy);
	}

	ObjectTools::DeleteObjectsUnchecked({ Package });
}

void UHLODProxy::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

	if (!OwningMap.IsValid())
	{
		return;
	}

	// Always rebuild key on save here.
	// We don't do this while cooking as keys rely on platform derived data which is context-dependent during cook
	if (!GIsCookerLoadingPackage)
	{
		if (GetDefault<UHierarchicalLODSettings>()->bSaveLODActorsToHLODPackages)
		{
			if (UWorld* World = Cast<UWorld>(OwningMap.ToSoftObjectPath().ResolveObject()))
			{
				for (AActor* Actor : World->PersistentLevel->Actors)
				{
					if (ALODActor* LODActor = Cast<ALODActor>(Actor))
					{
						if (LODActor->ProxyDesc && LODActor->ProxyDesc->GetOutermost() == GetOutermost())
						{
							LODActor->ProxyDesc->Key = UHLODProxy::GenerateKeyForActor(LODActor);
						}
					}
				}
			}
		}
	}
}

void UHLODProxy::UpdateHLODDescs(const ULevel* InLevel)
{
	// Gather a map of all the HLODProxyDescs used by LODActors in the level
	TMap<const UHLODProxyDesc*, ALODActor*> LODActors;
	for (AActor* Actor : InLevel->Actors)
	{
		if (ALODActor* LODActor = Cast<ALODActor>(Actor))
		{
			if (LODActor->ProxyDesc && LODActor->ProxyDesc->GetOutermost() == GetOutermost())
			{
				LODActors.Emplace(LODActor->ProxyDesc, LODActor);
			}
		}
	}

	// For each HLODProxyDesc stored in this proxy, ensure that it is up to date with the associated LODActor
	// Purge the HLODProxyDesc that are unused (not referenced by any LODActor)
	for (TMap<UHLODProxyDesc*, FHLODProxyMesh>::TIterator ItHLODActor = HLODActors.CreateIterator(); ItHLODActor; ++ItHLODActor)
	{
		UHLODProxyDesc* HLODProxyDesc = ItHLODActor.Key();
		ALODActor** LODActor = LODActors.Find(HLODProxyDesc);
		if (LODActor)
		{
			HLODProxyDesc->UpdateFromLODActor(*LODActor);
		}
		else
		{
			// Remove assets associated with this actor
			RemoveAssets(ItHLODActor.Value());

			Modify();
			ItHLODActor.RemoveCurrent();
		}
	}
}

const AActor* UHLODProxy::FindFirstActor(const ALODActor* LODActor)
{
	auto RecursiveFindFirstActor = [&](const ALODActor* InLODActor)
	{
		const AActor* FirstActor = InLODActor->SubActors.IsValidIndex(0) ? InLODActor->SubActors[0] : nullptr;
		while (FirstActor != nullptr && FirstActor->IsA<ALODActor>())
		{
			const ALODActor* SubLODActor = Cast<ALODActor>(FirstActor);
			if (SubLODActor)
			{
				FirstActor = SubLODActor->SubActors.IsValidIndex(0) ? SubLODActor->SubActors[0] : nullptr; 
			}
			else
			{
				// Unable to find a valid actor
				FirstActor = nullptr;
			}
		}
		return FirstActor;
	};

	// Retrieve first 'valid' AActor (non ALODActor)
	const AActor* FirstValidActor = nullptr;

	for (int32 Index = 0; Index < LODActor->SubActors.Num(); ++Index)
	{
		const AActor* SubActor = LODActor->SubActors[Index];

		if (const ALODActor* SubLODActor = Cast<ALODActor>(SubActor))
		{
			SubActor = RecursiveFindFirstActor(SubLODActor);
		}

		if (SubActor != nullptr)
		{
			FirstValidActor = SubActor;
			break;
		}
	}

	return FirstValidActor;
}

void UHLODProxy::ExtractStaticMeshComponentsFromLODActor(const ALODActor* LODActor, TArray<UStaticMeshComponent*>& InOutComponents)
{
	check(LODActor);

	for (const AActor* ChildActor : LODActor->SubActors)
	{
		if(ChildActor)
		{
			TArray<UStaticMeshComponent*> ChildComponents;
			if (ChildActor->IsA<ALODActor>())
			{
				ExtractStaticMeshComponentsFromLODActor(Cast<ALODActor>(ChildActor), ChildComponents);
			}
			else
			{
				ChildActor->GetComponents<UStaticMeshComponent>(ChildComponents);
			}

			InOutComponents.Append(ChildComponents);
		}
	}
}

void UHLODProxy::ExtractComponents(const ALODActor* LODActor, TArray<UPrimitiveComponent*>& InOutComponents)
{
	check(LODActor);

	for (const AActor* Actor : LODActor->SubActors)
	{
		if(Actor)
		{
			TArray<UStaticMeshComponent*> Components;

			if (Actor->IsA<ALODActor>())
			{
				ExtractStaticMeshComponentsFromLODActor(Cast<ALODActor>(Actor), Components);
			}
			else
			{
				Actor->GetComponents<UStaticMeshComponent>(Components);
			}

			Components.RemoveAll([&](UStaticMeshComponent* Val)
			{
				return Val->GetStaticMesh() == nullptr || !Val->ShouldGenerateAutoLOD(LODActor->LODLevel - 1);
			});

			InOutComponents.Append(Components);
		}
	}
}

uint32 UHLODProxy::GetCRC(UMaterialInterface* InMaterialInterface, uint32 InCRC)
{
	TArray<uint8> KeyBuffer;

	UMaterialInterface* MaterialInterface = InMaterialInterface;
	while(MaterialInterface)
	{
		// Walk material parent chain for instances with known states (we cant support MIDs directly as they are always changing)
		if(UMaterialInstance* MI = Cast<UMaterialInstance>(MaterialInterface))
		{
			if(UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(MI))
			{
				KeyBuffer.Append((uint8*)&MIC->ParameterStateId, sizeof(FGuid));
			}
			MaterialInterface = MI->Parent;
		}
		else if(UMaterial* Material = Cast<UMaterial>(MaterialInterface))
		{
			KeyBuffer.Append((uint8*)&Material->StateId, sizeof(FGuid));
			MaterialInterface = nullptr;
		}
	}

	return FCrc::MemCrc32(KeyBuffer.GetData(), KeyBuffer.Num(), InCRC);
}

uint32 UHLODProxy::GetCRC(UTexture* InTexture, uint32 InCRC)
{
	// Default to just the path name if we don't have render data
	 if (InTexture->GetRunningPlatformData() != nullptr)
     {
         FTexturePlatformData* PlatformData = *InTexture->GetRunningPlatformData();
         if (PlatformData != nullptr)
         {
             return FCrc::StrCrc32(*PlatformData->DerivedDataKey, InCRC);
         }
     }
 
     // Default to just the path name if we don't have render data
     return FCrc::StrCrc32(*InTexture->GetPathName(), InCRC);
}

uint32 UHLODProxy::GetCRC(UStaticMesh* InStaticMesh, uint32 InCRC)
{
	TArray<uint8> KeyBuffer;

	// Default to just the path name if we don't have render data
	FString DerivedDataKey = InStaticMesh->GetRenderData() ? InStaticMesh->GetRenderData()->DerivedDataKey : InStaticMesh->GetPathName();
	KeyBuffer.Append((uint8*)DerivedDataKey.GetCharArray().GetData(), DerivedDataKey.GetCharArray().Num() * DerivedDataKey.GetCharArray().GetTypeSize());

	const int32 LightMapCoordinateIndex = InStaticMesh->GetLightMapCoordinateIndex();
	KeyBuffer.Append((uint8*)&LightMapCoordinateIndex, sizeof(int32));
	if(InStaticMesh->GetBodySetup())
	{
		// Incorporate physics data
		KeyBuffer.Append((uint8*)&InStaticMesh->GetBodySetup()->BodySetupGuid, sizeof(FGuid));;
	}
	return FCrc::MemCrc32(KeyBuffer.GetData(), KeyBuffer.Num(), InCRC);
}

static void AppendRoundedTransform(const FRotator& ComponentRotation, const FVector& ComponentLocation, const FVector& ComponentScale, TArray<uint8>& OutKeyBuffer)
{
	// Include transform - round sufficiently to ensure stability
	FIntVector Location(FMath::RoundToInt(ComponentLocation.X), FMath::RoundToInt(ComponentLocation.Y), FMath::RoundToInt(ComponentLocation.Z));
	OutKeyBuffer.Append((uint8*)&Location, sizeof(Location));
	FVector RotationVector(ComponentRotation.GetNormalized().Vector());
	FIntVector Rotation(FMath::RoundToInt(RotationVector.X), FMath::RoundToInt(RotationVector.Y), FMath::RoundToInt(RotationVector.Z));
	OutKeyBuffer.Append((uint8*)&Rotation, sizeof(Rotation));
	FIntVector Scale(FMath::RoundToInt(ComponentScale.X), FMath::RoundToInt(ComponentScale.Y), FMath::RoundToInt(ComponentScale.Z));
	OutKeyBuffer.Append((uint8*)&Scale, sizeof(Scale));
}

static void AppendRoundedTransform(const FTransform& InTransform, TArray<uint8>& OutKeyBuffer)
{
	AppendRoundedTransform(InTransform.Rotator(), InTransform.GetLocation(), InTransform.GetScale3D(), OutKeyBuffer);
}

static int32 GetTransformCRC(const FTransform& InTransform, uint32 InCRC)
{
	TArray<uint8> KeyBuffer;
	AppendRoundedTransform(InTransform, KeyBuffer);
	return FCrc::MemCrc32(KeyBuffer.GetData(), KeyBuffer.Num(), InCRC);
}

uint32 UHLODProxy::GetCRC(UStaticMeshComponent* InComponent, uint32 InCRC, const FTransform& TransformComponents)
{
	TArray<uint8> KeyBuffer;

	FVector  ComponentLocation = InComponent->GetComponentLocation();
	FRotator ComponentRotation = InComponent->GetComponentRotation();
	FVector  ComponentScale = InComponent->GetComponentScale();

	ComponentLocation = TransformComponents.TransformPosition(ComponentLocation);
	ComponentRotation = TransformComponents.TransformRotation(ComponentRotation.Quaternion()).Rotator();
	AppendRoundedTransform(ComponentRotation, ComponentLocation, ComponentScale, KeyBuffer);

	// Include other relevant properties
	KeyBuffer.Append((uint8*)&InComponent->ForcedLodModel, sizeof(int32));
	bool bUseMaxLODAsImposter = InComponent->bUseMaxLODAsImposter;
	KeyBuffer.Append((uint8*)&bUseMaxLODAsImposter, sizeof(bool));
	bool bCastShadow = InComponent->CastShadow;
	KeyBuffer.Append((uint8*)&bCastShadow, sizeof(bool));
	bool bCastStaticShadow = InComponent->bCastStaticShadow;
	KeyBuffer.Append((uint8*)&bCastStaticShadow, sizeof(bool));
	bool bCastDynamicShadow = InComponent->bCastDynamicShadow;
	KeyBuffer.Append((uint8*)&bCastDynamicShadow, sizeof(bool));
	bool bCastFarShadow = InComponent->bCastFarShadow;
	KeyBuffer.Append((uint8*)&bCastFarShadow, sizeof(bool));
	int32 Width, Height;
	InComponent->GetLightMapResolution(Width, Height);
	KeyBuffer.Append((uint8*)&Width, sizeof(int32));
	KeyBuffer.Append((uint8*)&Height, sizeof(int32));
	
	// incorporate vertex colors
	for(FStaticMeshComponentLODInfo& LODInfo : InComponent->LODData)
	{
		if(LODInfo.OverrideVertexColors)
		{
			KeyBuffer.Append((uint8*)LODInfo.OverrideVertexColors->GetVertexData(), LODInfo.OverrideVertexColors->GetNumVertices() * LODInfo.OverrideVertexColors->GetStride());
		}
	}

	return FCrc::MemCrc32(KeyBuffer.GetData(), KeyBuffer.Num(), InCRC);
}

// Key that forms the basis of the HLOD proxy key. Bump this key (i.e. generate a new GUID) when you want to force a rebuild of ALL HLOD proxies
#define HLOD_PROXY_BASE_KEY		TEXT("174C29B19AB34A21894058E058F253B3")

namespace 
{
	class FHLODProxyCRCArchive : public FArchive
	{
	public:
		FHLODProxyCRCArchive()
			: Hash(0)
		{
			SetIsLoading(false);
			SetIsSaving(true);
			SetUseUnversionedPropertySerialization(true);
		}

		uint32 GetHash() const
		{
			return Hash;
		}

	private:
		virtual FArchive& operator<<(class UObject*& Value) override { check(0); return *this; }
		virtual FArchive& operator<<(class FName& Value) override { check(0); return *this; }

		virtual void Serialize(void* Data, int64 Num) override
		{
			Hash = FCrc::MemCrc32(Data, Num, Hash);
		}

		uint32 Hash;
	};
}

FName UHLODProxy::GenerateKeyForActor(const ALODActor* LODActor, bool bMustUndoLevelTransform)
{
	FString Key = HLOD_PROXY_BASE_KEY;

	// Base us off the unique object ID
	{
		const UObject* Obj = LODActor->ProxyDesc ? Cast<const UObject>(LODActor->ProxyDesc) : Cast<const UObject>(LODActor);
		FUniqueObjectGuid ObjectGUID = FUniqueObjectGuid::GetOrCreateIDForObject(Obj);
		Key += TEXT("_");
		Key += ObjectGUID.GetGuid().ToString(EGuidFormats::Digits);
	}

	// Accumulate a bunch of settings into a CRC
	{
		uint32 CRC = 0;

		// Get the HLOD settings CRC
		{
			TArray<FHierarchicalSimplification>& BuildLODLevelSettings = LODActor->GetLevel()->GetWorldSettings()->GetHierarchicalLODSetup();
			if(BuildLODLevelSettings.IsValidIndex(LODActor->LODLevel - 1))
			{
				FHierarchicalSimplification BuildLODLevelSetting = BuildLODLevelSettings[LODActor->LODLevel - 1];
				FHLODProxyCRCArchive Ar;
				FHierarchicalSimplification::StaticStruct()->SerializeItem(Ar, &BuildLODLevelSetting, nullptr);
				CRC = HashCombine(CRC, Ar.GetHash());
			}
		}

		// HLODBakingTransform
		CRC = GetTransformCRC(LODActor->GetLevel()->GetWorldSettings()->HLODBakingTransform, CRC);

		// screen size + override
		{
			if(LODActor->bOverrideScreenSize)
			{
				CRC = FCrc::MemCrc32(&LODActor->ScreenSize, sizeof(float), CRC);
			}
		}

		// material merge settings override
		{
			if (LODActor->bOverrideMaterialMergeSettings)
			{
				FMaterialProxySettings MaterialProxySettings = LODActor->MaterialSettings;
				FHLODProxyCRCArchive Ar;
				FMaterialProxySettings::StaticStruct()->SerializeItem(Ar, &MaterialProxySettings, nullptr);
				CRC = HashCombine(CRC, Ar.GetHash());
			}
		}

		Key += TEXT("_");
		Key += BytesToHex((uint8*)&CRC, sizeof(uint32));
	}

	// get the base material CRC
	{
		UMaterialInterface* BaseMaterial = LODActor->GetLevel()->GetWorldSettings()->GetHierarchicalLODBaseMaterial();
		uint32 CRC = GetCRC(BaseMaterial);
		Key += TEXT("_");
		Key += BytesToHex((uint8*)&CRC, sizeof(uint32));
	}

	// We get the CRC of the first actor name and various static mesh components
	{
		uint32 CRC = 0;
		const AActor* FirstActor = FindFirstActor(LODActor);
		if(FirstActor)
		{
			CRC = FCrc::StrCrc32(*FirstActor->GetName(), CRC);
		}

		TArray<UPrimitiveComponent*> Components;
		ExtractComponents(LODActor, Components);
		
		// Components can be offset by their streaming level transform. Undo that transform to have the same signature
		// when computing CRC for a sub level or a persistent level.
		FTransform TransformComponents = FTransform::Identity;
		if (bMustUndoLevelTransform)
		{
			ULevelStreaming* SteamingLevel = FLevelUtils::FindStreamingLevel(LODActor->GetLevel());
			if (SteamingLevel)
			{
				TransformComponents = SteamingLevel->LevelTransform.Inverse();
			}
		}

		// We get the CRC of each component
		TArray<uint32> ComponentsCRCs;
		for(UPrimitiveComponent* Component : Components)
		{
			if(UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
			{
				uint32 ComponentCRC = 0;

				// CRC component
				ComponentCRC = GetCRC(StaticMeshComponent, ComponentCRC, TransformComponents);

				if(UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
				{
					// CRC static mesh
					ComponentCRC = GetCRC(StaticMesh, ComponentCRC);

					// CRC materials
					const int32 NumMaterials = StaticMeshComponent->GetNumMaterials();
					for(int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
					{
						UMaterialInterface* MaterialInterface = StaticMeshComponent->GetMaterial(MaterialIndex);
						if (MaterialInterface)
						{
							ComponentCRC = GetCRC(MaterialInterface, ComponentCRC);

							TArray<UTexture*> Textures;
							MaterialInterface->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, true);
							for (UTexture* Texture : Textures)
							{
								ComponentCRC = GetCRC(Texture, ComponentCRC);
							}
						}
					}
				}

				ComponentsCRCs.Add(ComponentCRC);
			}
		}

		// Sort the components CRCs to ensure the order of components won't have an impact on the final CRC
		ComponentsCRCs.Sort();

		// Append all components CRCs
		for (uint32 ComponentCRC : ComponentsCRCs)
		{
			CRC = HashCombine(CRC, ComponentCRC);
		}

		Key += TEXT("_");
		Key += BytesToHex((uint8*)&CRC, sizeof(uint32));
	}

	// Mesh reduction method
	{
		// NOTE: This mimics code in the editor only FMeshReductionManagerModule::StartupModule(). If that changes then this should too
		FString HLODMeshReductionModuleName;
		GConfig->GetString(TEXT("/Script/Engine.ProxyLODMeshSimplificationSettings"), TEXT("r.ProxyLODMeshReductionModule"), HLODMeshReductionModuleName, GEngineIni);
		// If nothing was requested, default to simplygon for mesh merging reduction
		if (HLODMeshReductionModuleName.IsEmpty())
		{
			HLODMeshReductionModuleName = TEXT("SimplygonMeshReduction");
		}

		Key += TEXT("_");
		Key += HLODMeshReductionModuleName;
	}

	return FName(*Key);
}

void UHLODProxy::SpawnLODActors(ULevel* InLevel)
{
	for (const auto& Pair : HLODActors)
	{
		// Spawn LODActor
		ALODActor* LODActor = Pair.Key->SpawnLODActor(InLevel);
		if (LODActor)
		{
			LODActor->Proxy = this;
		}
	}
}

void UHLODProxy::PostLoad()
{
	Super::PostLoad();

	// PKG_ContainsMapData required so FEditorFileUtils::GetDirtyContentPackages can treat this as a map package
	GetOutermost()->SetPackageFlags(PKG_ContainsMapData);
}

void UHLODProxy::DestroyObject(UObject* InObject)
{
	if (!InObject->IsPendingKill())
	{
		InObject->MarkPackageDirty();

		InObject->ClearFlags(RF_Public | RF_Standalone);
		InObject->SetFlags(RF_Transient);
		InObject->Rename(nullptr, GetTransientPackage());
		InObject->MarkPendingKill();
	
		if (InObject->IsRooted())
		{
			InObject->RemoveFromRoot();
		}
	}
}

void UHLODProxy::RemoveAssets(const FHLODProxyMesh& ProxyMesh)
{
	UPackage* Outermost = GetOutermost();

	// Destroy the static mesh
	UStaticMesh* StaticMesh = const_cast<UStaticMesh*>(ProxyMesh.GetStaticMesh());
	if (StaticMesh)
	{
		// Destroy every materials
		for (const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials())
		{
			UMaterialInterface* Material = StaticMaterial.MaterialInterface;

			if (Material)
			{
				// Destroy every textures
				TArray<UTexture*> Textures;
				Material->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, true);
				for (UTexture* Texture : Textures)
				{
					if (Texture->GetOutermost() == Outermost)
					{
						DestroyObject(Texture);
					}
				}

				if (Material->GetOutermost() == Outermost)
				{
					DestroyObject(Material);
				}
			}
		}

		if (StaticMesh->GetOutermost() == Outermost)
		{
			DestroyObject(StaticMesh);
		}

		// Notify the LOD Actor that the static mesh just marked for deletion is no longer usable,
		// so that it regenerates its render thread state to no longer point to the deleted mesh.
		if (ALODActor* LODActor = ProxyMesh.GetLODActor().Get())
		{
			LODActor->SetStaticMesh(nullptr);
		}
	}
}

bool UHLODProxy::SetHLODBakingTransform(const FTransform& InTransform)
{
	bool bChanged = false;

	for (TMap<UHLODProxyDesc*, FHLODProxyMesh>::TIterator ItHLODActor = HLODActors.CreateIterator(); ItHLODActor; ++ItHLODActor)
	{
		UHLODProxyDesc* HLODProxyDesc = ItHLODActor.Key();
		if (!HLODProxyDesc->HLODBakingTransform.Equals(InTransform))
		{
			HLODProxyDesc->HLODBakingTransform = InTransform;
			bChanged = true;
		}
	}

	return bChanged;
}

#endif // #if WITH_EDITOR

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

bool UHLODProxy::ContainsDataForActor(const ALODActor* InLODActor) const
{
#if WITH_EDITOR
	FName Key;

	// Only re-generate the key in non-PIE worlds
	if(InLODActor->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor))
	{
		Key = InLODActor->GetKey();
	}
	else
	{
		Key = GenerateKeyForActor(InLODActor);
	}
#else
	FName Key = InLODActor->GetKey();
#endif

	if(Key == NAME_None)
	{
		return false;
	}

	for (const auto& Pair : HLODActors)
	{
		const FHLODProxyMesh& ProxyMesh = Pair.Value;
		if(ProxyMesh.GetKey() == Key)
		{
			return true;
		}
	}

	for(const FHLODProxyMesh& ProxyMesh : ProxyMeshes)
	{
		if(ProxyMesh.GetKey() == Key)
		{
			return true;
		}
	}

	return false;
}

#endif