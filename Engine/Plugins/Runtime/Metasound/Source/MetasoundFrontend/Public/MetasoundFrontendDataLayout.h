// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundGraph.h"
#include "MetasoundFrontendDataLayout.generated.h"

UENUM()
enum class EMetasoundClassType : uint8
{
	// Any classes of the external type are implemented in C++..
	External,
	// Objects with this type are Metasound graphs unto themselves,
	// they can be used like any other node.
	MetasoundGraph,
	// Input into the owning metasound. Only used in FMetasoundNodeDescription.
	Input,
	//Output from the owning metasound. Only used in FMetasoundNodeDescription.
	Output,
	Invalid UMETA(Hidden)
};

// The type of a given literal for an input value.
UENUM()
enum class EMetasoundLiteralType : uint8
{
	None,
	Bool,
	Float,
	Integer,
	String,
	UObject,
	UObjectArray,
	Invalid UMETA(Hidden)
};

USTRUCT()
struct FMetasoundClassMetadata
{
	GENERATED_BODY()

	// the title for this metasound object. Will be the way the metasound is described in the content browser.
	UPROPERTY()
	FString NodeName;

	UPROPERTY(EditAnywhere, Category = General)
	int32 MajorVersion = 1;

	UPROPERTY(EditAnywhere, Category = General)
	int32 MinorVersion = 0;

	// This will always be set to EMetasoundObjectType::Metasound.
	UPROPERTY(VisibleAnywhere, Category = General, meta = (DisplayName = "Type"))
	EMetasoundClassType NodeType;

	// Optional longform description of what this Metasound does. Can be displayed in a tooltip.
	UPROPERTY(EditAnywhere, Category = General, meta = (DisplayName = "Description"))
	FText MetasoundDescription;

	// Prompt that will will show if Metasound is missing as a dependency listed in another Metasound.
	// Can optionally provide hints as to how to obtain this Metasound (ex. documentation URL, Plugin name).
	UPROPERTY()
	FText PromptIfMissing;

	// Original author of this Metasound Object.
	UPROPERTY(EditAnywhere, Category = General, meta = (DisplayName = "Author"))
	FText AuthorName;
};


// Represents the serialized version of variant literal types. Currently, only
// support bools, integers, floats, and strings are supported as literals.
USTRUCT()
struct FMetasoundLiteralDescription
{
	GENERATED_BODY()

	// The actual type of this literal.
	UPROPERTY(EditAnywhere, Category = Customized)
	EMetasoundLiteralType LiteralType;

	UPROPERTY(EditAnywhere, Category = Customized)
	bool AsBool;

	UPROPERTY(EditAnywhere, Category = Customized)
	int32 AsInteger;

	UPROPERTY(EditAnywhere, Category = Customized)
	float AsFloat;

	UPROPERTY(EditAnywhere, Category = Customized)
	FString AsString;

	UPROPERTY(EditAnywhere, Category = Customized)
	UObject* AsUObject = nullptr;

	UPROPERTY(EditAnywhere, Category = Customized)
	TArray<UObject*> AsUObjectArray;
};

USTRUCT()
struct FMetasoundInputDescription
{
	GENERATED_BODY()

	// The name of this input.
	UPROPERTY(VisibleAnywhere, Category = CustomView)
	FString Name;

	// The name of this input displayed to users.
	UPROPERTY(EditAnywhere, Category = Parameters, meta = (DisplayName = "Name"))
	FText DisplayName;

	// The type of this input. This type matches with a type declared via the DECLARE_METASOUND_DATA_REFERENCE_TYPES macro.
	UPROPERTY(EditAnywhere, Category = Parameters)
	FName TypeName;

	// Optional description text about this input.
	UPROPERTY(EditAnywhere, Category = Parameters)
	FText ToolTip;

	// The optional literal value, if we have one.
	// NOTE: in the future we'll have a specific details customization for this.
	UPROPERTY(EditAnywhere, Category = Parameters)
	FMetasoundLiteralDescription LiteralValue;
};

