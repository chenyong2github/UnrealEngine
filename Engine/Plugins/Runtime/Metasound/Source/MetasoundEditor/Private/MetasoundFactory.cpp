// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFactory.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Metasound.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundSource.h"


namespace Metasound
{
	namespace FactoryPrivate
	{
		template <typename T>
		T* CreateNewMetaSoundObject(UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InReferencedMetaSoundObject)
		{
			using namespace Editor;

			T* MetaSoundObject = NewObject<T>(InParent, InName, InFlags);
			check(MetaSoundObject);

			if (InReferencedMetaSoundObject)
			{
				// Ensure referenced MetaSound is up-to-date
				FMetasoundAssetBase* RefMetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(InReferencedMetaSoundObject);
				check(RefMetasoundAsset);
				FName Name = InReferencedMetaSoundObject->GetFName();
				FString Path = InReferencedMetaSoundObject->GetPathName();
				if (Frontend::FVersionDocument(Name, Path).Transform(RefMetasoundAsset->GetDocumentHandle()))
				{
					InReferencedMetaSoundObject->MarkPackageDirty();
				}

				FGraphBuilder::InitMetaSound(*MetaSoundObject, UKismetSystemLibrary::GetPlatformUserName(), false /* bConformToArchetype */);
				FGraphBuilder::InitMetaSoundPreset(*InReferencedMetaSoundObject, *MetaSoundObject);
			}
			else
			{
				FGraphBuilder::InitMetaSound(*MetaSoundObject, UKismetSystemLibrary::GetPlatformUserName());
			}

			return MetaSoundObject;
		}
	}
}

UMetaSoundBaseFactory::UMetaSoundBaseFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UMetaSoundFactory::UMetaSoundFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMetaSound::StaticClass();
}

UObject* UMetaSoundFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InFeedbackContext)
{
	using namespace Metasound::FactoryPrivate;
	return CreateNewMetaSoundObject<UMetaSound>(InParent, InName, InFlags, ReferencedMetaSoundObject);
}

UMetaSoundSourceFactory::UMetaSoundSourceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMetaSoundSource::StaticClass();
}

UObject* UMetaSoundSourceFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InFeedbackContext)
{
	using namespace Metasound::FactoryPrivate;
	UMetaSoundSource* NewSource = CreateNewMetaSoundObject<UMetaSoundSource>(InParent, InName, InFlags, ReferencedMetaSoundObject);

	// Copy over referenced fields that are specific to sources
	if (UMetaSoundSource* ReferencedMetaSound = Cast<UMetaSoundSource>(ReferencedMetaSoundObject))
	{
		NewSource->OutputFormat = ReferencedMetaSound->OutputFormat;
	}

	return NewSource;
}
