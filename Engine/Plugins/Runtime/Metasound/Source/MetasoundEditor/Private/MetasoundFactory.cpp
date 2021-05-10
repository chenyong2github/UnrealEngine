// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFactory.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Metasound.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundSource.h"
#include "MetasoundUObjectRegistry.h"


namespace Metasound
{
	namespace Editor
	{
		namespace FactoryPrivate
		{
			UObject* InitMetasound(UMetaSoundSource* MetasoundSource, EObjectFlags Flags)
			{
				FMetasoundFrontendClassMetadata Metadata;

				Metadata.ClassName = FMetasoundFrontendClassName(FName(), *MetasoundSource->GetName(), FName());
				Metadata.Version.Major = 1;
				Metadata.Version.Minor = 0;
				Metadata.Type = EMetasoundFrontendClassType::Graph;
				Metadata.Author = FText::FromString(UKismetSystemLibrary::GetPlatformUserName());

				MetasoundSource->SetMetadata(Metadata);
				MetasoundSource->ConformDocumentToMetasoundArchetype();

				FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(MetasoundSource);
				check(MetasoundAsset);
				check(!MetasoundAsset->GetGraph());

				UObject* MetasoundObject = CastChecked<UObject>(MetasoundSource);

				UMetasoundEditorGraph* Graph = NewObject<UMetasoundEditorGraph>(MetasoundSource, FName(), Flags);
				Graph->Schema = UMetasoundEditorGraphSchema::StaticClass();
				MetasoundAsset->SetGraph(Graph);

				return MetasoundSource;
			}
		} // namespace FactoryPrivate
	} // namespace Editor
} // namespace Metasound

// TODO: Re-enable and potentially rename once composition is supported
// UMetasoundFactory::UMetasoundFactory(const FObjectInitializer& ObjectInitializer)
// 	: Super(ObjectInitializer)
// {
// 	SupportedClass = UMetaSound::StaticClass();
// 
// 	bCreateNew = true;
// 	bEditorImport = false;
// 	bEditAfterNew = true;
// }
// 
// UObject* UMetasoundFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName Name, EObjectFlags Flags, UObject* InContext, FFeedbackContext* InFeedbackContext)
// {
// 	UMetaSound* Metasound = NewObject<UMetaSound>(InParent, Name, Flags);
//	Metasound::Editor::FactoryPrivate::InitMetasound(Metasound, Flags);
// 
//	CastChecked<UMetasoundEditorGraph>(&Metasound->GetGraphChecked())->ParentMetasound = MetasoundSource;
// 
//	FGraphBuilder::ConstructGraph(*Metasound);
//	FGraphBuilder::SynchronizeGraph(*Metasound);
// }

UMetasoundSourceFactory::UMetasoundSourceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMetaSoundSource::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UMetasoundSourceFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName Name, EObjectFlags Flags, UObject* InContext, FFeedbackContext* InFeedbackContext)
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetaSoundSource* MetasoundSource = NewObject<UMetaSoundSource>(InParent, Name, Flags);
	Metasound::Editor::FactoryPrivate::InitMetasound(MetasoundSource, Flags);

	FGraphBuilder::ConstructGraph(*MetasoundSource);
	FGraphBuilder::SynchronizeGraph(*MetasoundSource);

	return MetasoundSource;
}
