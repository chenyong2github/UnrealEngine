// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Algo/Transform.h"
#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundFrontendRegistries.h"
#include "Misc/Guid.h"
#include "Sound/SoundWave.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPath.h"

#include "MetasoundEditorGraphInputNodes.generated.h"

// Forward Declarations
class UEdGraphPin;
class UMetasound;

namespace Metasound
{
	namespace Editor
	{
		class FGraphBuilder;
	}
}


UCLASS()
class METASOUNDEDITOR_API UMetasoundEditorGraphInputNode : public UMetasoundEditorGraphNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputNode() = default;

	virtual FMetasoundFrontendLiteral GetLiteralDefault() const
	{
		return FMetasoundFrontendLiteral();
	}


	void UpdateDocumentInput() const
	{
		using namespace Metasound::Frontend;

		FConstNodeHandle NodeHandle = GetNodeHandle();
		const FString& NodeName = NodeHandle->GetNodeName();

		FGraphHandle GraphHandle = GetRootGraphHandle();
		const FGuid VertexID = GraphHandle->GetVertexIDForInputVertex(NodeName);
		GraphHandle->SetDefaultInput(VertexID, GetLiteralDefault());
	}

	virtual void UpdatePreviewInstance(const Metasound::FVertexKey& InParamterName, IAudioInstanceTransmitter& InInstanceTransmitter) const {}

	void OnLiteralChanged() 
	{
		UpdateDocumentInput();

		if (UMetasoundEditorGraph* MetasoundGraph = Cast<UMetasoundEditorGraph>(GetGraph()))
		{
			if (IAudioInstanceTransmitter* Transmitter = MetasoundGraph->GetMetasoundInstanceTransmitter())
			{
				// TODO: fix how this parameter name is determined. It should not be done with a "DisplayName" but rather 
				// the vertex "Name". Currently user entered vertex names only have their "Names" stored as "DisplayNames" 
				TArray<Metasound::Frontend::FConstInputHandle> Inputs = GetNodeHandle()->GetConstInputs();

				// An input node should only have one input. 
				if (ensure(Inputs.Num() == 1))
				{
					Metasound::FVertexKey VertexKey = Metasound::FVertexKey(Inputs[0]->GetDisplayName().ToString());
					UpdatePreviewInstance(VertexKey, *Transmitter);
				}
			}
		}
	}

#if WITH_EDITORONLY_DATA
	virtual void PostEditUndo() override
	{
		Super::PostEditUndo();

		OnLiteralChanged();
	}
#endif // WITH_EDITORONLY_DATA

	FName GetLiteralDefaultPropertyFName() const
	{
		return "Default";
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::None;
	}

