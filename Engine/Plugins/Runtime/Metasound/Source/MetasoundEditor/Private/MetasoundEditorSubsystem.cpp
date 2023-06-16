// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorSubsystem.h"

#include "IAssetTools.h"
#include "Kismet/KismetSystemLibrary.h"
#include "MetasoundBuilderSubsystem.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundFactory.h"
#include "MetasoundUObjectRegistry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetasoundEditorSubsystem)

#define LOCTEXT_NAMESPACE "MetaSoundEditor"


TScriptInterface<IMetaSoundDocumentInterface> UMetaSoundEditorSubsystem::BuildToAsset(UMetaSoundBuilderBase* InBuilder, const FString& Author, const FString& AssetName, const FString& PackagePath, EMetaSoundBuilderResult& OutResult, const USoundWave* TemplateSoundWave)
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

			const bool bIsSource = &BuilderUClass == UMetaSoundSource::StaticClass();
			if (InBuilder->IsPreset())
			{
				FMetasoundAssetBase* PresetAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(NewMetaSound);
				check(PresetAsset);
				PresetAsset->ConformObjectDataToInterfaces();

				// Only use referenced object's soundwave settings for sources if not overridden 
				if (TemplateSoundWave == nullptr && bIsSource)
				{
					if (const UObject* ReferencedObject = InBuilder->GetReferencedPresetAsset())
					{
						TemplateSoundWave = CastChecked<USoundWave>(ReferencedObject);
					}
				}
			}

			// Template sound wave settings only apply to sources 
			if (TemplateSoundWave != nullptr && bIsSource)
			{
				SetSoundWaveSettingsFromTemplate(NewMetaSound, TemplateSoundWave);
			}

			InitEdGraph(*NewMetaSound);
			OutResult = EMetaSoundBuilderResult::Succeeded;
			return NewMetaSound;
		}
	}

	OutResult = EMetaSoundBuilderResult::Failed;
	return nullptr;
}

void UMetaSoundEditorSubsystem::SetSoundWaveSettingsFromTemplate(UObject* NewMetasound, const USoundWave* TemplateSoundWave)
{
	USoundWave* NewMetaSoundWave = CastChecked<USoundWave>(NewMetasound);

	NewMetaSoundWave->SoundClassObject = TemplateSoundWave->SoundClassObject;
	NewMetaSoundWave->AttenuationSettings = TemplateSoundWave->AttenuationSettings;
	NewMetaSoundWave->ModulationSettings = TemplateSoundWave->ModulationSettings;
	
	// Concurrency
	NewMetaSoundWave->bOverrideConcurrency = TemplateSoundWave->bOverrideConcurrency;
	NewMetaSoundWave->ConcurrencySet = TemplateSoundWave->ConcurrencySet;
	NewMetaSoundWave->ConcurrencyOverrides = TemplateSoundWave->ConcurrencyOverrides;

	// Submix and bus sends 
	NewMetaSoundWave->bEnableSubmixSends = TemplateSoundWave->bEnableSubmixSends;
	NewMetaSoundWave->SoundSubmixSends = TemplateSoundWave->SoundSubmixSends;
	NewMetaSoundWave->bEnableBusSends = TemplateSoundWave->bEnableBusSends;
	NewMetaSoundWave->BusSends = TemplateSoundWave->BusSends;
	NewMetaSoundWave->PreEffectBusSends = TemplateSoundWave->PreEffectBusSends;
}

void UMetaSoundEditorSubsystem::InitAsset(UObject& InNewMetaSound, UObject* InReferencedMetaSound)
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface = &InNewMetaSound;
	FMetaSoundFrontendDocumentBuilder Builder(DocInterface);
	Builder.InitDocument();
#if WITH_EDITORONLY_DATA
	Builder.InitNodeLocations();
#endif // WITH_EDITORONLY_DATA

	const FString& Author = GetDefaultAuthor();
	Builder.SetAuthor(Author);

	// Initialize asset as a preset
	if (InReferencedMetaSound)
	{
		// Ensure the referenced MetaSound is registered already
		RegisterGraphWithFrontend(*InReferencedMetaSound);

		// Initialize preset with referenced Metasound 
		TScriptInterface<IMetaSoundDocumentInterface> ReferencedDocInterface = InReferencedMetaSound;
		const IMetaSoundDocumentInterface* ReferencedInterface = ReferencedDocInterface.GetInterface();
		check(ReferencedInterface);
		Builder.ConvertToPreset(ReferencedInterface->GetDocument());

		// Update asset object data from interfaces 
		FMetasoundAssetBase* PresetAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InNewMetaSound);
		check(PresetAsset);
		PresetAsset->ConformObjectDataToInterfaces();

		// Copy sound wave settings to preset for sources
		if (&ReferencedInterface->GetBaseMetaSoundUClass() == UMetaSoundSource::StaticClass())
		{
			SetSoundWaveSettingsFromTemplate(&InNewMetaSound, CastChecked<USoundWave>(InReferencedMetaSound));
		}
	}

	// Initial graph generation is not something to be managed by the transaction
	// stack, so don't track dirty state until after initial setup if necessary.
	InitEdGraph(InNewMetaSound);
}

void UMetaSoundEditorSubsystem::InitEdGraph(UObject& InMetaSound)
{
	using namespace Metasound;
	using namespace Metasound::Editor;

	FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
	checkf(MetaSoundAsset, TEXT("EdGraph can only be initialized on registered MetaSoundAsset type"));

	UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(MetaSoundAsset->GetGraph());
	if (!Graph)
	{
		Graph = NewObject<UMetasoundEditorGraph>(&InMetaSound, FName(), RF_Transactional);
		Graph->Schema = UMetasoundEditorGraphSchema::StaticClass();
		MetaSoundAsset->SetGraph(Graph);

		// Has to be done inline to have valid graph initially when opening editor for the first
		// time (as opposed to being applied on tick when the document's modify context has updates)
		FGraphBuilder::SynchronizeGraph(InMetaSound);
	}
}

void UMetaSoundEditorSubsystem::RegisterGraphWithFrontend(UObject& InMetaSound, bool bInForceViewSynchronization)
{
	Metasound::Editor::FGraphBuilder::RegisterGraphWithFrontend(InMetaSound, bInForceViewSynchronization);
}

const FString UMetaSoundEditorSubsystem::GetDefaultAuthor()
{
	FString Author = UKismetSystemLibrary::GetPlatformUserName();
	if (const UMetasoundEditorSettings* EditorSettings = GetDefault<UMetasoundEditorSettings>())
	{
		if (!EditorSettings->DefaultAuthor.IsEmpty())
		{
			Author = EditorSettings->DefaultAuthor;
		}
	}
	return Author;
}

UMetaSoundEditorSubsystem& UMetaSoundEditorSubsystem::GetChecked()
{
	checkf(GEditor, TEXT("Cannot access UMetaSoundEditorSubsystem without editor loaded"));
	UMetaSoundEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMetaSoundEditorSubsystem>();
	checkf(EditorSubsystem, TEXT("Failed to find initialized 'UMetaSoundEditorSubsystem"));
	return *EditorSubsystem;
}

const UMetaSoundEditorSubsystem& UMetaSoundEditorSubsystem::GetConstChecked()
{
	checkf(GEditor, TEXT("Cannot access UMetaSoundEditorSubsystem without editor loaded"));
	UMetaSoundEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMetaSoundEditorSubsystem>();
	checkf(EditorSubsystem, TEXT("Failed to find initialized 'UMetaSoundEditorSubsystem"));
	return *EditorSubsystem;
}

#undef LOCTEXT_NAMESPACE // "MetaSoundEditor"
