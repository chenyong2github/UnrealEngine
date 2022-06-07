// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NEW_DIRECTLINK_PLUGIN

#include "DatasmithMaxDirectLink.h"

#include "DatasmithMaxHelper.h"
#include "DatasmithMaxWriter.h"
#include "DatasmithMaxClassIDs.h"
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
				if (!MaterialsCollectionTracker.EncounteredMaterials.Contains(Material))
				{
					int DuplicateCount = 0;
					FString ProposedName = Material->GetName().data();
					// todo: fix this without changing max material name? Btw - this requires changing all material export functions for all types of materials(those functions tied to Mtl->GetName())
					// todo: revert material names after export
					MaterialsCollectionTracker.MaterialNames.Add(*ProposedName);

					// Make unique material name
					FDatasmithUtils::SanitizeNameInplace(ProposedName);
					for (Mtl* OtherMaterial: MaterialsCollectionTracker.EncounteredMaterials)
					{
						if (ProposedName == FDatasmithUtils::SanitizeName(OtherMaterial->GetName().data()))
						{
							DuplicateCount++;
							ProposedName = FDatasmithUtils::SanitizeName(Material->GetName().data()) + TEXT("_(") + FString::FromInt(DuplicateCount) + TEXT(")");
						}
					}
					Material->SetName(*ProposedName);
					MaterialsCollectionTracker.EncounteredMaterials.Add(Material);
				}
				MaterialTracker.AddActualMaterial(Material);
			}

			bool bAddRecursively = Material->ClassID() == THEARANDOMCLASS || Material->ClassID() == VRAYBLENDMATCLASS || Material->ClassID() == CORONALAYERMATCLASS;
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

	UsedTextureToMaterialTracker.Reset();
	UsedTextureToDatasmithElement.Reset();
}

void FMaterialsCollectionTracker::UpdateMaterial(FMaterialTracker* MaterialTracker)
{
	RemoveConvertedMaterial(*MaterialTracker);
	FMaterialEnum(*this, *MaterialTracker).MaterialEnum(MaterialTracker->Material, true);
	for (Mtl* Material : MaterialTracker->GetActualMaterials())
	{
		UsedMaterialToMaterialTracker.FindOrAdd(Material).Add(MaterialTracker);
	}
}

void FMaterialsCollectionTracker::ConvertMaterial(Mtl* Material, TSharedRef<IDatasmithScene> DatasmithScene, const TCHAR* AssetsPath, TSet<Texmap*>& TexmapsConverted)
{
	SCENE_UPDATE_STAT_INC(UpdateMaterials, Total);
	// todo: reworking material export - composite materials(like VRayBlend) export submaterials too
	//   check below is needed to make sure that no material is exported twice.
	//   This is how it was handled before bu needs to be handled without going through the list of all materials.
	// Names should be unique prior to this
	FString MaterialName(FDatasmithUtils::SanitizeObjectName(Material->GetName().data()));
	for (int i = 0; i < DatasmithScene->GetMaterialsCount(); i++)
	{
		if (FString(DatasmithScene->GetMaterial(i)->GetName()) == MaterialName)
		{
			SCENE_UPDATE_STAT_INC(UpdateMaterials, SkippedAsAlreadyConverted);
			return;
		}
	}

	TMap<Texmap*, TSet<TSharedPtr<IDatasmithTextureElement>>> TexmapsUsedByMaterial;
	TSharedPtr<IDatasmithBaseMaterialElement> DatasmithMaterial;

	if (FDatasmithMaxMaterialsToUEPbr* MaterialConverter = FDatasmithMaxMaterialsToUEPbrManager::GetMaterialConverter(Material))
	{

		TGuardValue<TMap<Texmap*, TSet<TSharedPtr<IDatasmithTextureElement>>>*> TexmapsConvertedGuard(MaterialConverter->TexmapsConverted, &TexmapsUsedByMaterial);
		 
		MaterialConverter->Convert(DatasmithScene, DatasmithMaterial, Material, AssetsPath);

		if (DatasmithMaterial)
		{
			SCENE_UPDATE_STAT_INC(UpdateMaterials, Converted);
			DatasmithScene->AddMaterial(DatasmithMaterial);
			UsedMaterialToDatasmithMaterial.Add(Material, DatasmithMaterial);
			SceneTracker.RemapConvertedMaterialUVChannels(Material, DatasmithMaterial);
		}
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
