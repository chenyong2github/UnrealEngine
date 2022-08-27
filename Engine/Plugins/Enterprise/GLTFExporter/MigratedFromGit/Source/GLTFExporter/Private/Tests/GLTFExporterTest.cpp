// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Serialization/BufferArchive.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

#include "GLTFExporter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace {

// TODO Turn into config variable?
const TCHAR* TargetFilePath = TEXT("Tests/targets.json");
const TCHAR* ParamDelimiter = TEXT("c8a4fd9d525c0ac433fd7d24ce2a3eca");

} // anonymous namespace

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FGLTFExporterTest, "Unreal2glTF.Export Test", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FGLTFExporterTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray <FString>& OutTestCommands) const
{
	const FString TargetFilePathAbsolute = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TargetFilePath);
	FString TargetFileContent = "";
	FFileHelper::LoadFileToString(TargetFileContent, *TargetFilePathAbsolute);

	TArray<TSharedPtr<FJsonValue>> TargetJsonRoot = {};
	const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(TargetFileContent);

	if (FJsonSerializer::Deserialize(JsonReader, TargetJsonRoot))
	{
		const int32 TargetJsonElementCount = TargetJsonRoot.Num();

		for (int32 TargetJsonArrayElementIndex = 0; TargetJsonArrayElementIndex < TargetJsonElementCount; ++TargetJsonArrayElementIndex) {
			const TSharedPtr<FJsonObject>& TargetJsonElement = TargetJsonRoot[TargetJsonArrayElementIndex]->AsObject();
			const FString AssetField = TargetJsonElement->GetStringField("asset");
			const FString ControlField = TargetJsonElement->GetStringField("control");
			const FString ParamElements[] = { AssetField, ControlField };

			OutBeautifiedNames.Add(FString(TEXT("Test target with index ")) + FString::FromInt(TargetJsonArrayElementIndex));
			OutTestCommands.Add(FString::Join(ParamElements, ParamDelimiter));
		}
	}
}

bool FGLTFExporterTest::RunTest(const FString& Parameters)
{
	FString AssetPath = "";
	FString ControlFilePath = "";
	Parameters.Split(ParamDelimiter, &AssetPath, &ControlFilePath);

	UObject* ObjectToExport = LoadObject<UObject>(nullptr, *AssetPath, nullptr, LOAD_None, nullptr);

	if (ObjectToExport == nullptr) {
		AddError(FString::Printf(TEXT("Failed to find test asset %s"), *AssetPath));

		return false;
	}

	FBufferArchive BufferArchive = {};

	if (!UExporter::ExportToArchive(ObjectToExport, nullptr, BufferArchive, TEXT("gltf"), 0)) {
		AddError(FString::Printf(TEXT("Export failed for asset %s"), *AssetPath));

		return false;
	}

	const FString ControlFilePathAbsolute = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ControlFilePath);
	FString ControlFileContent = "";

	if (!FFileHelper::LoadFileToString(ControlFileContent, *ControlFilePathAbsolute)) {
		AddError(FString::Printf(TEXT("Failed to find test control file %s"), *ControlFilePathAbsolute));

		return false;
	}

	const FString ExportedText = FString(BufferArchive.Num(), UTF8_TO_TCHAR(BufferArchive.GetData()));

	if (!ExportedText.Equals(ControlFileContent)) {
		AddError(FString::Printf(TEXT("Exported GLTF for the asset %s did not match the control"), *AssetPath));

		return false;
	}

	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
