// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Algo/Count.h"
#include "Algo/Transform.h"
#include "Audio/AudioParameterInterface.h"
#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "MetasoundDataReference.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundPrimitives.h"
#include "Misc/Guid.h"
#include "Sound/SoundWave.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPath.h"

#include "MetasoundEditorGraphInputNodes.generated.h"

// Forward Declarations
class UEdGraphPin;
class UMetaSound;

namespace Metasound
{
	namespace Editor
	{
		// Forward Declarations
		class FGraphBuilder;

		namespace InputPrivate
		{
			template <typename TPODType>
			void ConvertLiteral(const FMetasoundFrontendLiteral& InLiteral, TPODType& OutValue)
			{
				const FName& TypeName = GetMetasoundDataTypeName<TPODType>();
				OutValue = InLiteral.ToLiteral(TypeName).Value.Get<TPODType>();
			}

			template <typename TPODType, typename TLiteralType = TPODType>
			void ConvertLiteralToArray(const FMetasoundFrontendLiteral& InLiteral, TArray<TLiteralType>& OutArray)
			{
				const FName& TypeName = *FString(GetMetasoundDataTypeString<TPODType>() + TEXT(":Array"));
				TArray<TPODType> NewValue = InLiteral.ToLiteral(TypeName).Value.Get<TArray<TPODType>>();
				Algo::Transform(NewValue, OutArray, [](const TPODType& InValue) { return TLiteralType { InValue }; });
			}
		}
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

	virtual void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const
	{
		if (Input)
		{
			Input->UpdatePreviewInstance(InParameterName, InParameterInterface);
		}
	}
	
	virtual FGuid GetNodeID() const override
	{
		if (Input)
		{
			return Input->NodeID;
		}

		return Super::GetNodeID();
	}

#if WITH_EDITORONLY_DATA
	virtual void PostEditUndo() override
	{
		Super::PostEditUndo();

		if (Input)
		{
			Input->OnLiteralChanged(false /* bPostTransaction */);
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

// Broken out to be able to customize and swap enum behavior for basic integer literal behavior
USTRUCT()
struct FMetasoundEditorGraphInputBoolRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	bool Value = false;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputBool : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputBool() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	FMetasoundEditorGraphInputBoolRef Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override
	{
		FMetasoundFrontendLiteral Literal;
		Literal.Set(Default.Value);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const override
	{
		return EMetasoundFrontendLiteralType::Boolean;
	}

	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override
	{
		Metasound::Editor::InputPrivate::ConvertLiteral<bool>(InLiteral, Default.Value);
	}

	virtual void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const override
	{
		InParameterInterface->SetBool(*InParameterName, Default.Value);
	}
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphInputBoolArray : public UMetasoundEditorGraphInputLiteral
{
	GENERATED_BODY()

public:
	virtual ~UMetasoundEditorGraphInputBoolArray() = default;

	UPROPERTY(EditAnywhere, Category = DefaultValue)
	TArray<FMetasoundEditorGraphInputBoolRef> Default;

	virtual FMetasoundFrontendLiteral GetDefault() const override
	{
		TArray<bool> BoolArray;
		Algo::Transform(Default, BoolArray, [](const FMetasoundEditorGraphInputBoolRef& InValue) { return InValue.Value; });

		FMetasoundFrontendLiteral Literal;
		Literal.Set(BoolArray);
		return Literal;
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const override
	{
		return EMetasoundFrontendLiteralType::BooleanArray;
	}

	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override
	{
		Metasound::Editor::InputPrivate::ConvertLiteralToArray<bool, FMetasoundEditorGraphInputBoolRef>(InLiteral, Default);
	}

	virtual void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const override
	{
		TArray<bool> BoolArray;
		Algo::Transform(Default, BoolArray, [](const FMetasoundEditorGraphInputBoolRef& InValue) { return InValue.Value; });
		InParameterInterface->SetBoolArray(*InParameterName, BoolArray);
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

	virtual EMetasoundFrontendLiteralType GetLiteralType() const override
	{
		return EMetasoundFrontendLiteralType::Integer;
	}

	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override
	{
		Metasound::Editor::InputPrivate::ConvertLiteral<int32>(InLiteral, Default.Value);
	}

	virtual void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const override
	{
		InParameterInterface->SetInt(*InParameterName, Default.Value);
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

	virtual EMetasoundFrontendLiteralType GetLiteralType() const override
	{
		return EMetasoundFrontendLiteralType::IntegerArray;
	}

	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override
	{
		Metasound::Editor::InputPrivate::ConvertLiteralToArray<int32, FMetasoundEditorGraphInputIntRef>(InLiteral, Default);
	}

	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const override
	{
		TArray<int32> IntArray;
		Algo::Transform(Default, IntArray, [](const FMetasoundEditorGraphInputIntRef& InValue) { return InValue.Value; });
		InParameterInterface->SetIntArray(*InParameterName, IntArray);
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

	virtual EMetasoundFrontendLiteralType GetLiteralType() const override
	{
		return EMetasoundFrontendLiteralType::Float;
	}

	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override
	{
		Metasound::Editor::InputPrivate::ConvertLiteral<float>(InLiteral, Default);
	}

	virtual void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const override
	{
		InParameterInterface->SetFloat(*InParameterName, Default);
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

	virtual EMetasoundFrontendLiteralType GetLiteralType() const override
	{
		return EMetasoundFrontendLiteralType::FloatArray;
	}

	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override
	{
		Metasound::Editor::InputPrivate::ConvertLiteralToArray<float>(InLiteral, Default);
	}

	virtual void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const override
	{
		InParameterInterface->SetFloatArray(*InParameterName, Default);
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

	virtual EMetasoundFrontendLiteralType GetLiteralType() const override
	{
		return EMetasoundFrontendLiteralType::String;
	}

	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override
	{
		Metasound::Editor::InputPrivate::ConvertLiteral<FString>(InLiteral, Default);
	}

	virtual void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const override
	{
		InParameterInterface->SetString(*InParameterName, Default);
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

	virtual EMetasoundFrontendLiteralType GetLiteralType() const override
	{
		return EMetasoundFrontendLiteralType::StringArray;
	}

	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override
	{
		Metasound::Editor::InputPrivate::ConvertLiteralToArray<FString>(InLiteral, Default);
	}

	virtual void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const override
	{
		InParameterInterface->SetStringArray(*InParameterName, Default);
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

	virtual EMetasoundFrontendLiteralType GetLiteralType() const override
	{
		return EMetasoundFrontendLiteralType::UObject;
	}

	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override
	{
		ensure(InLiteral.TryGet(Default.Object));
	}

	virtual void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const override
	{
		// TODO. We need proxy object here safely.
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

	virtual EMetasoundFrontendLiteralType GetLiteralType() const override
	{
		return EMetasoundFrontendLiteralType::UObjectArray;
	}

	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) override
	{
		TArray<UObject*> ObjectArray;
		ensure(InLiteral.TryGet(ObjectArray));

		Algo::Transform(ObjectArray, Default, [](UObject* InValue) { return FMetasoundEditorGraphInputObjectRef { InValue }; });
	}

	virtual void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const override
	{
		TArray<UObject*> ObjectArray;
		Algo::Transform(Default, ObjectArray, [](const FMetasoundEditorGraphInputObjectRef& InValue) { return InValue.Object; });
		// TODO. We need proxy object here safely.
	}
};