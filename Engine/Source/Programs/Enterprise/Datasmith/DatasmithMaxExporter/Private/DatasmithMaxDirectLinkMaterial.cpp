// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NEW_DIRECTLINK_PLUGIN

#include "DatasmithMaxDirectLink.h"

#include "DatasmithMaxHelper.h"
#include "DatasmithMaxWriter.h"
#include "DatasmithMaxClassIDs.h"
#include "DatasmithMaxSceneExporter.h"
#include "MaxMaterialsToUEPbr/DatasmithMaxMaterialsToUEPbr.h"


#include "DatasmithSceneFactory.h"


#include "Logging/LogMacros.h"


#include "Windows/AllowWindowsPlatformTypes.h"
MAX_INCLUDES_START
	#include "impexp.h"
	#include "max.h"
MAX_INCLUDES_END

namespace DatasmithMaxDirectLink
{

void FMaterialsCollectionTracker::AddActualMaterial(FMaterialTracker& MaterialTracker, Mtl* Material)
{
	// Record relationships between tracked material and actual materials used on geometry(e.g. submaterials of a tracked multisubobj material)
	UsedMaterialToMaterialTracker.FindOrAdd(Material).Add(&MaterialTracker);
	MaterialTracker.AddActualMaterial(Material);
}

const TCHAR* FMaterialsCollectionTracker::GetMaterialName(Mtl* Material)
{
	if (FString* NamePtr = UsedMaterialToDatasmithMaterialName.Find(Material))
	{
		return **NamePtr;
	}
	return *UsedMaterialToDatasmithMaterialName.Add(Material, 
		MaterialNameProvider.GenerateUniqueName(FDatasmithUtils::SanitizeObjectName(Material->GetName().data())));
}

void FMaterialsCollectionTracker::AssignMeshMaterials(const TSharedPtr<IDatasmithMeshElement>& MeshElement, Mtl* Material, const TSet<uint16>& SupportedChannels)
{
	if (FDatasmithMaxMatHelper::GetMaterialClass(Material) == EDSMaterialType::XRefMat)
	{
		AssignMeshMaterials(MeshElement, FDatasmithMaxMatHelper::GetRenderedXRefMaterial(Material), SupportedChannels);
		return;
	}
	if (Material != nullptr)
	{
		TArray<uint16> ChannelsSorted = SupportedChannels.Array();
		ChannelsSorted.Sort();
		for (uint16 Channel : ChannelsSorted)
		{
			//Max's channel UI is not zero-based, so we register an incremented ChannelID for better visual consistency after importing in Unreal.
			uint16 DisplayedChannelID = Channel + 1;

			// Multi mat
			if (FDatasmithMaxMatHelper::GetMaterialClass(Material) == EDSMaterialType::MultiMat)
			{
				// Replicate the 3ds max behavior where material ids greater than the number of sub-materials wrap around and are mapped to the existing sub-materials
				if ( Mtl* SubMaterial = Material->GetSubMtl(Channel % Material->NumSubMtls()) )
				{
					if (FDatasmithMaxMatHelper::GetMaterialClass(SubMaterial) == EDSMaterialType::TheaRandom)
					{
						MeshElement->SetMaterial(*FDatasmithMaxSceneExporter::GetRandomSubMaterial(SubMaterial, MeshElement->GetDimensions()), DisplayedChannelID);
					}
					else
					{
						MeshElement->SetMaterial(GetMaterialName(SubMaterial), DisplayedChannelID);
					}
				}
			}
			else if (FDatasmithMaxMatHelper::GetMaterialClass(Material) == EDSMaterialType::TheaRandom)
			{
				MeshElement->SetMaterial(*FDatasmithMaxSceneExporter::GetRandomSubMaterial(Material, MeshElement->GetDimensions()), DisplayedChannelID);
			}
			// Single material
			else
			{
				MeshElement->SetMaterial(GetMaterialName(Material), DisplayedChannelID);
			}
		}
	}
}

void FMaterialsCollectionTracker::AssignMeshActorMaterials(const TSharedPtr<IDatasmithMeshActorElement>& MeshActor, Mtl* Material, TSet<uint16>& SupportedChannels, const FVector3f& RandomSeed)
{
	if (FDatasmithMaxMatHelper::GetMaterialClass(Material) == EDSMaterialType::XRefMat)
	{
		AssignMeshActorMaterials(MeshActor, FDatasmithMaxMatHelper::GetRenderedXRefMaterial(Material), SupportedChannels, RandomSeed);
		return;
	}

	if (Material != nullptr)
	{
		if (SupportedChannels.Num() <= 1)
		{
			if (FDatasmithMaxMatHelper::GetMaterialClass(Material) != EDSMaterialType::MultiMat)
			{
				if (FDatasmithMaxMatHelper::GetMaterialClass(Material) != EDSMaterialType::TheaRandom)
				{
					MeshActor->AddMaterialOverride(GetMaterialName(Material), -1);
				}
				else
				{
					MeshActor->AddMaterialOverride( *FDatasmithMaxSceneExporter::GetRandomSubMaterial(Material, RandomSeed), -1 );
				}
			}
			else
			{
				int Mid = 0;

				//Find the lowest supported material id.
				SupportedChannels.Sort([](const uint16& A, const uint16& B) { return (A < B); });
				for (const uint16 SupportedMid : SupportedChannels)
				{
					Mid = SupportedMid;
				}

				// Replicate the 3ds max behavior where material ids greater than the number of sub-materials wrap around and are mapped to the existing sub-materials
				if (Mtl* SubMaterial = Material->GetSubMtl(Mid % Material->NumSubMtls()))
				{
					if (FDatasmithMaxMatHelper::GetMaterialClass(SubMaterial) != EDSMaterialType::TheaRandom)
					{
						MeshActor->AddMaterialOverride(GetMaterialName(SubMaterial), -1);
					}
					else
					{
						MeshActor->AddMaterialOverride( *FDatasmithMaxSceneExporter::GetRandomSubMaterial(SubMaterial, RandomSeed), -1 );
					}
				}
			}
		}
		else
		{
			int ActualSubObj = 1;
			SupportedChannels.Sort([](const uint16& A, const uint16& B) { return (A < B); });
			for (const uint16 Mid : SupportedChannels)
			{
				if (FDatasmithMaxMatHelper::GetMaterialClass(Material) != EDSMaterialType::MultiMat)
				{
					if (FDatasmithMaxMatHelper::GetMaterialClass(Material) != EDSMaterialType::TheaRandom)
					{
						MeshActor->AddMaterialOverride(GetMaterialName(Material), ActualSubObj);
					}
					else
					{
						MeshActor->AddMaterialOverride( *FDatasmithMaxSceneExporter::GetRandomSubMaterial(Material, RandomSeed), ActualSubObj );
					}
				}
				else
				{
					// Replicate the 3ds max behavior where material ids greater than the number of sub-materials wrap around and are mapped to the existing sub-materials
					int32 MaterialIndex = Mid % Material->NumSubMtls();

					Mtl* SubMaterial = Material->GetSubMtl(MaterialIndex);
					if (SubMaterial != nullptr)
					{
						if (FDatasmithMaxMatHelper::GetMaterialClass(SubMaterial) != EDSMaterialType::TheaRandom)
						{
							//Material slots in Max are not zero-based, so we serialize our SlotID starting from 1 for better visual consistency.
							MeshActor->AddMaterialOverride(GetMaterialName(SubMaterial), Mid + 1);
						}
						else
						{
							MeshActor->AddMaterialOverride( *FDatasmithMaxSceneExporter::GetRandomSubMaterial(SubMaterial, RandomSeed), MaterialIndex + 1 );
						}
					}
				}
				ActualSubObj++;
			}
		}
	}
}


// Copied from
// FDatasmithMaxSceneParser::MaterialEnum
// FDatasmithMaxSceneParser::TexEnum
// Collects actual materials that are used by the top-level material(assigned to node)
class FMaterialEnum
{
public:
	FMaterialsCollectionTracker& MaterialsCollectionTracker;
	FMaterialTracker& MaterialTracker;

