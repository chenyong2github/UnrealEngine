// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "UObject/ObjectMacros.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundFrontendRegistries.h"
#include "Misc/Guid.h"
#include "Sound/SoundWave.h"
#include "UObject/SoftObjectPath.h"

#include "MetasoundEditorGraphInputNodes.generated.h"

// Forward Declarations
class UEdGraphPin;
class UMetasound;


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

	FName GetLiteralDefaultPropertyFName() const
	{
		return "Default";
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::None;
	}
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
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputIntNode : public UMetasoundEditorGraphInputNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputIntNode() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	int32 Default = 0;

	virtual FMetasoundFrontendLiteral GetLiteralDefault() const override
	{
		FMetasoundFrontendLiteral Literal;
		Literal.Set(Default);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::Integer;
	}
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputIntArrayNode : public UMetasoundEditorGraphInputNode
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputIntArrayNode() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<int32> Default;

	virtual FMetasoundFrontendLiteral GetLiteralDefault() const override
	{
		FMetasoundFrontendLiteral Literal;
		Literal.Set(Default);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::IntegerArray;
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
};