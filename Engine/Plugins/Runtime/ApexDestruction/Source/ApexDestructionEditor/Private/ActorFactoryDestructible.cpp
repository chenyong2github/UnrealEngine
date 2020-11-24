// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorFactoryDestructible.h"
#include "DestructibleActor.h"
#include "DestructibleMesh.h"
#include "DestructibleComponent.h"
#include "DestructibleMeshFactory.h"
#include "Misc/FileHelper.h"
#include "ActorFactories/ActorFactory.h"
#include "AssetData.h"
#include "PhysicsPublic.h"
#include "PhysXIncludes.h"
#include "Editor.h"
#include "ApexDestructibleAssetImport.h"
#include "ApexClothingUtils.h"
#include "Misc/MessageDialog.h"
#include "ReimportDestructibleMeshFactory.h"
#include "EditorFramework/AssetImportData.h"

#define LOCTEXT_NAMESPACE "ActorFactoryDestructible"

DEFINE_LOG_CATEGORY_STATIC(LogDestructibleFactories, Log, All);

/*-----------------------------------------------------------------------------
UActorFactoryDestructible
-----------------------------------------------------------------------------*/
UActorFactoryDestructible::UActorFactoryDestructible(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	NewActorClass = ADestructibleActor::StaticClass();
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	DisplayName = LOCTEXT("DestructibleDisplayName", "Destructible");
	bUseSurfaceOrientation = true;
}

bool UActorFactoryDestructible::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (!AssetData.IsValid() || !AssetData.GetClass()->IsChildOf(UDestructibleMesh::StaticClass()))
	{
		OutErrorMsg = NSLOCTEXT("CanCreateActor", "NoDestructibleMeshSpecified", "No destructible mesh was specified.");
		return false;
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	return true;
}

void UActorFactoryDestructible::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UDestructibleMesh* DestructibleMesh = CastChecked<UDestructibleMesh>(Asset);
	ADestructibleActor* NewDestructibleActor = CastChecked<ADestructibleActor>(NewActor);

	// Term Component
	NewDestructibleActor->GetDestructibleComponent()->UnregisterComponent();

	// Change properties
	NewDestructibleActor->GetDestructibleComponent()->SetSkeletalMesh(DestructibleMesh);

	// Init Component
	NewDestructibleActor->GetDestructibleComponent()->RegisterComponent();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

UObject* UActorFactoryDestructible::GetAssetFromActorInstance(AActor* Instance)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	check(Instance->IsA(NewActorClass));
	ADestructibleActor* DA = CastChecked<ADestructibleActor>(Instance);

	check(DA->GetDestructibleComponent());
	return DA->GetDestructibleComponent()->SkeletalMesh;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UActorFactoryDestructible::PostCreateBlueprint(UObject* Asset, AActor* CDO)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (Asset != NULL && CDO != NULL)
	{
		UDestructibleMesh* DestructibleMesh = CastChecked<UDestructibleMesh>(Asset);
		ADestructibleActor* DestructibleActor = CastChecked<ADestructibleActor>(CDO);

		DestructibleActor->GetDestructibleComponent()->SetSkeletalMesh(DestructibleMesh);
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FQuat UActorFactoryDestructible::AlignObjectToSurfaceNormal(const FVector& InSurfaceNormal, const FQuat& ActorRotation) const
{
	// Meshes align the Z (up) axis with the surface normal
	return FindActorAlignmentRotation(ActorRotation, FVector(0.f, 0.f, 1.f), InSurfaceNormal);
}

/*------------------------------------------------------------------------------
UDestructibleMeshFactory implementation.
------------------------------------------------------------------------------*/
namespace DestructibleFactoryConstants
{
	static const FString DestructibleAssetClass("DestructibleAssetParameters");
}

UDestructibleMeshFactory::UDestructibleMeshFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	bEditorImport = true;

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	SupportedClass = UDestructibleMesh::StaticClass();
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	bCreateNew = false;
	Formats.Add(TEXT("apx;APEX XML Asset"));
	Formats.Add(TEXT("apb;APEX Binary Asset"));
}

