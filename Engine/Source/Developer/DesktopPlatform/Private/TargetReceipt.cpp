// Copyright Epic Games, Inc. All Rights Reserved.

#include "TargetReceipt.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

bool FTargetReceipt::Read(const FString& FileName)
{
	// Read the file from disk
	FString FileContents;
	if (!FFileHelper::LoadFileToString(FileContents, *FileName))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Object;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContents);
	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		return false;
	}

	// Get the project file
	FString RelativeProjectFile;
	if(Object->TryGetStringField(TEXT("Project"), RelativeProjectFile))
	{
		ProjectFile = FPaths::Combine(FPaths::GetPath(FileName), RelativeProjectFile);
		FPaths::MakeStandardFilename(ProjectFile);
		ProjectDir = FPaths::GetPath(ProjectFile);
	}

	// Get the target name
	if (!Object->TryGetStringField(TEXT("TargetName"), TargetName))
	{
		return false;
	}
	if (!Object->TryGetStringField(TEXT("Platform"), Platform))
	{
		return false;
	}
	if (!Object->TryGetStringField(TEXT("Architecture"), Architecture))
	{
		return false;
	}

	// Read the configuration
	FString ConfigurationString;
	if (!Object->TryGetStringField(TEXT("Configuration"), ConfigurationString) || !LexTryParseString(Configuration, *ConfigurationString))
	{
		return false;
	}

	// Read the target type
	FString TargetTypeString;
	if (!Object->TryGetStringField(TEXT("TargetType"), TargetTypeString) || !LexTryParseString(TargetType, *TargetTypeString))
	{
		return false;
	}

	// Get the launch path
	if(!Object->TryGetStringField(TEXT("Launch"), Launch))
	{
		return false;
	}
	ExpandVariables(Launch);

	// Read the list of build products
	const TArray<TSharedPtr<FJsonValue>>* BuildProductsArray;
	if (Object->TryGetArrayField(TEXT("BuildProducts"), BuildProductsArray))
	{
		for(const TSharedPtr<FJsonValue>& BuildProductValue : *BuildProductsArray)
		{
			const TSharedPtr<FJsonObject>* BuildProductObject;
			if(!BuildProductValue->TryGetObject(BuildProductObject))
			{
				return false;
			}

			FBuildProduct BuildProduct;
			if(!(*BuildProductObject)->TryGetStringField(TEXT("Type"), BuildProduct.Type) || !(*BuildProductObject)->TryGetStringField(TEXT("Path"), BuildProduct.Path))
			{
				return false;
			}

			ExpandVariables(BuildProduct.Path);

			BuildProducts.Add(MoveTemp(BuildProduct));
		}
	}

	// Read the list of runtime dependencies
	const TArray<TSharedPtr<FJsonValue>>* RuntimeDependenciesArray;
	if (Object->TryGetArrayField(TEXT("RuntimeDependencies"), RuntimeDependenciesArray))
	{
		for(const TSharedPtr<FJsonValue>& RuntimeDependencyValue : *RuntimeDependenciesArray)
		{
			const TSharedPtr<FJsonObject>* RuntimeDependencyObject;
			if(!RuntimeDependencyValue->TryGetObject(RuntimeDependencyObject))
			{
				return false;
			}

			FRuntimeDependency RuntimeDependency;
			if(!(*RuntimeDependencyObject)->TryGetStringField(TEXT("Path"), RuntimeDependency.Path) || !(*RuntimeDependencyObject)->TryGetStringField(TEXT("Type"), RuntimeDependency.Type))
			{
				return false;
			}

			ExpandVariables(RuntimeDependency.Path);

			RuntimeDependencies.Add(MoveTemp(RuntimeDependency));
		}
	}

	// Read the list of additional properties
	const TArray<TSharedPtr<FJsonValue>>* AdditionalPropertiesArray;
	if (Object->TryGetArrayField(TEXT("AdditionalProperties"), AdditionalPropertiesArray))
	{
		for(const TSharedPtr<FJsonValue>& AdditionalPropertyValue : *AdditionalPropertiesArray)
		{
			const TSharedPtr<FJsonObject>* AdditionalPropertyObject;
			if(!AdditionalPropertyValue->TryGetObject(AdditionalPropertyObject))
			{
				return false;
			}

			FReceiptProperty Property;
			if(!(*AdditionalPropertyObject)->TryGetStringField(TEXT("Name"), Property.Name) || !(*AdditionalPropertyObject)->TryGetStringField(TEXT("Value"), Property.Value))
			{
				return false;
			}

			ExpandVariables(Property.Value);

			AdditionalProperties.Add(MoveTemp(Property));
		}
	}

	return true;
}

FString FTargetReceipt::GetDefaultPath(const TCHAR* BaseDir, const TCHAR* TargetName, const TCHAR* Platform, EBuildConfiguration Configuration, const TCHAR* BuildArchitecture)
{
	const TCHAR* ArchitectureSuffix = TEXT("");
	if (BuildArchitecture != nullptr)
	{
		ArchitectureSuffix = BuildArchitecture;
	}

	if ((BuildArchitecture == nullptr || BuildArchitecture[0] == 0) && Configuration == EBuildConfiguration::Development)
	{
		return FPaths::Combine(BaseDir, FString::Printf(TEXT("Binaries/%s/%s.target"), Platform, TargetName));
	}
	else
	{
		return FPaths::Combine(BaseDir, FString::Printf(TEXT("Binaries/%s/%s-%s-%s%s.target"), Platform, TargetName, Platform, LexToString(Configuration), ArchitectureSuffix));
	}
}

void FTargetReceipt::ExpandVariables(FString& Path)
{
	static FString EngineDirPrefix = TEXT("$(EngineDir)");
	static FString ProjectDirPrefix = TEXT("$(ProjectDir)");
	if(Path.StartsWith(EngineDirPrefix))
	{
		FString EngineDir = FPaths::EngineDir();
		if (EngineDir.Len() > 0 && EngineDir[EngineDir.Len() - 1] == '/')
		{
			EngineDir = EngineDir.Left(EngineDir.Len() - 1);
		}
		Path = EngineDir + Path.Mid(EngineDirPrefix.Len());
	}
	else if(Path.StartsWith(ProjectDirPrefix) && ProjectDir.Len() > 0)
	{
		Path = ProjectDir + Path.Mid(ProjectDirPrefix.Len());
	}
}