USTRUCT()
struct FMetasoundOutputDescription
{
	GENERATED_BODY()

	// The descriptive name of this output.
	UPROPERTY(VisibleAnywhere, Category = CustomView)
	FString Name;

	// The name of this input displayed to users.
	UPROPERTY(EditAnywhere, Category = Parameters, meta = (DisplayName = "Name"))
	FText DisplayName;

	// The type of this output. This type matches with a type declared via the DECLARE_METASOUND_DATA_REFERENCE_TYPES macro.
	UPROPERTY(EditAnywhere, Category = Parameters)
	FName TypeName;

	// Optional description text about this output.
	UPROPERTY(EditAnywhere, Category = Parameters)
	FText ToolTip;
};

USTRUCT() 
struct FMetasoundEnvironmentVariableDescription
{
	GENERATED_BODY()

	// The name of this environment variable.
	UPROPERTY(VisibleAnywhere, Category = CustomView)
	FString Name;

	// The name of this input displayed to users.
	UPROPERTY(EditAnywhere, Category = Parameters, meta = (DisplayName = "Name"))
	FText DisplayName;

	// Optional description text about this input.
	UPROPERTY(EditAnywhere, Category = Parameters)
	FText ToolTip;
};

// This is used to describe a single incoming connection or literal for a given input on a FMetasoundNodeDescription.
USTRUCT()
struct FMetasoundNodeConnectionDescription
{
	GENERATED_BODY()

	static constexpr int32 DisconnectedNodeID = INDEX_NONE;

	// Name of the input this connection is for. Should match an input name in the Inputs array of this node's FMetasoundClassDescription.
	UPROPERTY()
	FString InputName;

	// Unique ID for the node this connection is from.
	UPROPERTY()
	int32 NodeID;

	// FMetasoundOutputDescription::Name of the output this connection is from if NodeID is valid,
	// if FMetasoundNodeConnectionDescription::DisconnectedNodeID.
	UPROPERTY()
	FString OutputName;

	// The optional literal value, if we have one.
	UPROPERTY()
	FMetasoundLiteralDescription LiteralValue;
};

// description for an individual node within the FMetasoundGraphDescription.
// corresponds to an instance of a FMetasoundObjectDescription listed in the Dependencies array.
USTRUCT()
struct FMetasoundNodeDescription
{
	GENERATED_BODY()

	static constexpr int32 InvalidID = INDEX_NONE;

	// Unique integer given to this node. Only has to be unique within the Nodes array.
	UPROPERTY()
	int32 UniqueID;

	UPROPERTY()
	int32 DependencyID;

	// This will either match a FMetasoundObjectDescription::Metadata::Name in the Dependencies list of the owning FMetasoundObjectDescription,
	// Or it will match a FMetasoundInputDescription::Name or FMetasoundOutputDescription::Name, depending on the value of ObjectTypeOfNode.
	// TODO: Now that each dependency class has a unique ID, we can consider using that rather than the full name.
	UPROPERTY()
	FString Name;

	// This describes what type of node this object represents.
	UPROPERTY()
	EMetasoundClassType ObjectTypeOfNode;

	//TODO: See if we need to explicitly list the typename of the input/output if ObjectTypeOfNode is Input or Output.

	// list of connections or literal values for each connected input pin on this node.
	UPROPERTY()
	TArray<FMetasoundNodeConnectionDescription> InputConnections;

	// todo: create way to control init params for specific node instances.
	UPROPERTY()
	TMap<FName, FMetasoundLiteralDescription> StaticParameters;
};

USTRUCT()
struct FMetasoundGraphDescription
{
	GENERATED_BODY()

	// TODO: pp - Break out FMetasoundNodeDescription to be a list of connections 
	// and a list of nodes. Currently FMetasoudnNodeConnectionDescription lives 
	// on the input node, but would be better living on the graph. 
	//
	// NodeConnections TMap<Tuple(FromNodeId, ToNodeId), {VertexName, VertexName}>?
	//
	UPROPERTY()
	TArray<FMetasoundNodeDescription> Nodes;
};

