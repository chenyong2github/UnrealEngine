// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVR/DMXMVRGeneralSceneDescription.h"

#include "DMXProtocolCommon.h"
#include "DMXRuntimeLog.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXGDTFAssetImportData.h"
#include "Library/DMXImportGDTF.h"
#include "Library/DMXLibrary.h"
#include "MVR/DMXMVRAssetImportData.h"

#include "XmlFile.h"
#include "XmlNode.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/Paths.h"


UDMXMVRGeneralSceneDescription::UDMXMVRGeneralSceneDescription()
{
#if WITH_EDITORONLY_DATA
	MVRAssetImportData = NewObject<UDMXMVRAssetImportData>(this, TEXT("MVRAssetImportData"), RF_Public);
#endif
}

#if WITH_EDITOR
UDMXMVRGeneralSceneDescription* UDMXMVRGeneralSceneDescription::CreateFromXmlFile(TSharedRef<FXmlFile> GeneralSceneDescriptionXml, UObject* Outer, FName Name, EObjectFlags Flags)
{
	UDMXMVRGeneralSceneDescription* GeneralSceneDescription = NewObject<UDMXMVRGeneralSceneDescription>(Outer, Name, Flags);
	GeneralSceneDescription->ParseGeneralSceneDescriptionXml(GeneralSceneDescriptionXml);

	return GeneralSceneDescription;
}
#endif // WITH_EDITOR

#if WITH_EDITOR
UDMXMVRGeneralSceneDescription* UDMXMVRGeneralSceneDescription::CreateFromDMXLibrary(const UDMXLibrary& DMXLibrary, UObject* Outer, FName Name, EObjectFlags Flags)
{
	UDMXMVRGeneralSceneDescription* GeneralSceneDescription = NewObject<UDMXMVRGeneralSceneDescription>(Outer, Name, Flags);
	GeneralSceneDescription->WriteDMXLibraryToGeneralSceneDescription(DMXLibrary);

	return GeneralSceneDescription;
}
#endif // WITH_EDITOR

FDMXMVRFixture* UDMXMVRGeneralSceneDescription::FindMVRFixture(const FGuid& MVRFixtureUUID)
{
	return MVRFixtures.FindByPredicate([MVRFixtureUUID](const FDMXMVRFixture& MVRFixture)
		{
			return MVRFixture.UUID == MVRFixtureUUID;
		});
}

void UDMXMVRGeneralSceneDescription::AddMVRFixture(FDMXMVRFixture& MVRFixture)
{
	if (ensureAlwaysMsgf(MVRFixture.UUID.IsValid(), TEXT("Trying to add MVR Fixture '%s' to General Scene Description, but the UUID is invalid."), *MVRFixture.Name))
	{
		return;
	}

	const bool bUUIDAlreadyContained = FindMVRFixture(MVRFixture.UUID) != nullptr;
	if (ensureAlwaysMsgf(bUUIDAlreadyContained, TEXT("Trying to add MVR Fixture '%s' to General Scene Description, but its UUID is already containted in the General Scene Description."), *MVRFixture.Name))
	{
		return;
	}

	MVRFixtures.Add(MVRFixture);
}

#if WITH_EDITOR
void UDMXMVRGeneralSceneDescription::WriteDMXLibraryToGeneralSceneDescription(const UDMXLibrary& DMXLibrary)
{
	TArray<FDMXMVRFixture> MVRFixturesInDMXLibrary;

	TArray<UDMXEntityFixturePatch*> FixturePatches = DMXLibrary.GetEntitiesTypeCast<UDMXEntityFixturePatch>();
	FixturePatches.Sort([](const UDMXEntityFixturePatch& FixturePatchA, const UDMXEntityFixturePatch& FixturePatchB)
		{
			const bool bUniverseIsSmaller = FixturePatchA.GetUniverseID() < FixturePatchB.GetUniverseID();
			const bool bUniverseIsEqual = FixturePatchA.GetUniverseID() == FixturePatchB.GetUniverseID();
			const bool bAddressIsSmaller = FixturePatchA.GetStartingChannel() <= FixturePatchB.GetStartingChannel();

			return bUniverseIsSmaller || (bUniverseIsEqual && bAddressIsSmaller);
		});

	TArray<FGuid> MVRFixtureUUIDsInUse;
	for (UDMXEntityFixturePatch* FixturePatch : FixturePatches)
	{
		MVRFixtureUUIDsInUse.Add(FixturePatch->GetMVRFixtureUUID());
	}

	// Remove entries no longer present in the DMX Library
	MVRFixtures.RemoveAll([&MVRFixtureUUIDsInUse](const FDMXMVRFixture& MVRFixture)
		{
			return !MVRFixtureUUIDsInUse.Contains(MVRFixture.UUID);
		});

	// Create or update MVR Fixtures for each Fixture Patch's MVR Fixture UUIDs
	for (UDMXEntityFixturePatch* FixturePatch : FixturePatches)
	{
		if (FixturePatch)
		{
			WriteFixturePatchToGeneralSceneDescription(*FixturePatch);
		}
	}
}
#endif // WITH_EDITOR

void UDMXMVRGeneralSceneDescription::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar << MVRFixtures;
}

