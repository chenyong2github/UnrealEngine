// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Map.h"
#include "Engine/Engine.h"
#include "Interfaces/MetasoundOutputFormatInterfaces.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentBuilder.h"
#include "Subsystems/EngineSubsystem.h"
#include "UObject/NoExportTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundBuilderSubsystem.generated.h"


// Forward Declarations
class UMetaSound;
class UMetaSoundSource;
struct FMetasoundFrontendVersion;
class UAudioComponent;
enum class EMetaSoundOutputAudioFormat : uint8;

namespace Metasound::Engine
{
	struct FOutputAudioFormatInfo;
} // namespace Metasound::Engine


DECLARE_DYNAMIC_DELEGATE_OneParam(FOnCreateAuditionGeneratorHandleDelegate, UMetasoundGeneratorHandle*, GeneratorHandle);

USTRUCT(BlueprintType, meta = (DisplayName = "MetaSound Node Input Handle"))
struct METASOUNDENGINE_API FMetaSoundBuilderNodeInputHandle : public FMetasoundFrontendVertexHandle
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType, meta = (DisplayName = "MetaSound Node Output Handle"))
struct METASOUNDENGINE_API FMetaSoundBuilderNodeOutputHandle : public FMetasoundFrontendVertexHandle
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct METASOUNDENGINE_API FMetaSoundNodeHandle
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid NodeID;

public:
	FMetaSoundNodeHandle() = default;
	FMetaSoundNodeHandle(const FGuid& InNodeID)
		: NodeID(InNodeID)
	{
	}

	// Returns whether or not the vertex handle is set (may or may not be
	// valid depending on what builder context it is referenced against)
	bool IsSet() const
	{
		return NodeID.IsValid();
	}
};

USTRUCT(BlueprintType)
struct METASOUNDENGINE_API FMetaSoundBuilderOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaSound|Builder")
	FName Name;

	// If true, adds MetaSound to node registry, making it available
	// for reference by other dynamically created MetaSounds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaSound|Builder")
	bool bAddToRegistry = false;

	TScriptInterface<IMetaSoundDocumentInterface> ExistingAsset;
};

UENUM(BlueprintType)
enum class EMetaSoundBuilderResult : uint8
{
	Succeeded,
	Failed
};

/** Base implementation of MetaSound builder */
UCLASS(Abstract)
class METASOUNDENGINE_API UMetaSoundBuilderBase : public UObject
{
	GENERATED_BODY()

public:
	// Adds a graph input node with the given name, DataType, and sets the graph input to default value.
	// Returns the new input node's output handle if it was successfully created, or an invalid handle if it failed.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult", AdvancedDisplay = "3"))
	UPARAM(DisplayName = "Output Handle") FMetaSoundBuilderNodeOutputHandle AddGraphInputNode(FName Name, FName DataType, FMetasoundFrontendLiteral DefaultValue, EMetaSoundBuilderResult& OutResult, bool bIsConstructorInput = false);

	// Adds a graph output node with the given name, DataType, and sets output node's input to default value.
	// Returns the new output node's input handle if it was successfully created, or an invalid handle if it failed.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Input Handle") FMetaSoundBuilderNodeInputHandle AddGraphOutputNode(FName Name, FName DataType, FMetasoundFrontendLiteral DefaultValue, EMetaSoundBuilderResult& OutResult, bool bIsConstructorInput = false);

	// Adds an interface registered with the given name to the graph, adding associated input and output nodes.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void AddInterface(FName InterfaceName, EMetaSoundBuilderResult& OutResult);

	// Adds a node to the graph using the provided MetaSound asset as its defining NodeClass.
	// Returns a node handle to the created node if successful, or an invliad handle if it failed.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", DisplayName = "Add MetaSound Node From Asset Class", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Node Handle") FMetaSoundNodeHandle AddNode(TScriptInterface<IMetaSoundDocumentInterface> NodeClass, EMetaSoundBuilderResult& OutResult);

