// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonSerializerMacros.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"

namespace NNX 
{
namespace Test
{
namespace Json
{
	struct FTestConfigTensor : FJsonSerializable
	{
		TArray<int32> Shape;
		FString Type;
		//TODO Extend:
		// by adding custom data
		// by adding custom initializer or parameter (range, distribution)
		// by adding data type
		// by adding source CPU or GPU resident data

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE_ARRAY("shape", Shape);
			JSON_SERIALIZE("data_type", Type);
			//JSON_SERIALIZE("on_gpu", OnGpu);
			//JSON_SERIALIZE("range", ...);
			//JSON_SERIALIZE("data_source", DataSource);
		END_JSON_SERIALIZER
	};

	struct FTestConfigRuntime : FJsonSerializable
	{
		FString Name;
		bool Skip;
		float AbsoluteError;
		float RelativeError;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("name", Name);
			JSON_SERIALIZE("skip", Skip);
			JSON_SERIALIZE("absolute_error", AbsoluteError);
			JSON_SERIALIZE("relative_error", RelativeError);
		END_JSON_SERIALIZER
	};

	struct FTestConfigDataset : FJsonSerializable
	{
		TArray<FTestConfigTensor> Inputs;
		TArray<FTestConfigTensor> Outputs;
		TArray<FTestConfigRuntime> Runtimes;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE_ARRAY_SERIALIZABLE("inputs", Inputs, FTestConfigTensor);
			JSON_SERIALIZE_ARRAY_SERIALIZABLE("outputs", Outputs, FTestConfigTensor);
			JSON_SERIALIZE_ARRAY_SERIALIZABLE("runtimes", Runtimes, FTestConfigRuntime);
		END_JSON_SERIALIZER
	};

	struct FTestConfigTarget : FJsonSerializable
	{
		FString Target;
		TArray<FString> Tags;
		TArray<FString> AdditionalDatasets;
		TArray<FString> RemovedDatasets;
		bool Skip;
		float AbsoluteError;
		float RelativeError;
		FString InputType;
		FString OutputType;
		TArray<FTestConfigRuntime> Runtimes;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("target", Target);
			JSON_SERIALIZE("skip", Skip);
			JSON_SERIALIZE("absolute_error", AbsoluteError);
			JSON_SERIALIZE("relative_error", RelativeError);
			JSON_SERIALIZE("input_type", InputType);
			JSON_SERIALIZE("output_type", OutputType);
			JSON_SERIALIZE_ARRAY_SERIALIZABLE("runtimes", Runtimes, FTestConfigRuntime);
			JSON_SERIALIZE_ARRAY("tags", Tags);
			JSON_SERIALIZE_ARRAY("additional_datasets", AdditionalDatasets);
			JSON_SERIALIZE_ARRAY("removed_datasets", RemovedDatasets);
		END_JSON_SERIALIZER
	};

	struct FTestCategory : FJsonSerializable
	{
		FString Category;
		TArray<FTestConfigTarget> Targets;
		TArray<FTestConfigRuntime> Runtimes;
		bool Skip;
		bool IsModelTest;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("category", Category);
			JSON_SERIALIZE("skip", Skip);
			JSON_SERIALIZE("is_model_test", IsModelTest);
			JSON_SERIALIZE_ARRAY_SERIALIZABLE("targets", Targets, FTestConfigTarget);
			JSON_SERIALIZE_ARRAY_SERIALIZABLE("runtimes", Runtimes, FTestConfigRuntime);
		END_JSON_SERIALIZER
	};

	struct FTestConfigInputOutputSet : FJsonSerializable
	{
		FString Name;
		TArray<FTestConfigDataset> Datasets;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("name", Name);
			JSON_SERIALIZE_ARRAY_SERIALIZABLE("datasets", Datasets, FTestConfigDataset);
		END_JSON_SERIALIZER
	};

	bool LoadTestDescriptionFromJson(const FString& FullPath,
		TArray<FTestCategory>& ModelTestCategories,
		TArray<FTestCategory>& OperatorCategories,
		TArray<FTestConfigInputOutputSet>& InputOutputSets)
	{
		ModelTestCategories.Empty();
		OperatorCategories.Empty();
		InputOutputSets.Empty();

		FString JsonContent;
		FFileHelper::LoadFileToString(JsonContent, *FullPath);
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonContent);
		TSharedPtr<FJsonObject> GlobalJsonObject = MakeShareable(new FJsonObject);

		if (!FJsonSerializer::Deserialize(JsonReader, GlobalJsonObject))
		{
			return false;
		}

		if (!GlobalJsonObject.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* JsonArrayValue;
		if (GlobalJsonObject->TryGetArrayField(TEXT("model_test_categories"), JsonArrayValue))
		{
			for (TSharedPtr<FJsonValue> JsonValue : *JsonArrayValue)
			{
				const TSharedPtr<FJsonObject>* JsonObject = nullptr;
				if (!JsonValue->TryGetObject(JsonObject))
				{
					return false;
				}
				ModelTestCategories.Emplace_GetRef().FromJson(*JsonObject);
			}
		}

		if (GlobalJsonObject->TryGetArrayField(TEXT("operator_test_categories"), JsonArrayValue))
		{
			for (TSharedPtr<FJsonValue> JsonValue : *JsonArrayValue)
			{
				const TSharedPtr<FJsonObject>* JsonObject = nullptr;
				if (!JsonValue->TryGetObject(JsonObject))
				{
					return false;
				}
				OperatorCategories.Emplace_GetRef().FromJson(*JsonObject);
			}
		}

		if (GlobalJsonObject->TryGetArrayField(TEXT("input_output_sets"), JsonArrayValue))
		{
			for (TSharedPtr<FJsonValue> JsonValue : *JsonArrayValue)
			{
				const TSharedPtr<FJsonObject>* JsonObject = nullptr;
				if (!JsonValue->TryGetObject(JsonObject))
				{
					return false;
				}
				InputOutputSets.Emplace_GetRef().FromJson(*JsonObject);
			}
		}
		return true;
	}

} // namespace Json
} // namespace Test
} // namespace NNX
