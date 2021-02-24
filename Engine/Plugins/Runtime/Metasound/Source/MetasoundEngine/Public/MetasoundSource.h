// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundInstanceTransmitter.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundRouter.h"

#include "Sound/SoundWaveProcedural.h"


#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
#include "Misc/AssertionMacros.h"
#endif // WITH_EDITORONLY_DATA

#include "MetasoundSource.generated.h"

/** Declares the output audio format of the UMetasoundSource */
UENUM()
enum class EMetasoundSourceAudioFormat : uint8
{
	Mono,  //< Mono audio output.
	Stereo //< Stereo audio output.
};

/**
 * This Metasound type can be played as an audio source.
 */
UCLASS(hidecategories = object, BlueprintType)
class METASOUNDENGINE_API UMetasoundSource : public USoundWaveProcedural, public FMetasoundAssetBase
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = CustomView)
	FMetasoundFrontendDocument RootMetasoundDocument;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = CustomView)
	UEdGraph* Graph;
#endif // WITH_EDITORONLY_DATA

public:
	UMetasoundSource(const FObjectInitializer& ObjectInitializer);

	// The output audio format of the metasound source.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Audio)
	EMetasoundSourceAudioFormat OutputFormat;

#if WITH_EDITORONLY_DATA
	// Returns document name (for editor purposes, and avoids making document public for edit
	// while allowing editor to reference directly)
	static FName GetDocumentPropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(UMetasoundSource, RootMetasoundDocument);
	}

	// Returns the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @return Editor graph associated with UMetasoundSource.
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
	// @param Editor graph associated with UMetasoundSource.
	virtual void SetGraph(UEdGraph* InGraph) override
	{
		Graph = InGraph;
	}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR

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

	Metasound::Frontend::TAccessPtr<FMetasoundFrontendDocument> GetDocument() override
	{
		// Return document using FAccessPoint to inform the TAccessPtr when the 
		// object is no longer valid.
		return Metasound::Frontend::MakeAccessPtr<FMetasoundFrontendDocument>(RootMetasoundDocument.AccessPoint, RootMetasoundDocument);
	}

	Metasound::Frontend::TAccessPtr<const FMetasoundFrontendDocument> GetDocument() const override
	{
		// Return document using FAccessPoint to inform the TAccessPtr when the 
		// object is no longer valid.
		return Metasound::Frontend::MakeAccessPtr<const FMetasoundFrontendDocument>(RootMetasoundDocument.AccessPoint, RootMetasoundDocument);
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
	static const FMetasoundFrontendArchetype& GetBaseArchetype();
	static const FMetasoundFrontendArchetype& GetMonoSourceArchetype();
	static const FMetasoundFrontendArchetype& GetStereoSourceArchetype();
};