USTRUCT()
struct FMetasoundExternalClassLookupInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FName ExternalNodeClassName;

	UPROPERTY()
	uint32 ExternalNodeClassHash;
};

USTRUCT()
struct FMetasoundEditorData
{
	GENERATED_BODY()

	UPROPERTY()
	FString GraphData;
};

// Full saved out description of a Metasound object,
// which is anything that could be used as a node in a graph.
// It is not called "FMetasoundNodeDescription" to avoid confusion with the individual node objects that compose
// an asset's Metasound graph.
USTRUCT()
struct FMetasoundClassDescription
{
	GENERATED_BODY()

	static constexpr int32 RootClassID = 0;
	static constexpr int32 InvalidID = INDEX_NONE;

	// Unique ID for this FMetasoundClassDescription in FMetasoundDocument::Dependencies.
	// this will be set to FMetasoundClassDescription::RootClassID for FMetasoundDocument::RootClass.
	UPROPERTY()
	int32 UniqueID;

	UPROPERTY(EditAnywhere, Category = CustomView)
	FMetasoundClassMetadata Metadata;

	UPROPERTY(EditAnywhere, Category = CustomView)
	TArray<FMetasoundInputDescription> Inputs;

	UPROPERTY(EditAnywhere, Category = CustomView)
	TArray<FMetasoundOutputDescription> Outputs;

	UPROPERTY(EditAnywhere, Category = CustomView)
	TArray<FMetasoundEnvironmentVariableDescription> EnvironmentVariables;

	// If this object is itself of the Metasound type, here we list the unique IDs of other objects it depends on to be built.
	// These unique IDs are then found in FMetasoundDocument::Dependencies.
	UPROPERTY()
	TArray<int32> DependencyIDs;

	// If this object is itself of the Metasound type, We can fully describe that metasound's graph here.
	// If this object is a Metasound and this Graph is empty,
	// we will attempt to find the implemented version of this Metasound in the asset registry using Metasound::Frontend::FindGraphHandleForClass.
	UPROPERTY()
	FMetasoundGraphDescription Graph;

	// If this object is an external node type (implemented in C++), this name is used to quickly look up the class in the Node Registry.
	UPROPERTY()
	FMetasoundExternalClassLookupInfo ExternalNodeClassLookupInfo;
};

// This is used to describe the required inputs and outputs for a metasound, and is used to make sure we can use a metasound graph for specific applications.
// For example, a UMetasoundSource needs to generate audio, so its RequiredOutputs will contain "MainAudioOutput"
USTRUCT()
struct FMetasoundArchetype
{
	GENERATED_BODY()

	// Name of the archetype we're using.
	UPROPERTY()
	FName ArchetypeName;

	// whatever inputs are required to support this archetype.
	// call Special Inputs
	UPROPERTY()
	TArray<FMetasoundInputDescription> RequiredInputs;

	// whatever outputs are required to support this archetype.
	// call Special Outputs
	UPROPERTY()
	TArray<FMetasoundOutputDescription> RequiredOutputs;

	// The environment variables supplied by the archetype.
	UPROPERTY()
	TArray<FMetasoundEnvironmentVariableDescription> EnvironmentVariables;
};

// Outermost description of a single metasound.
USTRUCT()
struct FMetasoundDocument
{
	GENERATED_BODY()

	// description of what kind of metasound this is going to be (i.e. source effect, playable source, generic node, etc).
	UPROPERTY()
	FMetasoundArchetype Archetype;

	// The highest level Metasound Class in this document.
	// Typically of type MetasoundGraph, contains graph of the node classes listed in Dependencies.
	UPROPERTY(EditAnywhere, Category = CustomView)
	FMetasoundClassDescription RootClass;

	// this is the list of every dependency required by the RootClass, as well as nested dependencies.
	UPROPERTY()
	TArray<FMetasoundClassDescription> Dependencies;

	UPROPERTY()
	FMetasoundEditorData EditorData;
};
