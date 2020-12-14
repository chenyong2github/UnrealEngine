// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDataLayout.h"
#include "MetasoundAssetBase.h"

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
	FMetasoundDocument RootMetasoundDocument;

	// MetasoundSourceAccessPoint allows the RootMetasoundDocument to be safely
	// accessed via the TAccessPtr. The lifetime of MetasoundSourceAccessPoint is
	// tied to the lifetime of RootMetasoundDocument by their coexistence as members
	// of the same class. When MetasoundSourceAccessPoint is destructed, the documents
	// access ptr becomes invalid. 
	Metasound::Frontend::FAccessPoint MetasoundSourceAccessPoint;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
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

	UObject* GetOwningAsset() const override
	{
		// Hack to allow handles to manipulate while providing
		// ability to inspect via a handle from the const version of GetRootGraphHandle()
		return const_cast<UObject*>(CastChecked<const UObject>(this));
	}

	// Updates the Metasound's metadata (name, author, etc).
	// @param InMetadata Metadata containing corrections to the class metadata.
	void SetMetadata(FMetasoundClassMetadata& InMetadata) override;

	bool IsPlayable() const override;
	bool SupportsSubtitles() const override;
	float GetDuration() override;
	ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;
	void PostLoad() override;

	// Get the most up to date archetype for metasound sources.
	const TArray<FMetasoundArchetype>& GetPreferredArchetypes() const override;

protected:

	Metasound::Frontend::TAccessPtr<FMetasoundDocument> GetDocument() override
	{
		// Return document using FAccessPoint to inform the TAccessPtr when the 
		// object is no longer valid.
		return Metasound::Frontend::MakeAccessPtr<FMetasoundDocument>(MetasoundSourceAccessPoint, RootMetasoundDocument);
	}

	Metasound::Frontend::TAccessPtr<const FMetasoundDocument> GetDocument() const override
	{
		// Return document using FAccessPoint to inform the TAccessPtr when the 
		// object is no longer valid.
		return Metasound::Frontend::MakeAccessPtr<const FMetasoundDocument>(MetasoundSourceAccessPoint, RootMetasoundDocument);
	}

private:

	static const FString& GetOnPlayInputName();
	static const FString& GetAudioOutputName();
	static const FString& GetIsFinishedOutputName();
	static const FString& GetAudioDeviceHandleVariableName();
	static const FMetasoundArchetype& GetBaseArchetype();
	static const FMetasoundArchetype& GetMonoSourceArchetype();
	static const FMetasoundArchetype& GetStereoSourceArchetype();
};
