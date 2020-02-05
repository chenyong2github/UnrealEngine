// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/CommandLine.h"
#include "Engine/DeveloperSettings.h"
#include "MoviePipelineNewProcessExecutorSettings.generated.h"

/**
* This is the implementation responsible for executing the rendering of
* multiple movie pipelines on the local machine in an external process.
* This simply handles launching and managing the external processes and 
* acts as a proxy to them where possible. This internally uses the
* UMoviePipelineInProcessExecutor on the launched instances.
*/
UCLASS(BlueprintType, config = Editor, defaultconfig, meta=(DisplayName = "Movie Render Pipeline New Process") )
class MOVIERENDERPIPELINEEDITOR_API UMoviePipelineNewProcessExecutorSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	
public:
	UMoviePipelineNewProcessExecutorSettings()
		: UDeveloperSettings()
	{
		bCloseEditor = false;
		AdditionalCommandLineArguments = TEXT("-NoLoadingScreen -FixedSeed -log -Unattended");

		// Find all arguments from the command line and set them as the InheritedCommandLineArguments.
		TArray<FString> Tokens, Switches;
		FCommandLine::Parse(FCommandLine::Get(), Tokens, Switches);
		for (auto& Switch : Switches)
		{
			InheritedCommandLineArguments.AppendChar('-');
			InheritedCommandLineArguments.Append(Switch);
			InheritedCommandLineArguments.AppendChar(' ');
		}
	}
	
		/** Gets the settings container name for the settings, either Project or Editor */
	virtual FName GetContainerName() const override { return FName("Project"); }
	/** Gets the category for the settings, some high level grouping like, Editor, Engine, Game...etc. */
	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** If enabled the editor will close itself when a new process is started. This can be used to gain some performance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = "Movie Render Pipeline")
	bool bCloseEditor;

	/** A list of additional command line arguments to be appended to the new process startup. In the form of "-foo -bar=baz". Can be useful if your game requires certain arguments to start such as disabling log-in screens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = "Movie Render Pipeline")
	FString AdditionalCommandLineArguments;

	/** A list of command line arguments which are inherited from the currently running Editor instance that will be automatically appended to the new process. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, config, Category = "Movie Render Pipeline")
	FString InheritedCommandLineArguments;
};