FText UDestructibleMeshFactory::GetDisplayName() const
{
	return LOCTEXT("APEXDestructibleFactoryDescription", "APEX Destructible Asset");
}

#if WITH_APEX

bool UDestructibleMeshFactory::FactoryCanImport(const FString& Filename)
{
	// Need to read in the file and try to create an asset to get it's type
	TArray<uint8> FileBuffer;
	if (FFileHelper::LoadFileToArray(FileBuffer, *Filename, FILEREAD_Silent))
	{
		physx::PxFileBuf* Stream = GApexSDK->createMemoryReadStream(FileBuffer.GetData(), FileBuffer.Num());
		if (Stream)
		{
			NvParameterized::Serializer::SerializeType SerializeType = GApexSDK->getSerializeType(*Stream);
			if (NvParameterized::Serializer* Serializer = GApexSDK->createSerializer(SerializeType))
			{
				NvParameterized::Serializer::DeserializedData DeserializedData;
				Serializer->deserialize(*Stream, DeserializedData);

				if (DeserializedData.size() > 0)
				{
					NvParameterized::Interface* AssetInterface = DeserializedData[0];

					int32 StringLength = StringCast<TCHAR>(AssetInterface->className()).Length();
					FString ClassName(StringLength, StringCast<TCHAR>(AssetInterface->className()).Get());

					if (ClassName == DestructibleFactoryConstants::DestructibleAssetClass)
					{
						return true;
					}
				}
			}

			GApexSDK->releaseMemoryReadStream(*Stream);
		}
	}
	return false;
}

UObject* UDestructibleMeshFactory::FactoryCreateBinary
(
	UClass*				Class,
	UObject*			InParent,
	FName				Name,
	EObjectFlags		Flags,
	UObject*			Context,
	const TCHAR*		FileType,
	const uint8*&		Buffer,
	const uint8*			BufferEnd,
	FFeedbackContext*	Warn
)
{
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, Class, InParent, Name, FileType);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// The return value
	UDestructibleMesh* DestructibleMesh = nullptr;

	// Create an Apex NxDestructibleAsset from the binary blob
	apex::DestructibleAsset* ApexDestructibleAsset = CreateApexDestructibleAssetFromBuffer(Buffer, (int32)(BufferEnd - Buffer));
	if (ApexDestructibleAsset != nullptr)
	{
		// Succesfully created the NxDestructibleAsset, now create a UDestructibleMesh
		DestructibleMesh = ImportDestructibleMeshFromApexDestructibleAsset(InParent, *ApexDestructibleAsset, Name, Flags, nullptr);
		if (DestructibleMesh != nullptr)
		{
			GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, DestructibleMesh);

			// Success
			DestructibleMesh->PostEditChange();
		}
	}
#if WITH_APEX_CLOTHING
	else
	{
		// verify whether this is an Apex Clothing asset or not 
		apex::ClothingAsset* ApexClothingAsset = ApexClothingUtils::CreateApexClothingAssetFromBuffer(Buffer, (int32)(BufferEnd - Buffer));

		if (ApexClothingAsset)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ApexClothingWrongImport", "The file you tried to import is an APEX clothing asset file. You need to use Persona to import this asset and associate it with a skeletal mesh.\n\n 1. Import a skeletal mesh from an FBX file, or choose an existing skeletal asset and open it up in Persona.\n 2. Choose \"Add APEX clothing file\" and choose this APEX clothing asset file."));

			// This asset is used only for showing a message how to import an Apex Clothing asset properly
			GPhysCommandHandler->DeferredRelease(ApexClothingAsset);
		}
	}
#endif // #if WITH_APEX_CLOTHING

	return DestructibleMesh;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

#endif // WITH_APEX

/*-----------------------------------------------------------------------------
UReimportDestructibleMeshFactory implementation.
-----------------------------------------------------------------------------*/
UReimportDestructibleMeshFactory::UReimportDestructibleMeshFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	SupportedClass = UDestructibleMesh::StaticClass();
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	bCreateNew = false;
	bText = false;
	Formats.Add(TEXT("apx;APEX XML Asset"));
	Formats.Add(TEXT("apb;APEX Binary Asset"));

}

