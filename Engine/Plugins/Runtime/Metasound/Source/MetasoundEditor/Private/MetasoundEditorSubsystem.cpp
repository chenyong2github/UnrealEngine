// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorSubsystem.h"

#include "IAssetTools.h"
#include "MetasoundBuilderSubsystem.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundUObjectRegistry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetasoundEditorSubsystem)

#define LOCTEXT_NAMESPACE "MetaSoundEditor"


TScriptInterface<IMetaSoundDocumentInterface> UMetaSoundEditorSubsystem::BuildToAsset(UMetaSoundBuilderBase* InBuilder, const FString& Author, const FString& AssetName, const FString& PackagePath, EMetaSoundBuilderResult& OutResult)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	if (InBuilder)
	{
		constexpr UObject* Parent = nullptr;
		constexpr UFactory* Factory = nullptr;
		if (UObject* NewMetaSound = IAssetTools::Get().CreateAsset(AssetName, PackagePath, &InBuilder->GetBuilderUClass(), Factory))
		{
			InBuilder->InitNodeLocations();
			InBuilder->SetAuthor(Author);

			FMetaSoundBuilderOptions BuilderOptions { FName(*AssetName) };
			BuilderOptions.ExistingAsset = NewMetaSound;
			TScriptInterface<IMetaSoundDocumentInterface> DocInterface = InBuilder->Build(Parent, BuilderOptions);

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(NewMetaSound);
			UMetasoundEditorGraph* Graph = NewObject<UMetasoundEditorGraph>(NewMetaSound, FName(), RF_Transactional);
			Graph->Schema = UMetasoundEditorGraphSchema::StaticClass();
			MetaSoundAsset->SetGraph(Graph);

			OutResult = EMetaSoundBuilderResult::Succeeded;
			return NewMetaSound;
		}
	}

	OutResult = EMetaSoundBuilderResult::Failed;
	return nullptr;
}
