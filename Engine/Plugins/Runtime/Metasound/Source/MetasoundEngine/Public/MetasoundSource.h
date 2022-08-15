// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "IAudioParameterTransmitter.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundParameterTransmitter.h"
#include "MetasoundRouter.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/MetaData.h"

#include "MetasoundSource.generated.h"


/** Declares the output audio format of the UMetaSoundSource */
UENUM()
enum class EMetasoundSourceAudioFormat : uint8
{
	Mono,
	Stereo,
	Quad,
	FiveDotZero UMETA(DisplayName="5.0"),
	FiveDotOne UMETA(DisplayName="5.1"),
	SevenDotOne UMETA(DisplayName="7.1"),

	COUNT UMETA(Hidden)
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

	UPROPERTY()
	TSet<FString> ReferencedAssetClassKeys;

	UPROPERTY()
	TSet<FSoftObjectPath> ReferenceAssetClassCache;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UMetasoundEditorGraphBase> Graph;
#endif // WITH_EDITORONLY_DATA

public:
	UMetaSoundSource(const FObjectInitializer& ObjectInitializer);

	// The output audio format of the metasound source.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Metasound)
	EMetasoundSourceAudioFormat OutputFormat;

	UPROPERTY(AssetRegistrySearchable)
	FGuid AssetClassID;

#if WITH_EDITORONLY_DATA
	UPROPERTY(AssetRegistrySearchable)
	FString RegistryInputTypes;

	UPROPERTY(AssetRegistrySearchable)
	FString RegistryOutputTypes;

	UPROPERTY(AssetRegistrySearchable)
	int32 RegistryVersionMajor = 0;

	UPROPERTY(AssetRegistrySearchable)
	int32 RegistryVersionMinor = 0;

	//~ Begin UObject Interface.
	virtual void PostLoad() override;
	//~ End UObject Interface.

	// Sets Asset Registry Metadata associated with this MetaSoundSource
	virtual void SetRegistryAssetClassInfo(const Metasound::Frontend::FNodeClassInfo& InNodeInfo) override;

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

	virtual void PostDuplicate(EDuplicateMode::Type InDuplicateMode) override;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& InEvent) override;

private:
	void PostEditChangeOutputFormat();
public:

#endif // WITH_EDITOR

	virtual const TSet<FString>& GetReferencedAssetClassKeys() const override
	{
		return ReferencedAssetClassKeys;
	}

	virtual void BeginDestroy() override;
	virtual void PreSave(FObjectPreSaveContext InSaveContext) override;
	virtual void Serialize(FArchive& Ar) override;

	virtual TSet<FSoftObjectPath>& GetReferencedAssetClassCache() override;
	virtual const TSet<FSoftObjectPath>& GetReferencedAssetClassCache() const override;

	// Returns Asset Metadata associated with this MetaSoundSource
	virtual Metasound::Frontend::FNodeClassInfo GetAssetClassInfo() const override;


	virtual bool ConformObjectDataToInterfaces() override;

	UObject* GetOwningAsset() override
	{
		return this;
	}

	const UObject* GetOwningAsset() const override
	{
		return this;
	}

	virtual void InitParameters(TArray<FAudioParameter>& InParametersToInit, FName InFeatureName) override;
	virtual void InitResources() override;

	virtual bool IsPlayable() const override;
	virtual bool SupportsSubtitles() const override;
	virtual float GetDuration() const override;
	virtual bool ImplementsParameterInterface(Audio::FParameterInterfacePtr InInterface) const override;
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;
	virtual TUniquePtr<Audio::IParameterTransmitter> CreateParameterTransmitter(Audio::FParameterTransmitterInitParams&& InParams) const override;
	virtual bool IsParameterValid(const FAudioParameter& InParameter) const override;
	virtual bool IsLooping() const override;
	virtual bool IsOneShot() const override;
	virtual bool EnableSubmixSendsOnPreview() const override { return true; }
protected:
	/** Gets all the default parameters for this Asset.  */
	virtual bool GetAllDefaultParameters(TArray<FAudioParameter>& OutParameters) const override;

	virtual void SetReferencedAssetClassKeys(TSet<Metasound::Frontend::FNodeRegistryKey>&& InKeys) override;

	bool IsParameterValid(const FAudioParameter& InParameter, const TMap<FName, FName>& InInputNameTypePairs) const;

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
	Metasound::FMetasoundEnvironment CreateEnvironment() const;
	Metasound::FMetasoundEnvironment CreateEnvironment(const FSoundGeneratorInitParams& InParams) const;
	Metasound::FMetasoundEnvironment CreateEnvironment(const Audio::FParameterTransmitterInitParams& InParams) const;
	const TArray<Metasound::FVertexName>& GetOutputAudioChannelOrder() const;
};