FText UReimportDestructibleMeshFactory::GetDisplayName() const
{
	return LOCTEXT("APEXReimportDestructibleAssetFactoryDescription", "APEX Reimport Destructible Asset");
}

#if WITH_APEX

bool UReimportDestructibleMeshFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UDestructibleMesh* DestructibleMesh = Cast<UDestructibleMesh>(Obj);
	if (DestructibleMesh)
	{
		if (DestructibleMesh->AssetImportData)
		{
			DestructibleMesh->AssetImportData->ExtractFilenames(OutFilenames);
		}
		else
		{
			OutFilenames.Add(TEXT(""));
		}
		return true;
	}
	return false;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UReimportDestructibleMeshFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UDestructibleMesh* DestructibleMesh = Cast<UDestructibleMesh>(Obj);
	if (DestructibleMesh && ensure(NewReimportPaths.Num() == 1))
	{
		DestructibleMesh->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

EReimportResult::Type UReimportDestructibleMeshFactory::Reimport(UObject* Obj)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Only handle valid skeletal meshes
	if (!Obj || !Obj->IsA(UDestructibleMesh::StaticClass()))
	{
		return EReimportResult::Failed;
	}

	UDestructibleMesh* DestructibleMesh = Cast<UDestructibleMesh>(Obj);

	const FString Filename = DestructibleMesh->AssetImportData->GetFirstFilename();

	// If there is no file path provided, can't reimport from source
	if (!Filename.Len())
	{
		// Since this is a new system most skeletal meshes don't have paths, so logging has been commented out
		//UE_LOG(LogEditorFactories, Warning, TEXT("-- cannot reimport: skeletal mesh resource does not have path stored."));
		return EReimportResult::Failed;
	}

	UE_LOG(LogDestructibleFactories, Log, TEXT("Performing atomic reimport of [%s]"), *Filename);

	// Ensure that the file provided by the path exists
	if (IFileManager::Get().FileSize(*Filename) == INDEX_NONE)
	{
		UE_LOG(LogDestructibleFactories, Warning, TEXT("-- cannot reimport: source file cannot be found."));
		return EReimportResult::Failed;
	}

	CurrentFilename = Filename;

	// Create an Apex NxDestructibleAsset from the binary blob
	apex::DestructibleAsset* ApexDestructibleAsset = CreateApexDestructibleAssetFromFile(Filename);
	if (ApexDestructibleAsset != nullptr)
	{
		// Succesfully created the NxDestructibleAsset, now create a UDestructibleMesh
		UDestructibleMesh* ReimportedDestructibleMesh = ImportDestructibleMeshFromApexDestructibleAsset(DestructibleMesh->GetOuter(), *ApexDestructibleAsset, DestructibleMesh->GetFName(), DestructibleMesh->GetFlags(), nullptr,
			EDestructibleImportOptions::PreserveSettings);
		if (ReimportedDestructibleMesh != nullptr)
		{
			check(ReimportedDestructibleMesh == DestructibleMesh);

			UE_LOG(LogDestructibleFactories, Log, TEXT("-- imported successfully"));

			// Try to find the outer package so we can dirty it up
			if (DestructibleMesh->GetOuter())
			{
				DestructibleMesh->GetOuter()->MarkPackageDirty();
			}
			else
			{
				DestructibleMesh->MarkPackageDirty();
			}
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "ImportFailed_Destructible", "Reimport Failed"));
			UE_LOG(LogDestructibleFactories, Warning, TEXT("-- import failed"));
		}
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "ImportFailed_Destructible", "Reimport Failed"));
		UE_LOG(LogDestructibleFactories, Warning, TEXT("-- import failed"));
	}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

	return EReimportResult::Succeeded;
}

int32 UReimportDestructibleMeshFactory::GetPriority() const
{
	return ImportPriority;
}

#endif // #if WITH_APEX

#undef LOCTEXT_NAMESPACE