#if WITH_EDITOR
void UDMXMVRGeneralSceneDescription::WriteFixturePatchToGeneralSceneDescription(const UDMXEntityFixturePatch& FixturePatch)
{
	if (const UDMXLibrary* DMXLibrary = FixturePatch.GetParentLibrary())
	{
		const FGuid& MVRFixtureUUID = FixturePatch.GetMVRFixtureUUID();
		FDMXMVRFixture* MVRFixturePtr = FindMVRFixture(MVRFixtureUUID);

		FDMXMVRFixture MVRFixture;
		if (MVRFixturePtr)
		{
			MVRFixture = *MVRFixturePtr;
		}
		else
		{
			const TArray<int32> UnitNumbersInUse = GetUnitNumbersInUse(*DMXLibrary);
			const int32 UnitNumber = UnitNumbersInUse.Num() > 0 ? UnitNumbersInUse.Last() + 1 : 1;

			MVRFixture.Name = FixturePatch.Name;
			MVRFixture.UUID = MVRFixtureUUID;

			// Create a new Unit Number and add it to the array of Unit Numbers in use
			MVRFixture.UnitNumber = UnitNumbersInUse.Last() + 1;
		}

		MVRFixture.Addresses.Universe = FixturePatch.GetUniverseID();
		MVRFixture.Addresses.Address = FixturePatch.GetStartingChannel();

		bool bSetGDTFSpec = false;
		if (UDMXEntityFixtureType* FixtureType = FixturePatch.GetFixtureType())
		{
			if (UDMXImportGDTF* GDTF = FixtureType->GDTF)
			{
				const FString SourceFilename = [GDTF]()
				{
					if (GDTF && GDTF->GetGDTFAssetImportData())
					{
						return GDTF->GetGDTFAssetImportData()->GetSourceFilePathAndName();
					}
					return FString();
				}();
				MVRFixture.GDTFSpec = FPaths::GetBaseFilename(SourceFilename);
			}

			const int32 ModeIndex = FixturePatch.GetActiveModeIndex();
			if (ensureAlwaysMsgf(FixtureType->Modes.IsValidIndex(ModeIndex), TEXT("Trying to write Active Mode of to General Scene Description, but the Mode Index is invalid")))
			{
				MVRFixture.GDTFMode = FixtureType->Modes[ModeIndex].ModeName;
			}
		}

		if (!bSetGDTFSpec)
		{
			// No point to set a mode when there's no GDTF
			MVRFixture.GDTFMode = TEXT("");
			MVRFixture.GDTFSpec = TEXT("");
		}

		if (MVRFixturePtr)
		{
			(*MVRFixturePtr) = MVRFixture;
		}
		else
		{
			MVRFixtures.Add(MVRFixture);
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXMVRGeneralSceneDescription::ParseGeneralSceneDescriptionXml(const TSharedRef<FXmlFile>& GeneralSceneDescription)
{
	if (const FXmlNode* RootNode = GeneralSceneDescription->GetRootNode())
	{
		static const FString NodeName_Scene = TEXT("Scene");
		if (const FXmlNode* SceneNode = RootNode->FindChildNode(NodeName_Scene))
		{
			static const FString NodeName_Layers = TEXT("Layers");
			if (const FXmlNode* LayersNode = SceneNode->FindChildNode(NodeName_Layers))
			{
				for (const FXmlNode* LayerNode : LayersNode->GetChildrenNodes())
				{
					static const FString NodeName_Layer = TEXT("Layer");
					if (LayerNode->GetTag() == NodeName_Layer)
					{
						for (const FXmlNode* ChildListNode : LayerNode->GetChildrenNodes())
						{
							static const FString NodeName_ChildList = TEXT("ChildList");
							if (ChildListNode->GetTag() == NodeName_ChildList)
							{
								for (const FXmlNode* FixtureNode : ChildListNode->GetChildrenNodes())
								{
									static const FString NodeName_Fixture = TEXT("Fixture");
									if (FixtureNode->GetTag() == NodeName_Fixture)
									{
										FDMXMVRFixture MVRFixture(FixtureNode);
										if (MVRFixture.IsValid())
										{
											MVRFixtures.Add(MoveTemp(MVRFixture));
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
}
#endif // WITH_EDITOR

TArray<int32> UDMXMVRGeneralSceneDescription::GetUnitNumbersInUse(const UDMXLibrary& DMXLibrary)
{
	TArray<UDMXEntityFixturePatch*> FixturePatches = DMXLibrary.GetEntitiesTypeCast<UDMXEntityFixturePatch>();

	TArray<FGuid> MVRFixtureUUIDsInUse;
	for (UDMXEntityFixturePatch* FixturePatch : FixturePatches)
	{
		MVRFixtureUUIDsInUse.Add(FixturePatch->GetMVRFixtureUUID());
	}

	// Fetch Unit Numbers in use to create new ones when adding new MVR Fixtures
	TArray<int32> UnitNumbersInUse;
	for (const FGuid& MVRFixtureUUID : MVRFixtureUUIDsInUse)
	{
		const FDMXMVRFixture* MVRFixturePtr = FindMVRFixture(MVRFixtureUUID);
		if (MVRFixturePtr)
		{
			UnitNumbersInUse.Add(MVRFixturePtr->UnitNumber);
		}
	}
	if (UnitNumbersInUse.Num() == 0)
	{
		UnitNumbersInUse.Add(0);
	}
	else
	{
		UnitNumbersInUse.Sort([](int32 UnitNumberA, int32 UnitNumberB)
			{
				return UnitNumberA < UnitNumberB;
			});
	}

	return UnitNumbersInUse;
}
