// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UnrealExporter.h"
#include "Serialization/BufferArchive.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "GLTFExporter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace {

struct TestDefinition {
	TCHAR* AssetPath;
	TCHAR* ControlFilePath;
} TestDefinitions[] = {
	{
		TEXT("StaticMesh'/Engine/EngineMeshes/Cube.Cube'"),
		TEXT("Tests/Cube/Cube.gltf")
	}
};

const TCHAR* ParamDelimiter = TEXT("c8a4fd9d525c0ac433fd7d24ce2a3eca");

} // anonymous namespace

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FGLTFExporterTest, "Unreal2glTF.Export Test", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FGLTFExporterTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray <FString>& OutTestCommands) const
{
	const int32 TestDefinitionCount = sizeof(TestDefinitions) / sizeof(TestDefinition);

	for (int32 TestDefinitionIndex = 0; TestDefinitionIndex < TestDefinitionCount; ++TestDefinitionIndex) {
		const TestDefinition& CurrentTestDefinition = TestDefinitions[TestDefinitionIndex];
		const FString Elements[] = { CurrentTestDefinition.AssetPath, CurrentTestDefinition.ControlFilePath };

		OutBeautifiedNames.Add(FString(TEXT("Test target with index ")) + FString::FromInt(TestDefinitionIndex));
		OutTestCommands.Add(FString::Join(Elements, ParamDelimiter));
	}
}

bool FGLTFExporterTest::RunTest(const FString& Parameters)
{
	FString AssetPath = "";
	FString ControlFilePath = "";
	Parameters.Split(ParamDelimiter, &AssetPath, &ControlFilePath);

	UObject* ObjectToExport = LoadObject<UObject>(NULL, *AssetPath, NULL, LOAD_None, NULL);

	if (ObjectToExport == nullptr) {
		AddError(FString::Printf(TEXT("Failed to find test asset %s"), *AssetPath));

		return false;
	}

	FBufferArchive BufferArchive;
	UExporter::ExportToArchive(ObjectToExport, nullptr, BufferArchive, TEXT("gltf"), 0);
	const FString ExportedText = BytesToString(BufferArchive.GetData(), BufferArchive.Num());

	const FString AbsoluteControlFilePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ControlFilePath);
	FString ControlFileContent = "";

	if (!FFileHelper::LoadFileToString(ControlFileContent, *AbsoluteControlFilePath)) {
		AddError(FString::Printf(TEXT("Failed to find test control file %s"), *AbsoluteControlFilePath));

		return false;
	}

	if (!ExportedText.Equals(ControlFileContent)) {
		AddError(FString::Printf(TEXT("Exported GLTF for the asset %s did not match the expected result"), *AssetPath));

		return false;
	}

	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