	// Connects node output to a node input. Does *NOT* provide loop detection for performance reasons.  Loop detection is checked on class registration when built or played.
	// Returns succeeded if connection made, failed if connection already exists with input, the data types do not match, or the connection is not supported due to access type
	// incompatibility (ex. constructor input to non-constructor input).
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void ConnectNodes(const FMetaSoundBuilderNodeOutputHandle& NodeOutputHandle, const FMetaSoundBuilderNodeInputHandle& NodeInputHandle, EMetaSoundBuilderResult& OutResult);

	// Connects two nodes using defined MetaSound Interface Bindings registered with the MetaSound Interface registry.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void ConnectNodesByInterfaceBindings(const FMetaSoundNodeHandle& FromNodeHandle, const FMetaSoundNodeHandle& ToNodeHandle, EMetaSoundBuilderResult& OutResult);

	// Connects a given node's outputs to all graph outputs for shared interfaces implemented on both the node's referenced class and the builder's MetaSound graph.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Connected Graph Output Node Inputs") TArray<FMetaSoundBuilderNodeInputHandle> ConnectNodeOutputsToMatchingGraphInterfaceOutputs(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult);

	// Connects a given node's inputs to all graph inputs for shared interfaces implemented on both the node's referenced class and the builder's MetaSound graph. Returns output input node's outputs that were connected.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Connected Graph Input Node Outputs") TArray<FMetaSoundBuilderNodeOutputHandle> ConnectNodeInputsToMatchingGraphInterfaceInputs(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult);

	// Connects a given node output to the graph output with the given name.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void ConnectNodeOutputToGraphOutput(FName GraphOutputName, const FMetaSoundBuilderNodeOutputHandle& NodeOutputHandle, EMetaSoundBuilderResult& OutResult);

	// Connects a given node input to the graph input with the given name.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void ConnectNodeInputToGraphInput(FName GraphInputName, const FMetaSoundBuilderNodeInputHandle& NodeInputHandle, EMetaSoundBuilderResult& OutResult);

	// Returns whether node exists.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "IsValid") bool ContainsNode(const FMetaSoundNodeHandle& Node) const;

	// Returns whether node input exists.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "IsValid") bool ContainsNodeInput(const FMetaSoundBuilderNodeInputHandle& Input) const;

	// Returns whether node output exists.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "IsValid") bool ContainsNodeOutput(const FMetaSoundBuilderNodeOutputHandle& Output) const;

	// Disconnects node output to a node input. Returns success if connection was removed, failed if not.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void DisconnectNodes(const FMetaSoundBuilderNodeOutputHandle& NodeOutputHandle, const FMetaSoundBuilderNodeInputHandle& NodeInputHandle, EMetaSoundBuilderResult& OutResult);

	// Removes connection to a given node input. Returns success if connection was removed, failed if not.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void DisconnectNodeInput(const FMetaSoundBuilderNodeInputHandle& NodeInputHandle, EMetaSoundBuilderResult& OutResult);

	// Removes all connections from a given node output. Returns success if all connections were removed, failed if not.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void DisconnectNodeOutput(const FMetaSoundBuilderNodeOutputHandle& NodeOutputHandle, EMetaSoundBuilderResult& OutResult);

	// Returns graph input node by the given name if it exists, or an invalid handle if not found.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Node Handle") FMetaSoundNodeHandle FindGraphInputNode(FName InputName, EMetaSoundBuilderResult& OutResult) const;

	// Returns graph output node by the given name if it exists, or an invalid handle if not found.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Node Handle") FMetaSoundNodeHandle FindGraphOutputNode(FName OutputName, EMetaSoundBuilderResult& OutResult) const;

	// Returns node input by the given name if it exists, or an invalid handle if not found.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Input Handle") FMetaSoundBuilderNodeInputHandle FindNodeInputByName(const FMetaSoundNodeHandle& NodeHandle, FName InputName, EMetaSoundBuilderResult& OutResult) const;

	// Returns node output by the given name.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Input  Handles") TArray<FMetaSoundBuilderNodeInputHandle> FindNodeInputs(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult) const;

	// Returns node inputs by the given DataType (ex. "Audio", "Trigger", "String", "Bool", "Float", "Int32", etc.).
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Input Handles") TArray<FMetaSoundBuilderNodeInputHandle> FindNodeInputsByDataType(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult, FName DataType) const;

