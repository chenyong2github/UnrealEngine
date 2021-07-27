// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraphInputNodes.h"

#include "Algo/Count.h"
#include "Algo/Transform.h"
#include "Audio/AudioParameterInterface.h"
#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "GraphEditorSettings.h"
#include "MetasoundDataReference.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorSettings.h"
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

namespace Metasound
{
	namespace Editor
	{
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

FMetasoundFrontendClassName UMetasoundEditorGraphInputNode::GetClassName() const
{
	if (ensure(Input))
	{
		return Input->ClassName;
	}

	return Super::GetClassName();
}

void UMetasoundEditorGraphInputNode::UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const
{
	if (Input)
	{
		Input->UpdatePreviewInstance(InParameterName, InParameterInterface);
	}
}

FGuid UMetasoundEditorGraphInputNode::GetNodeID() const
{
	if (Input)
	{
		return Input->NodeID;
	}

	return Super::GetNodeID();
}

FLinearColor UMetasoundEditorGraphInputNode::GetNodeTitleColor() const
{
	if (const UMetasoundEditorSettings* EditorSettings = GetDefault<UMetasoundEditorSettings>())
	{
		return EditorSettings->InputNodeTitleColor;
	}

	return Super::GetNodeTitleColor();
}

FSlateIcon UMetasoundEditorGraphInputNode::GetNodeTitleIcon() const
{
	static const FName NativeIconName = "MetasoundEditor.Graph.Node.Class.Input";
	return FSlateIcon("MetaSoundStyle", NativeIconName);
}

#if WITH_EDITORONLY_DATA
void UMetasoundEditorGraphInputNode::PostEditUndo()
{
	Super::PostEditUndo();

	if (Input)
	{
		Input->OnLiteralChanged(false /* bPostTransaction */);
	}
}
#endif // WITH_EDITORONLY_DATA

void UMetasoundEditorGraphInputNode::SetNodeID(FGuid InNodeID)
{
	Input->NodeID = InNodeID;
}

FMetasoundFrontendLiteral UMetasoundEditorGraphInputBool::GetDefault() const 
{
	FMetasoundFrontendLiteral Literal;
	Literal.Set(Default.Value);
	return Literal;
}

EMetasoundFrontendLiteralType UMetasoundEditorGraphInputBool::GetLiteralType() const 
{
	return EMetasoundFrontendLiteralType::Boolean;
}

void UMetasoundEditorGraphInputBool::SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral) 
{
	Metasound::Editor::InputPrivate::ConvertLiteral<bool>(InLiteral, Default.Value);
}

void UMetasoundEditorGraphInputBool::UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const
	{
		InParameterInterface->SetBool(*InParameterName, Default.Value);
	}

FMetasoundFrontendLiteral UMetasoundEditorGraphInputBoolArray::GetDefault() const
	{
		TArray<bool> BoolArray;
		Algo::Transform(Default, BoolArray, [](const FMetasoundEditorGraphInputBoolRef& InValue) { return InValue.Value; });

		FMetasoundFrontendLiteral Literal;
		Literal.Set(BoolArray);
		return Literal;
	}

EMetasoundFrontendLiteralType UMetasoundEditorGraphInputBoolArray::GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::BooleanArray;
	}

void UMetasoundEditorGraphInputBoolArray::SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral)
	{
		Metasound::Editor::InputPrivate::ConvertLiteralToArray<bool, FMetasoundEditorGraphInputBoolRef>(InLiteral, Default);
	}

void UMetasoundEditorGraphInputBoolArray::UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const
{
	TArray<bool> BoolArray;
	Algo::Transform(Default, BoolArray, [](const FMetasoundEditorGraphInputBoolRef& InValue) { return InValue.Value; });
	InParameterInterface->SetBoolArray(*InParameterName, BoolArray);
}

FMetasoundFrontendLiteral UMetasoundEditorGraphInputInt::GetDefault() const
{
	FMetasoundFrontendLiteral Literal;
	Literal.Set(Default.Value);
	return Literal;
}

EMetasoundFrontendLiteralType UMetasoundEditorGraphInputInt::GetLiteralType() const
{
	return EMetasoundFrontendLiteralType::Integer;
}

void UMetasoundEditorGraphInputInt::SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral)
{
	Metasound::Editor::InputPrivate::ConvertLiteral<int32>(InLiteral, Default.Value);
}

void UMetasoundEditorGraphInputInt::UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const
{
	InParameterInterface->SetInt(*InParameterName, Default.Value);
}

FMetasoundFrontendLiteral UMetasoundEditorGraphInputIntArray::GetDefault() const
{
	TArray<int32> IntArray;
	Algo::Transform(Default, IntArray, [](const FMetasoundEditorGraphInputIntRef& InValue) { return InValue.Value; });

	FMetasoundFrontendLiteral Literal;
	Literal.Set(IntArray);

	return Literal;
}

EMetasoundFrontendLiteralType UMetasoundEditorGraphInputIntArray::GetLiteralType() const
{
	return EMetasoundFrontendLiteralType::IntegerArray;
}

