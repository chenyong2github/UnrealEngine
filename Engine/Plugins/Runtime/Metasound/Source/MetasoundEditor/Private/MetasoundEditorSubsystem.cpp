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

		// Not about to follow this lack of const correctness down a multidecade in the works rabbit hole.
		UClass& BuilderUClass = const_cast<UClass&>(InBuilder->GetBuilderUClass());
		if (UObject* NewMetaSound = IAssetTools::Get().CreateAsset(AssetName, PackagePath, &BuilderUClass, Factory))
		{
			InBuilder->InitNodeLocations();
			InBuilder->SetAuthor(Author);

			FMetaSoundBuilderOptions BuilderOptions { FName(*AssetName) };
			BuilderOptions.ExistingMetaSound = NewMetaSound;
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
#undef LOCTEXT_NAMESPACE // "MetaSoundEditor"