	// Returns node output by the given name.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Output Handle") FMetaSoundBuilderNodeOutputHandle FindNodeOutputByName(const FMetaSoundNodeHandle& NodeHandle, FName OutputName, EMetaSoundBuilderResult& OutResult) const;

	// Returns all node outputs.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Output Handles") TArray<FMetaSoundBuilderNodeOutputHandle> FindNodeOutputs(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult) const;

	// Returns node outputs by the given DataType (ex. "Audio", "Trigger", "String", "Bool", "Float", "Int32", etc.).
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Output Handles") TArray<FMetaSoundBuilderNodeOutputHandle> FindNodeOutputsByDataType(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult, FName DataType) const;

	// Returns input nodes associated with a given interface.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Input Node Handles") TArray<FMetaSoundNodeHandle> FindInterfaceInputNodes(FName InterfaceName, EMetaSoundBuilderResult& OutResult) const;

	// Returns output nodes associated with a given interface.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Output Node Handles") TArray<FMetaSoundNodeHandle> FindInterfaceOutputNodes(FName InterfaceName, EMetaSoundBuilderResult& OutResult) const;

	// Returns input's parent node if the input is valid, otherwise returns invalid node handle.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Node Handle") FMetaSoundNodeHandle FindNodeInputParent(const FMetaSoundBuilderNodeInputHandle& InputHandle, EMetaSoundBuilderResult& OutResult) const;

	// Returns output's parent node if the input is valid, otherwise returns invalid node handle.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Node Handle") FMetaSoundNodeHandle FindNodeOutputParent(const FMetaSoundBuilderNodeOutputHandle& OutputHandle, EMetaSoundBuilderResult& OutResult) const;

	// Returns output's parent node if the input is valid, otherwise returns invalid node handle.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Node ClassVersion") FMetasoundFrontendVersion FindNodeClassVersion(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult) const;

	// Returns node input's data if valid (including things like name and datatype).
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void GetNodeInputData(const FMetaSoundBuilderNodeInputHandle& InputHandle, FName& Name, FName& DataType, EMetaSoundBuilderResult& OutResult) const;

	// Returns node output's data if valid (including things like name and datatype).
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void GetNodeOutputData(const FMetaSoundBuilderNodeOutputHandle& OutputHandle, FName& Name, FName& DataType, EMetaSoundBuilderResult& OutResult) const;

	// Returns if a given interface is declared.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "IsDeclared") bool InterfaceIsDeclared(FName InterfaceName) const;

	// Returns if a given node output and node input are connected.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Connected") bool NodesAreConnected(const FMetaSoundBuilderNodeOutputHandle& OutputHandle, const FMetaSoundBuilderNodeInputHandle& InputHandle) const;

	// Returns if a given node input has connections.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Connected") bool NodeInputIsConnected(const FMetaSoundBuilderNodeInputHandle& InputHandle) const;

	// Returns if a given node output is connected.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Connected") bool NodeOutputIsConnected(const FMetaSoundBuilderNodeOutputHandle& OutputHandle) const;

	// Removes the interface with the given name from the builder's MetaSound. Removes any graph inputs
	// and outputs associated with the given interface and their respective connections (if any).
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void RemoveInterface(FName InterfaceName, EMetaSoundBuilderResult& OutResult);

	// Removes node and any associated connections from the builder's MetaSound.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void RemoveNode(const FMetaSoundNodeHandle& NodeHandle, EMetaSoundBuilderResult& OutResult);

#if WITH_EDITOR
	// Sets the author of the MetaSound.
	void SetAuthor(const FString& InAuthor);
#endif // WITH_EDITOR

	// Sets the node's input default value (used if no connection to the given node input is present)
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void SetNodeInputDefault(const FMetaSoundBuilderNodeInputHandle& NodeInputHandle, const FMetasoundFrontendLiteral& Literal, EMetaSoundBuilderResult& OutResult);