protected:
	UPROPERTY()
	FName InputTypeName;

	friend class Metasound::Editor::FGraphBuilder;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputBoolNode : public UMetasoundEditorGraphInputNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputBoolNode() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	bool Default = false;

	virtual FMetasoundFrontendLiteral GetLiteralDefault() const override
	{
		FMetasoundFrontendLiteral Literal;
		Literal.Set(Default);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::Boolean;
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const override
	{
		InInstanceTransmitter.SetParameter(*InParameterName, Default);
	}
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputBoolArrayNode : public UMetasoundEditorGraphInputNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputBoolArrayNode() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<bool> Default;

	virtual FMetasoundFrontendLiteral GetLiteralDefault() const override
	{
		FMetasoundFrontendLiteral Literal;
		Literal.Set(Default);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::BooleanArray;
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const override
	{
		InInstanceTransmitter.SetParameter(*InParameterName, TArray<bool>{Default});
	}
};

// Broken out to be able to customize and swap enum behavior for basic integer literal behavior
USTRUCT()
struct FMetasoundEditorGraphInputInt
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	int32 Value = 0;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputIntNode : public UMetasoundEditorGraphInputNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputIntNode() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	FMetasoundEditorGraphInputInt Default;

	virtual FMetasoundFrontendLiteral GetLiteralDefault() const override
	{
		FMetasoundFrontendLiteral Literal;
		Literal.Set(Default.Value);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::Integer;
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const override
	{
		InInstanceTransmitter.SetParameter(*InParameterName, Default.Value);
	}
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputIntArrayNode : public UMetasoundEditorGraphInputNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputIntArrayNode() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<FMetasoundEditorGraphInputInt> Default;

	virtual FMetasoundFrontendLiteral GetLiteralDefault() const override
	{
		TArray<int32> Values;
		for (const FMetasoundEditorGraphInputInt& Value : Default)
		{
			Values.Add(Value.Value);
		}

		FMetasoundFrontendLiteral Literal;
		Literal.Set(Values);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::IntegerArray;
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const override
	{
		TArray<int32> IntArray;
		Algo::Transform(Default, IntArray, [](const FMetasoundEditorGraphInputInt& InValue) { return InValue.Value; });

		InInstanceTransmitter.SetParameter(*InParameterName, TArray<int32>{ IntArray });
	}
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputFloatNode : public UMetasoundEditorGraphInputNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputFloatNode() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	float Default = 0.f;

	virtual FMetasoundFrontendLiteral GetLiteralDefault() const override
	{
		FMetasoundFrontendLiteral Literal;
		Literal.Set(Default);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::Float;
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const override
	{
		InInstanceTransmitter.SetParameter(*InParameterName, Default);
	}
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputFloatArrayNode : public UMetasoundEditorGraphInputNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputFloatArrayNode() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<float> Default;

	virtual FMetasoundFrontendLiteral GetLiteralDefault() const override
	{
		FMetasoundFrontendLiteral Literal;
		Literal.Set(Default);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::FloatArray;
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const override
	{
		InInstanceTransmitter.SetParameter(*InParameterName, TArray<float>{Default});
	}
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputStringNode : public UMetasoundEditorGraphInputNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputStringNode() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	FString Default;

	virtual FMetasoundFrontendLiteral GetLiteralDefault() const override
	{
		FMetasoundFrontendLiteral Literal;
		Literal.Set(Default);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::String;
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const override
	{
		InInstanceTransmitter.SetParameter(*InParameterName, FString{ Default });
	}
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputStringArrayNode : public UMetasoundEditorGraphInputNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputStringArrayNode() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<FString> Default;

	virtual FMetasoundFrontendLiteral GetLiteralDefault() const override
	{
		FMetasoundFrontendLiteral Literal;
		Literal.Set(Default);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::StringArray;
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const override
	{
		InInstanceTransmitter.SetParameter(*InParameterName, TArray<FString>{Default});
	}
};

// Broken out to be able to customize and swap AllowedClass based on provided object proxy
USTRUCT()
struct FMetasoundEditorGraphInputObject
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	UObject* Object = nullptr;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputObjectNode : public UMetasoundEditorGraphInputNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputObjectNode() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	FMetasoundEditorGraphInputObject Default;

	virtual FMetasoundFrontendLiteral GetLiteralDefault() const override
	{
		FMetasoundFrontendLiteral Literal;
		Literal.Set(Default.Object);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::UObject;
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const override
	{
		// TODO. We need proxy object here safely.
		//InInstanceTransmitter.SetParameter(*InParameterName, Default.Object);
	}
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputObjectArrayNode : public UMetasoundEditorGraphInputNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputObjectArrayNode() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<FMetasoundEditorGraphInputObject> Default;

	virtual FMetasoundFrontendLiteral GetLiteralDefault() const override
	{
		TArray<UObject*> Objects;
		for (const FMetasoundEditorGraphInputObject& InputObject : Default)
		{
			Objects.Add(InputObject.Object);
		}

		FMetasoundFrontendLiteral Literal;
		Literal.Set(Objects);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::UObjectArray;
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const override
	{
		TArray<UObject*> ObjectArray;
		Algo::Transform(Default, ObjectArray, [](const FMetasoundEditorGraphInputObject& InValue) { return InValue.Object; });
		
		// TODO. We need proxy object here safely.
		//InInstanceTransmitter.SetParameter(*InParameterName, Default.Object);
	}
};