	FMaterialEnum(FMaterialsCollectionTracker& InMaterialsCollectionTracker, FMaterialTracker& InMaterialTracker): MaterialsCollectionTracker(InMaterialsCollectionTracker), MaterialTracker(InMaterialTracker) {}

	void MaterialEnum(Mtl* Material, bool bAddMaterial)
	{
		if (Material == NULL)
		{
			return;
		}

		if (FDatasmithMaxMatHelper::GetMaterialClass(Material) == EDSMaterialType::XRefMat)
		{
			MaterialEnum(FDatasmithMaxMatHelper::GetRenderedXRefMaterial(Material), true);
		}
		else if (FDatasmithMaxMatHelper::GetMaterialClass(Material) == EDSMaterialType::MultiMat)
		{
			for (int i = 0; i < Material->NumSubMtls(); i++)
			{
				MaterialEnum(Material->GetSubMtl(i), true);
			}
		}
		else
		{
			if (bAddMaterial)
			{
				MaterialsCollectionTracker.AddActualMaterial(MaterialTracker, Material);
			}

			bool bAddRecursively = Material->ClassID() == THEARANDOMCLASS || Material->ClassID() == VRAYBLENDMATCLASS || Material->ClassID() == CORONALAYERMATCLASS || Material->ClassID() == BLENDMATCLASS;
			for (int i = 0; i < Material->NumSubMtls(); i++)
			{
				MaterialEnum(Material->GetSubMtl(i), bAddRecursively);
			}
		}
	}
};

void FMaterialsCollectionTracker::Reset()
{
	MaterialTrackers.Reset();
	InvalidatedMaterialTrackers.Reset();

	EncounteredMaterials.Reset();
	EncounteredTextures.Reset();
	MaterialNames.Reset();

	UsedMaterialToMaterialTracker.Reset();
	UsedMaterialToDatasmithMaterial.Reset();
	UsedMaterialToDatasmithMaterialName.Reset();
	MaterialNameProvider.Clear();

	UsedTextureToMaterialTracker.Reset();
	UsedTextureToDatasmithElement.Reset();
}

void FMaterialsCollectionTracker::UpdateMaterial(FMaterialTracker* MaterialTracker)
{
	RemoveConvertedMaterial(*MaterialTracker);
	FMaterialEnum(*this, *MaterialTracker).MaterialEnum(MaterialTracker->Material, true);
}

void FMaterialsCollectionTracker::AddDatasmithMaterialForUsedMaterial(TSharedRef<IDatasmithScene> DatasmithScene, Mtl* Material, TSharedPtr<IDatasmithBaseMaterialElement> DatasmithMaterial)
{
	if (DatasmithMaterial)
	{
		SCENE_UPDATE_STAT_INC(UpdateMaterials, Converted);
		DatasmithScene->AddMaterial(DatasmithMaterial);
		UsedMaterialToDatasmithMaterial.Add(Material, DatasmithMaterial);
		SceneTracker.RemapConvertedMaterialUVChannels(Material, DatasmithMaterial);
	}
}

void FMaterialsCollectionTracker::ConvertMaterial(Mtl* Material, TSharedRef<IDatasmithScene> DatasmithScene, const TCHAR* AssetsPath, TSet<Texmap*>& TexmapsConverted)
{
	if (UsedMaterialToDatasmithMaterial.Contains(Material))
	{
		// Material might have been already converted - if it's present in UsedMaterialToDatasmithMaterial this means that it(or multisubobj it's part of) wasn't changed
		// e.g. this happens when another multisubobj material is added with existing (and already converted) material as submaterial
		return;
	}

	SCENE_UPDATE_STAT_INC(UpdateMaterials, Total);

	TMap<Texmap*, TSet<TSharedPtr<IDatasmithTextureElement>>> TexmapsUsedByMaterial;
	FMaterialConversionContext MaterialConversionContext = {TexmapsUsedByMaterial, *this};
	TGuardValue MaterialConversionContextGuard(FDatasmithMaxMaterialsToUEPbrManager::Context, &MaterialConversionContext);

	TSharedPtr<IDatasmithBaseMaterialElement> DatasmithMaterial;
	if (FDatasmithMaxMaterialsToUEPbr* MaterialConverter = FDatasmithMaxMaterialsToUEPbrManager::GetMaterialConverter(Material))
	{
		 
		MaterialConverter->Convert(DatasmithScene, DatasmithMaterial, Material, AssetsPath);

		AddDatasmithMaterialForUsedMaterial(DatasmithScene, Material, DatasmithMaterial);
	}

	// Tie texture used by an actual material to tracked material
	for (TPair<Texmap*, TSet<TSharedPtr<IDatasmithTextureElement>>> TexmapAndDatasmithElements : TexmapsUsedByMaterial)
	{
		Texmap* Tex = TexmapAndDatasmithElements.Key;
		for (FMaterialTracker* MaterialTracker : UsedMaterialToMaterialTracker[Material])
		{
			MaterialTracker->AddActualTexture(Tex);
			UsedTextureToMaterialTracker.FindOrAdd(Tex).Add(MaterialTracker);
		}

		// Some material converters create texture elements during material conversion(like bakeable) - record created texture elements
		UsedTextureToDatasmithElement.FindOrAdd(Tex).Append(TexmapAndDatasmithElements.Value);

		TexmapsConverted.Add(Tex);
	}

}

void FMaterialsCollectionTracker::ReleaseMaterial(FMaterialTracker& MaterialTracker)
{
	RemoveConvertedMaterial(MaterialTracker);
	MaterialTrackers.Remove(MaterialTracker.Material);
	InvalidatedMaterialTrackers.Remove(&MaterialTracker);
}


void FMaterialsCollectionTracker::RemoveConvertedMaterial(FMaterialTracker& MaterialTracker)
{
	for (Mtl* Material: MaterialTracker.GetActualMaterials())
	{
		TSet<FMaterialTracker*>& MaterialTrackersForMaterial = UsedMaterialToMaterialTracker[Material];
		MaterialTrackersForMaterial.Remove(&MaterialTracker);
		if (!MaterialTrackersForMaterial.Num())
		{
			UsedMaterialToMaterialTracker.Remove(Material);

			if (FString Name; UsedMaterialToDatasmithMaterialName.RemoveAndCopyValue(Material, Name))
			{
				MaterialNameProvider.RemoveExistingName(Name);
			}

			TSharedPtr<IDatasmithBaseMaterialElement> DatasmithMaterial;
			if(UsedMaterialToDatasmithMaterial.RemoveAndCopyValue(Material, DatasmithMaterial))
			{
				SceneTracker.RemoveMaterial(DatasmithMaterial);
			}
		}
	}

	for (Texmap* Texture: MaterialTracker.GetActualTexmaps())
	{
		TSet<FMaterialTracker*>& MaterialTrackersForTexture = UsedTextureToMaterialTracker[Texture];
		MaterialTrackersForTexture.Remove(&MaterialTracker);

		if (!MaterialTrackersForTexture.Num()) // No tracked materials are using this texture anymore
		{
			UsedTextureToMaterialTracker.Remove(Texture);

			for (const TSharedPtr<IDatasmithTextureElement>& TextureElement: UsedTextureToDatasmithElement[Texture])
			{
				SceneTracker.RemoveTexture(TextureElement);
			}
			UsedTextureToDatasmithElement.Remove(Texture);
		}
	}

	MaterialTracker.ResetActualMaterialAndTextures();
}

FMaterialTracker* FMaterialsCollectionTracker::AddMaterial(Mtl* Material)
{
	if (FMaterialTrackerHandle* HandlePtr = MaterialTrackers.Find(Material))
	{
		return HandlePtr->GetMaterialTracker();
	}

	// Track material if not yet
	FMaterialTrackerHandle& MaterialTrackerHandle = MaterialTrackers.Emplace(Material, Material);
	InvalidatedMaterialTrackers.Add(MaterialTrackerHandle.GetMaterialTracker());
	return MaterialTrackerHandle.GetMaterialTracker();
}

void FMaterialsCollectionTracker::InvalidateMaterial(Mtl* Material)
{
	if (FMaterialTrackerHandle* MaterialTrackerHandle = MaterialTrackers.Find(Material))
	{
		InvalidatedMaterialTrackers.Add(MaterialTrackerHandle->GetMaterialTracker());
	}
}

}

#include "Windows/HideWindowsPlatformTypes.h"

#endif // NEW_DIRECTLINK_PLUGIN