	virtual TScriptInterface<IMetaSoundDocumentInterface> Build(UObject* Parent, const FMetaSoundBuilderOptions& Options) const PURE_VIRTUAL(UMetaSoundBuilderBase::Build, return { }; );

	// Returns the builder's supported UClass
	virtual UClass& GetBuilderUClass() const PURE_VIRTUAL(UMetaSoundBuilderBase::GetBuilderUClass, return *UObject::StaticClass(); );

	virtual void PostInitProperties() override;

	// Initializes and ensures all nodes have a position (required prior to exporting to an asset if expected to be viewed in the editor).
	void InitNodeLocations();

protected:
	const FMetaSoundFrontendDocumentBuilder& GetConstBuilder() const
	{
		return Builder;
	}

	template <typename UClassType>
	TScriptInterface<IMetaSoundDocumentInterface> BuildInternal(UObject* Parent, const FMetaSoundBuilderOptions& BuilderOptions) const
	{
		FName ObjectName = BuilderOptions.Name;
		if (!ObjectName.IsNone())
		{
			ObjectName = MakeUniqueObjectName(Parent, UClassType::StaticClass(), BuilderOptions.Name);
		}

		UClassType* NewMetaSound = BuilderOptions.ExistingAsset
			? CastChecked<UClassType>(BuilderOptions.ExistingAsset.GetObject())
			: NewObject<UClassType>(Parent, ObjectName, RF_Public | RF_Transient);
		if (!NewMetaSound)
		{
			return nullptr;
		}

		FMetasoundFrontendDocument NewDocument = GetConstBuilder().GetDocument();
		{
			// This is required to ensure the newly build document has a unique class
			// identifier to avoid collisions if added to the Frontend class registry
			// (either below or at a later point in time).
			constexpr bool bResetVersion = false;
			FMetaSoundFrontendDocumentBuilder::InitGraphClassMetadata(NewDocument.RootGraph.Metadata, bResetVersion);
		}
		NewMetaSound->SetDocument(MoveTemp(NewDocument));
		NewMetaSound->ConformObjectDataToInterfaces();

		if (BuilderOptions.bAddToRegistry)
		{
			NewMetaSound->RegisterGraphWithFrontend();
		}

		UE_LOG(LogMetaSound, VeryVerbose, TEXT("New MetaSound '%s' built from '%s'"), *BuilderOptions.Name.ToString(), *GetFullName());
		return NewMetaSound;
	}

	UPROPERTY()
	FMetaSoundFrontendDocumentBuilder Builder;

	// Friending allows for swapping the builder in certain circumstances where desired (eg. attaching a builder to an existing asset)
	friend class UMetaSoundBuilderSubsystem;
};

/** Builder in charge of building a MetaSound */
UCLASS(Transient, BlueprintType)
class METASOUNDENGINE_API UMetaSoundPatchBuilder : public UMetaSoundBuilderBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (WorldContext = "Parent"))
	virtual UPARAM(DisplayName = "MetaSound") TScriptInterface<IMetaSoundDocumentInterface> Build(UObject* Parent, const FMetaSoundBuilderOptions& Options) const override;

	virtual UClass& GetBuilderUClass() const override;
};

/** Builder in charge of building a MetaSound Source */
UCLASS(Transient, BlueprintType)
class METASOUNDENGINE_API UMetaSoundSourceBuilder : public UMetaSoundBuilderBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (WorldContext = "Parent", AdvancedDisplay = "2"))
	void Audition(UObject* Parent, UAudioComponent* AudioComponent, FOnCreateAuditionGeneratorHandleDelegate OnCreateGenerator);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (WorldContext = "Parent"))
	virtual UPARAM(DisplayName = "MetaSound") TScriptInterface<IMetaSoundDocumentInterface> Build(UObject* Parent, const FMetaSoundBuilderOptions& Options) const override;

	// Sets the output audio format of the source
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void SetFormat(EMetaSoundOutputAudioFormat OutputFormat, EMetaSoundBuilderResult& OutResult);

	virtual UClass& GetBuilderUClass() const override;

	const Metasound::Engine::FOutputAudioFormatInfoPair* FindOutputAudioFormatInfo() const;

