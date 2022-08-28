// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFExportOptions.h"
#include "UI/GLTFExportOptionsWindow.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "UObject/UObjectBaseUtility.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"

UGLTFExportOptions::UGLTFExportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bExportUnlitMaterials = true;
	bExportClearCoatMaterials = true;
	bBakeMaterialInputs = true;
	DefaultMaterialBakeSize = EGLTFExporterMaterialBakeSize::POT_1024;
	bExportVertexColors = true;
	NormalExportMethod = EGLTFExporterNormalExportMethod::NormalsAndTangents;
	bTangentDataQuantization = false;
	bNormalizeUVCoordinates = EGLTFExporterNormalizeUVCoordinates::Never;
	DefaultLevelOfDetail = 0;
	bMapSkeletalMotionToRoot = false;
	TextureFormat = EGLTFExporterTextureFormat::PNG;
	bExportLightmaps = true;
	TextureHDREncoding = EGLTFExporterTextureHDREncoding::RGBM;
	ExportScale = 0.01;
	bExportLights = EGLTFExporterLightMobility::MovableAndStationary;
	bExportCameras = true;
	bExportOrbitalCameras = false;
	bExportReflectionCaptures = false;
	bExportHDRIBackdrops = true;
	bExportVariantSets = true;
	bExportInteractionHotspots = false;
	bBundleWebViewer = false;
	bExportPreviewMesh = true;
	bAllExtensionsRequired = false;
}

void UGLTFExportOptions::ResetToDefault()
{
	ReloadConfig();
}

void UGLTFExportOptions::LoadOptions()
{
	const int32 PortFlags = 0;

	for (FProperty* Property = GetClass()->PropertyLink; Property; Property = Property->PropertyLinkNext)
	{
		if (!Property->HasAnyPropertyFlags(CPF_Config))
		{
			continue;
		}
		FString Section = GetClass()->GetName();
		FString Key = Property->GetName();

		const bool bIsPropertyInherited = Property->GetOwnerClass() != GetClass();
		UObject* SuperClassDefaultObject = GetClass()->GetSuperClass()->GetDefaultObject();

		const FString& PropFileName = GEditorPerProjectIni;

		FArrayProperty* Array = CastField<FArrayProperty>(Property);
		if (Array)
		{
			FConfigSection* Sec = GConfig->GetSectionPrivate(*Section, false, true, *GEditorPerProjectIni);
			if (Sec != nullptr)
			{
				TArray<FConfigValue> List;
				const FName KeyName(*Key, FNAME_Find);
				Sec->MultiFind(KeyName, List);

				FScriptArrayHelper_InContainer ArrayHelper(Array, this);
				// Only override default properties if there is something to override them with.
				if (List.Num() > 0)
				{
					ArrayHelper.EmptyAndAddValues(List.Num());
					for (int32 i = List.Num() - 1, c = 0; i >= 0; i--, c++)
					{
						Array->Inner->ImportText(*List[i].GetValue(), ArrayHelper.GetRawPtr(c), PortFlags, this);
					}
				}
				else
				{
					int32 Index = 0;
					const FConfigValue* ElementValue = nullptr;
					do
					{
						// Add array index number to end of key
						FString IndexedKey = FString::Printf(TEXT("%s[%i]"), *Key, Index);

						// Try to find value of key
						const FName IndexedName(*IndexedKey, FNAME_Find);
						if (IndexedName == NAME_None)
						{
							break;
						}
						ElementValue = Sec->Find(IndexedName);

						// If found, import the element
						if (ElementValue != nullptr)
						{
							// expand the array if necessary so that Index is a valid element
							ArrayHelper.ExpandForIndex(Index);
							Array->Inner->ImportText(*ElementValue->GetValue(), ArrayHelper.GetRawPtr(Index), PortFlags, this);
						}

						Index++;
					} while (ElementValue || Index < ArrayHelper.Num());
				}
			}
		}
		else
		{
			for (int32 i = 0; i < Property->ArrayDim; i++)
			{
				if (Property->ArrayDim != 1)
				{
					Key = FString::Printf(TEXT("%s[%i]"), *Property->GetName(), i);
				}

				FString Value;
				const bool bFoundValue = GConfig->GetString(*Section, *Key, Value, *GEditorPerProjectIni);

				if (bFoundValue)
				{
					if (Property->ImportText(*Value, Property->ContainerPtrToValuePtr<uint8>(this, i), PortFlags, this) == nullptr)
					{
						// this should be an error as the properties from the .ini / .int file are not correctly being read in and probably are affecting things in subtle ways
					}
				}
			}
		}
	}
}

void UGLTFExportOptions::SaveOptions()
{
	const int32 PortFlags = 0;

	for (FProperty* Property = GetClass()->PropertyLink; Property; Property = Property->PropertyLinkNext)
	{
		if (!Property->HasAnyPropertyFlags(CPF_Config))
		{
			continue;
		}
		FString Section = GetClass()->GetName();
		FString Key = Property->GetName();

		const bool bIsPropertyInherited = Property->GetOwnerClass() != GetClass();
		UObject* SuperClassDefaultObject = GetClass()->GetSuperClass()->GetDefaultObject();

		FArrayProperty* Array = CastField<FArrayProperty>(Property);
		if (Array)
		{
			FConfigSection* Sec = GConfig->GetSectionPrivate(*Section, true, false, *GEditorPerProjectIni);
			check(Sec);
			Sec->Remove(*Key);

			FScriptArrayHelper_InContainer ArrayHelper(Array, this);
			for (int32 i = 0; i < ArrayHelper.Num(); i++)
			{
				FString Buffer;
				Array->Inner->ExportTextItem(Buffer, ArrayHelper.GetRawPtr(i), ArrayHelper.GetRawPtr(i), this, PortFlags);
				Sec->Add(*Key, *Buffer);
			}
		}
		else
		{
			TCHAR TempKey[MAX_SPRINTF] = TEXT("");
			for (int32 Index = 0; Index < Property->ArrayDim; Index++)
			{
				if (Property->ArrayDim != 1)
				{
					FCString::Sprintf(TempKey, TEXT("%s[%i]"), *Property->GetName(), Index);
					Key = TempKey;
				}

				FString Value;
				Property->ExportText_InContainer(Index, Value, this, this, this, PortFlags);
				GConfig->SetString(*Section, *Key, *Value, *GEditorPerProjectIni);
			}
		}
	}
	GConfig->Flush(false);
}

void UGLTFExportOptions::FillOptions(bool bBatchMode, bool bShowOptionDialog, const FString& FullPath, bool& bOutOperationCanceled, bool& bOutExportAll)
{
	bOutOperationCanceled = false;

	// TODO: call LoadOptions();

	//Return if we do not show the export options or we are running automation test or we are unattended
	if (!bShowOptionDialog || GIsAutomationTesting || FApp::IsUnattended())
	{
		return;
	}

	bOutExportAll = false;

	SGLTFExportOptionsWindow::ShowDialog(this, FullPath, bBatchMode, bOutOperationCanceled, bOutExportAll);
	// TODO: call SaveOptions();
}

bool UGLTFExportOptions::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty->GetName() == TEXT("DefaultMaterialBakeSize"))
	{
		return true;
	}

	return false; // TODO: change to true for each option implemented and support by the object to be exported.
}

FIntPoint UGLTFExportOptions::GetDefaultMaterialBakeSize() const
{
	const int32 Size = 1 << static_cast<int>(DefaultMaterialBakeSize);
	return { Size, Size };
}