void UMetasoundEditorGraphInputIntArray::SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral)
{
	Metasound::Editor::InputPrivate::ConvertLiteralToArray<int32, FMetasoundEditorGraphInputIntRef>(InLiteral, Default);
}

void UMetasoundEditorGraphInputIntArray::UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const
{
	TArray<int32> IntArray;
	Algo::Transform(Default, IntArray, [](const FMetasoundEditorGraphInputIntRef& InValue) { return InValue.Value; });
	InParameterInterface->SetIntArray(*InParameterName, IntArray);
}

FMetasoundFrontendLiteral UMetasoundEditorGraphInputFloat::GetDefault() const
{
	FMetasoundFrontendLiteral Literal;
	Literal.Set(Default);
	return Literal;
}

EMetasoundFrontendLiteralType UMetasoundEditorGraphInputFloat::GetLiteralType() const
{
	return EMetasoundFrontendLiteralType::Float;
}

void UMetasoundEditorGraphInputFloat::SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral)
{
	Metasound::Editor::InputPrivate::ConvertLiteral<float>(InLiteral, Default);
}

void UMetasoundEditorGraphInputFloat::UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const
{
	InParameterInterface->SetFloat(*InParameterName, Default);
}

FMetasoundFrontendLiteral UMetasoundEditorGraphInputFloatArray::GetDefault() const
{
	FMetasoundFrontendLiteral Literal;
	Literal.Set(Default);
	return Literal;
}

EMetasoundFrontendLiteralType UMetasoundEditorGraphInputFloatArray::GetLiteralType() const
{
	return EMetasoundFrontendLiteralType::FloatArray;
}

void UMetasoundEditorGraphInputFloatArray::SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral)
{
	Metasound::Editor::InputPrivate::ConvertLiteralToArray<float>(InLiteral, Default);
}

void UMetasoundEditorGraphInputFloatArray::UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const
{
	InParameterInterface->SetFloatArray(*InParameterName, Default);
}

FMetasoundFrontendLiteral UMetasoundEditorGraphInputString::GetDefault() const
{
	FMetasoundFrontendLiteral Literal;
	Literal.Set(Default);
	return Literal;
}

EMetasoundFrontendLiteralType UMetasoundEditorGraphInputString::GetLiteralType() const
{
	return EMetasoundFrontendLiteralType::String;
}

void UMetasoundEditorGraphInputString::SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral)
{
	Metasound::Editor::InputPrivate::ConvertLiteral<FString>(InLiteral, Default);
}

void UMetasoundEditorGraphInputString::UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const
{
	InParameterInterface->SetString(*InParameterName, Default);
}

FMetasoundFrontendLiteral UMetasoundEditorGraphInputStringArray::GetDefault() const
{
	FMetasoundFrontendLiteral Literal;
	Literal.Set(Default);
	return Literal;
}

EMetasoundFrontendLiteralType UMetasoundEditorGraphInputStringArray::GetLiteralType() const
{
	return EMetasoundFrontendLiteralType::StringArray;
}

void UMetasoundEditorGraphInputStringArray::SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral)
{
	Metasound::Editor::InputPrivate::ConvertLiteralToArray<FString>(InLiteral, Default);
}

void UMetasoundEditorGraphInputStringArray::UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const
{
	InParameterInterface->SetStringArray(*InParameterName, Default);
}

FMetasoundFrontendLiteral UMetasoundEditorGraphInputObject::GetDefault() const
{
	FMetasoundFrontendLiteral Literal;
	Literal.Set(Default.Object);
	return Literal;
}

EMetasoundFrontendLiteralType UMetasoundEditorGraphInputObject::GetLiteralType() const
{
	return EMetasoundFrontendLiteralType::UObject;
}

void UMetasoundEditorGraphInputObject::SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral)
{
	ensure(InLiteral.TryGet(Default.Object));
}

void UMetasoundEditorGraphInputObject::UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const
{
	// TODO. We need proxy object here safely.
}

FMetasoundFrontendLiteral UMetasoundEditorGraphInputObjectArray::GetDefault() const
{
	TArray<UObject*> ObjectArray;
	Algo::Transform(Default, ObjectArray, [](const FMetasoundEditorGraphInputObjectRef& InValue) { return InValue.Object; });

	FMetasoundFrontendLiteral Literal;
	Literal.Set(ObjectArray);
	return Literal;
}

EMetasoundFrontendLiteralType UMetasoundEditorGraphInputObjectArray::GetLiteralType() const
{
	return EMetasoundFrontendLiteralType::UObjectArray;
}

void UMetasoundEditorGraphInputObjectArray::SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral)
{
	TArray<UObject*> ObjectArray;
	ensure(InLiteral.TryGet(ObjectArray));

	Algo::Transform(ObjectArray, Default, [](UObject* InValue) { return FMetasoundEditorGraphInputObjectRef { InValue }; });
}

void UMetasoundEditorGraphInputObjectArray::UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const
{
	TArray<UObject*> ObjectArray;
	Algo::Transform(Default, ObjectArray, [](const FMetasoundEditorGraphInputObjectRef& InValue) { return InValue.Object; });
	// TODO. We need proxy object here safely.
}