private:
	TWeakObjectPtr<UMetaSoundSource> AuditionSound;
};

/** The subsystem in charge of tracking MetaSound builders */
UCLASS()
class METASOUNDENGINE_API UMetaSoundBuilderSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FName, TObjectPtr<UMetaSoundPatchBuilder>> PatchBuilders;

	UPROPERTY()
	TMap<FName, TObjectPtr<UMetaSoundSourceBuilder>> SourceBuilders;

public:
	UMetaSoundSourceBuilder* AttachSourceBuilderToAsset(UMetaSoundSource* InSource) const;

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder",  meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Patch Builder") UMetaSoundPatchBuilder* CreatePatchBuilder(FName BuilderName, EMetaSoundBuilderResult& OutResult);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Source Builder") UMetaSoundSourceBuilder* CreateSourceBuilder(
		FName BuilderName,
		FMetaSoundBuilderNodeOutputHandle& OnPlayNodeOutput,
		FMetaSoundBuilderNodeInputHandle& OnFinishedNodeInput,
		TArray<FMetaSoundBuilderNodeInputHandle>& AudioOutNodeInputs,
		EMetaSoundBuilderResult& OutResult,
		EMetaSoundOutputAudioFormat OutputFormat = EMetaSoundOutputAudioFormat::Mono,
		bool bIsOneShot = true);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Bool Literal"))
	UPARAM(DisplayName = "Bool Literal") FMetasoundFrontendLiteral CreateBoolMetaSoundLiteral(bool Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Bool Array Literal"))
	UPARAM(DisplayName = "Bool Array Literal") FMetasoundFrontendLiteral CreateBoolArrayMetaSoundLiteral(const TArray<bool>& Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Float Literal"))
	UPARAM(DisplayName = "Float Literal") FMetasoundFrontendLiteral CreateFloatMetaSoundLiteral(float Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Float Array Literal"))
	UPARAM(DisplayName = "Float Array Literal") FMetasoundFrontendLiteral CreateFloatArrayMetaSoundLiteral(const TArray<float>& Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Int Literal"))
	UPARAM(DisplayName = "Int32 Literal") FMetasoundFrontendLiteral CreateIntMetaSoundLiteral(int32 Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Int Array Literal"))
	UPARAM(DisplayName = "Int32 Array Literal") FMetasoundFrontendLiteral CreateIntArrayMetaSoundLiteral(const TArray<int32>& Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Object Literal"))
	UPARAM(DisplayName = "Object Literal") FMetasoundFrontendLiteral CreateObjectMetaSoundLiteral(UObject* Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Object Array Literal"))
	UPARAM(DisplayName = "Object Array Literal") FMetasoundFrontendLiteral CreateObjectArrayMetaSoundLiteral(const TArray<UObject*>& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound String Literal"))
	UPARAM(DisplayName = "String Literal") FMetasoundFrontendLiteral CreateStringMetaSoundLiteral(const FString& Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound String Array Literal"))
	UPARAM(DisplayName = "String Array Literal") FMetasoundFrontendLiteral CreateStringArrayMetaSoundLiteral(const TArray<FString>& Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Literal From AudioParameter"))
	UPARAM(DisplayName = "Param Literal") FMetasoundFrontendLiteral CreateMetaSoundLiteralFromParam(const FAudioParameter& Param);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Patch Builder") UMetaSoundPatchBuilder* FindPatchBuilder(FName BuilderName);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Source Builder") UMetaSoundSourceBuilder* FindSourceBuilder(FName BuilderName);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Is Registered") bool IsInterfaceRegistered(FName InInterfaceName) const;

	// Adds builder to subsystem's registry to make it persistent and easily accessible by multiple systems or Blueprints
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	void RegisterPatchBuilder(FName BuilderName, UMetaSoundPatchBuilder* Builder);

	// Adds builder to subsystem's registry to make it persistent and easily accessible by multiple systems or Blueprints
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	void RegisterSourceBuilder(FName BuilderName, UMetaSoundSourceBuilder* Builder);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Unregistered") bool UnregisterPatchBuilder(FName BuilderName);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Unregistered") bool UnregisterSourceBuilder(FName BuilderName);
};
