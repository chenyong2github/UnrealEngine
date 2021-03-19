// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Algo/Count.h"
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

	UPROPERTY()
	TObjectPtr<UMetasoundEditorGraphInput> Input;

public:
	virtual FMetasoundFrontendClassName GetClassName() const override
	{
		if (ensure(Input))
		{
			return Input->ClassName;
		}

		return Super::GetClassName();
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const
	{
		if (ensure(Input))
		{
			Input->UpdatePreviewInstance(InParameterName, InInstanceTransmitter);
		}
	}
	
	virtual FGuid GetNodeID() const override
	{
		if (ensure(Input))
		{
			return Input->NodeID;
		}

		return Super::GetNodeID();
	}

#if WITH_EDITORONLY_DATA
	virtual void PostEditUndo() override
	{
		Super::PostEditUndo();

		if (ensure(Input))
		{
			Input->OnLiteralChanged();
		}
	}
#endif // WITH_EDITORONLY_DATA

protected:
	virtual void SetNodeID(FGuid InNodeID) override
	{
		Input->NodeID = InNodeID;
	}

	friend class Metasound::Editor::FGraphBuilder;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputBool : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputBool() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	bool Default = false;

	virtual FMetasoundFrontendLiteral GetDefault() const override
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
class UMetasoundEditorGraphInputBoolArray : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputBoolArray() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<bool> Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override
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
struct FMetasoundEditorGraphInputIntRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	int32 Value = 0;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputInt : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputInt() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	FMetasoundEditorGraphInputIntRef Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override
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
class UMetasoundEditorGraphInputIntArray : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputIntArray() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<FMetasoundEditorGraphInputIntRef> Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override
	{
		TArray<int32> IntArray;
		Algo::Transform(Default, IntArray, [](const FMetasoundEditorGraphInputIntRef& InValue) { return InValue.Value; });

		FMetasoundFrontendLiteral Literal;
		Literal.Set(IntArray);

		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::IntegerArray;
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const override
	{
		TArray<int32> IntArray;
		Algo::Transform(Default, IntArray, [](const FMetasoundEditorGraphInputIntRef& InValue) { return InValue.Value; });

		InInstanceTransmitter.SetParameter(*InParameterName, TArray<int32>{ IntArray });
	}
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputFloat : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputFloat() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	float Default = 0.f;

	virtual FMetasoundFrontendLiteral GetDefault() const override
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
class UMetasoundEditorGraphInputFloatArray : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputFloatArray() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<float> Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override
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
class UMetasoundEditorGraphInputString : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputString() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	FString Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override
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
class UMetasoundEditorGraphInputStringArray : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputStringArray() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<FString> Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override
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
struct FMetasoundEditorGraphInputObjectRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	UObject* Object = nullptr;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputObject : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputObject() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	FMetasoundEditorGraphInputObjectRef Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override
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
class UMetasoundEditorGraphInputObjectArray : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputObjectArray() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<FMetasoundEditorGraphInputObjectRef> Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override
	{
		TArray<UObject*> ObjectArray;
		Algo::Transform(Default, ObjectArray, [](const FMetasoundEditorGraphInputObjectRef& InValue) { return InValue.Object; });

		FMetasoundFrontendLiteral Literal;
		Literal.Set(ObjectArray);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::UObjectArray;
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const override
	{
		TArray<UObject*> ObjectArray;
		Algo::Transform(Default, ObjectArray, [](const FMetasoundEditorGraphInputObjectRef& InValue) { return InValue.Object; });
		
		// TODO. We need proxy object here safely.
		//InInstanceTransmitter.SetParameter(*InParameterName, Default.Object);
	}
};