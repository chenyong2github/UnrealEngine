// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundInstanceTransmitter.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundRouter.h"
#include "Sound/SoundWaveProcedural.h"

#include "MetasoundSource.generated.h"


namespace Metasound
{
	namespace ConsoleVariables
	{
		static float BlockRate = 100.f;
	}
} // namespace Metasound

/** Declares the output audio format of the UMetaSoundSource */
UENUM()
enum class EMetasoundSourceAudioFormat : uint8
{
	// Mono audio output
	Mono,

	// Stereo audio output
	Stereo,

	COUNT UMETA(Hidden)
};

UCLASS()
class METASOUNDENGINE_API UMetasoundEditorGraphBase : public UEdGraph
{
	GENERATED_BODY()

public:
	virtual bool IsEditorOnly() const override { return true; }
	virtual bool NeedsLoadForEditorGame() const override { return false; }

	virtual void Synchronize() { }
};

/**
 * This Metasound type can be played as an audio source.
 */
UCLASS(hidecategories = object, BlueprintType)
class METASOUNDENGINE_API UMetaSoundSource : public USoundWaveProcedural, public FMetasoundAssetBase
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = CustomView)
	FMetasoundFrontendDocument RootMetasoundDocument;

#if WITH_EDITORONLY_DATA
	UPROPERTY(AssetRegistrySearchable)
	FMetasoundFrontendClassAssetTags AssetTags;

	UPROPERTY()
	UMetasoundEditorGraphBase* Graph;
#endif // WITH_EDITORONLY_DATA

public:
	UMetaSoundSource(const FObjectInitializer& ObjectInitializer);

	// The output audio format of the metasound source.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Metasound)
	EMetasoundSourceAudioFormat OutputFormat;

#if WITH_EDITORONLY_DATA
	// Returns document name (for editor purposes, and avoids making document public for edit
	// while allowing editor to reference directly)
	static FName GetDocumentPropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(UMetaSoundSource, RootMetasoundDocument);
	}

	// Name to display in editors
	virtual FText GetDisplayName() const override;

	// Returns the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @return Editor graph associated with UMetaSoundSource.
	virtual UEdGraph* GetGraph() override;
	virtual const UEdGraph* GetGraph() const override;
	virtual UEdGraph& GetGraphChecked() override;
	virtual const UEdGraph& GetGraphChecked() const override;

	// Sets the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @param Editor graph associated with UMetaSoundSource.
	virtual void SetGraph(UEdGraph* InGraph) override
	{
		Graph = CastChecked<UMetasoundEditorGraphBase>(InGraph);
	}
#endif // #if WITH_EDITORONLY_DATA

#if WITH_EDITOR
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	virtual void PostEditUndo() override;

	virtual bool GetRedrawThumbnail() const override
	{
		return false;
	}

	virtual void SetRedrawThumbnail(bool bInRedraw) override
	{
	}

	virtual bool CanVisualizeAsset() const override
	{
		return false;
	}

	virtual void PostEditChangeProperty(FPropertyChangedEvent& InEvent) override;
#endif // WITH_EDITOR

	virtual const FMetasoundFrontendArchetype& GetArchetype() const override;

	// If set to be a preset, converts to a full-access MetaSound,
	// removing edit restrictions and excluding it from automatic
	// interface versioning.
	UFUNCTION(Category = Metasound, meta = (CallInEditor = "true"))
	void ConvertFromPreset();

	UObject* GetOwningAsset() override
	{
		return this;
	}

	const UObject* GetOwningAsset() const override
	{
		return this;
	}

	virtual bool IsPlayable() const override;
	virtual bool SupportsSubtitles() const override;
	virtual float GetDuration() override;
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;
	virtual TUniquePtr<IAudioInstanceTransmitter> CreateInstanceTransmitter(const FAudioInstanceTransmitterInitParams& InParams) const override;

	// Get the most up to date archetype for metasound sources.
	const TArray<FMetasoundFrontendArchetype>& GetPreferredArchetypes() const override;

protected:
	Metasound::Frontend::FDocumentAccessPtr GetDocument() override
	{
		using namespace Metasound::Frontend;
		// Return document using FAccessPoint to inform the TAccessPtr when the 
		// object is no longer valid.
		return MakeAccessPtr<FDocumentAccessPtr>(RootMetasoundDocument.AccessPoint, RootMetasoundDocument);
	}

	Metasound::Frontend::FConstDocumentAccessPtr GetDocument() const override
	{
		using namespace Metasound::Frontend;
		// Return document using FAccessPoint to inform the TAccessPtr when the 
		// object is no longer valid.
		return MakeAccessPtr<FConstDocumentAccessPtr>(RootMetasoundDocument.AccessPoint, RootMetasoundDocument);
	}

private:
	Metasound::FOperatorSettings GetOperatorSettings(Metasound::FSampleRate InSampleRate) const;

	static const FString& GetOnPlayInputName();
	static const FString& GetAudioOutputName();
	static const FString& GetIsFinishedOutputName();
	static const FString& GetAudioDeviceHandleVariableName();
	static const FString& GetSoundUniqueIdName();
	static const FString& GetIsPreviewSoundName();
	static const FMetasoundFrontendArchetype& GetBaseArchetype();
	static const FMetasoundFrontendArchetype& GetMonoSourceArchetype();
	static const FMetasoundFrontendArchetype& GetStereoSourceArchetype();
};
