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

/** Declares the output audio format of the UMetaSoundSource */
UENUM()
enum class EMetasoundSourceAudioFormat : uint8
{
	Mono,  //< Mono audio output.
	Stereo //< Stereo audio output.
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
	UPROPERTY()
	UMetasoundEditorGraphBase* Graph;
#endif // WITH_EDITORONLY_DATA

public:
	UMetaSoundSource(const FObjectInitializer& ObjectInitializer);

	// The output audio format of the metasound source.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Metasound)
	EMetasoundSourceAudioFormat OutputFormat;

#if WITH_EDITOR
	// Returns document name (for editor purposes, and avoids making document public for edit
	// while allowing editor to reference directly)
	static FName GetDocumentPropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(UMetaSoundSource, RootMetasoundDocument);
	}

	virtual void PostEditUndo() override;

	// Returns the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @return Editor graph associated with UMetaSoundSource.
	virtual UEdGraph* GetGraph() override
	{
		return Graph;
	}

	virtual const UEdGraph* GetGraph() const override
	{
		return Graph;
	}

	virtual UEdGraph& GetGraphChecked() override
	{
		check(Graph);
		return *Graph;
	}

	virtual const UEdGraph& GetGraphChecked() const override
	{
		check(Graph);
		return *Graph;
	}

	// Sets the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @param Editor graph associated with UMetaSoundSource.
	virtual void SetGraph(UEdGraph* InGraph) override
	{
		Graph = CastChecked<UMetasoundEditorGraphBase>(InGraph);
	}

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

	void PostEditChangeProperty(FPropertyChangedEvent& InEvent);

#endif // WITH_EDITOR

	UObject* GetOwningAsset() override
	{
		return this;
	}

	const UObject* GetOwningAsset() const override
	{
		return this;
	}

	bool IsPlayable() const override;
	bool SupportsSubtitles() const override;
	float GetDuration() override;
	ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;
	TUniquePtr<IAudioInstanceTransmitter> CreateInstanceTransmitter(const FAudioInstanceTransmitterInitParams& InParams) const override;

	// Get the most up to date archetype for metasound sources.
	const TArray<FMetasoundFrontendArchetype>& GetPreferredMetasoundArchetypes() const override;

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
	struct FSendInfoAndVertexName
	{
		Metasound::FMetasoundInstanceTransmitter::FSendInfo SendInfo;
		FString VertexName;
	};

	bool GetReceiveNodeMetadataForDataType(const FName& InTypeName, FMetasoundFrontendClassMetadata& OutMetadata) const;
	Metasound::Frontend::FNodeHandle AddInputPinForSendAddress(const Metasound::FMetasoundInstanceTransmitter::FSendInfo& InSendInfo, Metasound::Frontend::FGraphHandle InGraph) const;
	bool CopyDocumentAndInjectReceiveNodes(uint64 InInstanceID, const FMetasoundFrontendDocument& InSourceDoc, FMetasoundFrontendDocument& OutDestDoc) const;	
	TArray<FString> GetTransmittableInputVertexNames() const;
	Metasound::FOperatorSettings GetOperatorSettings(Metasound::FSampleRate InSampleRate) const;
	Metasound::FSendAddress CreateSendAddress(uint64 InInstanceID, const FString& InVertexName, const FName& InDataTypeName) const;
	


	TArray<FSendInfoAndVertexName> GetSendInfos(uint64 InInstanceID) const;


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
