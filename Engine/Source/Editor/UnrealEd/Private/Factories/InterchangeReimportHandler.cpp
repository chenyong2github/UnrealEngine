// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/InterchangeReimportHandler.h"

#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Factories/Factory.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeManager.h"
#include "Settings/EditorExperimentalSettings.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ObjectMacros.h"

#define LOCTEXT_NAMESPACE "InterchangeReimportHandler"

UInterchangeReimportHandler::UInterchangeReimportHandler(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

//~ Begin FReimportHandler Interface
bool UInterchangeReimportHandler::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	auto CanReimportAsset = [&OutFilenames](UAssetImportData* AssetImportData)
	{
		if (!AssetImportData)
		{
			return false;
		}
		UInterchangeAssetImportData* InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(AssetImportData);
		if (InterchangeAssetImportData == nullptr)
		{
			return false;
		}
		AssetImportData->ExtractFilenames(OutFilenames);
		return true;
	};

	
	const UEditorExperimentalSettings* EditorExperimentalSettings = GetDefault<UEditorExperimentalSettings>();
	const bool bUseInterchangeFramework = EditorExperimentalSettings->bEnableInterchangeFramework;
	const bool bUseInterchangeFrameworkForTextureOnly = (!bUseInterchangeFramework) && EditorExperimentalSettings->bEnableInterchangeFrameworkForTextureOnly;

	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Obj);
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(Obj);
	//UAnimSequence* AnimSequence = Cast<UAnimSequence>(Obj);
	UTexture* Texture = Cast<UTexture>(Obj);
	if (bUseInterchangeFramework && SkeletalMesh)
	{
		if (UAssetImportData* AssetImportData = SkeletalMesh->GetAssetImportData())
		{
			return CanReimportAsset(AssetImportData);
		}
	}
	else if (bUseInterchangeFramework && StaticMesh)
	{
		if (UAssetImportData* AssetImportData = StaticMesh->GetAssetImportData())
		{
			return CanReimportAsset(AssetImportData);
		}
	}
// 	else if (bUseInterchangeFramework && AnimSequence)
// 	{
// 		if (UAssetImportData* AssetImportData = AnimSequence->AssetImportData)
// 		{
// 			return CanReimportAsset(AssetImportData);
// 		}
// 	}
	else if ((bUseInterchangeFramework || bUseInterchangeFrameworkForTextureOnly) && Texture)
	{
		if (UAssetImportData* AssetImportData = Texture->AssetImportData)
		{
			return CanReimportAsset(AssetImportData);
		}
	}
	return false;
}

void UInterchangeReimportHandler::SetReimportPaths(UObject* Obj, const FString& NewReimportPath, const int32 SourceFileIndex)
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Obj);
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(Obj);
//	UAnimSequence* AnimSequence = Cast<UAnimSequence>(Obj);
	UTexture* Texture = Cast<UTexture>(Obj);

	UAssetImportData* AssetImportData = nullptr;
	if (SkeletalMesh)
	{
		AssetImportData = SkeletalMesh->GetAssetImportData();
	}
	else if (StaticMesh)
	{
		AssetImportData = StaticMesh->GetAssetImportData();
	}
// 	else if (AnimSequence)
// 	{
// 		AssetImportData = AnimSequence->AssetImportData;
// 	}
	else if (Texture)
	{
		AssetImportData = Texture->AssetImportData;
	}

	if (AssetImportData)
	{
		int32 RealSourceFileIndex = SourceFileIndex == INDEX_NONE ? 0 : SourceFileIndex;
		if (RealSourceFileIndex < AssetImportData->GetSourceFileCount())
		{
			AssetImportData->UpdateFilenameOnly(NewReimportPath, SourceFileIndex);
		}
		else
		{
			//Create a source file entry, this case happen when user import a specific content for the first time
			FString SourceIndexLabel = USkeletalMesh::GetSourceFileLabelFromIndex(RealSourceFileIndex).ToString();
			AssetImportData->AddFileName(NewReimportPath, RealSourceFileIndex, SourceIndexLabel);
		}
	}
}

void UInterchangeReimportHandler::SetReimportSourceIndex(UObject* Obj, const int32 SourceIndex)
{
	if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Obj))
	{
		if (UInterchangeAssetImportData* InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(SkeletalMesh->GetAssetImportData()))
		{
			for (UInterchangePipelineBase* PipelineBase : InterchangeAssetImportData->Pipelines)
			{
				PipelineBase->ScriptedSetReimportSourceIndex(Obj->GetClass(), SourceIndex);
			}
		}
	}
}

EReimportResult::Type UInterchangeReimportHandler::Reimport(UObject* Obj, int32 SourceFileIndex)
{
	return EReimportResult::Failed;
}

int32 UInterchangeReimportHandler::GetPriority() const
{
	//We want a high priority to surpass other legacy re-import handlers
	return UFactory::GetDefaultImportPriority() + 10;
}

#undef LOCTEXT_NAMESPACE