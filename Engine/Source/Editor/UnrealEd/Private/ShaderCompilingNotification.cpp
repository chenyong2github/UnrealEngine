// Copyright Epic Games, Inc. All Rights Reserved.


#include "CoreMinimal.h"
#include "GlobalEditorNotification.h"
#include "ShaderCompiler.h"
#include "Widgets/Notifications/SNotificationList.h"

/** Notification class for asynchronous shader compiling. */
class FShaderCompilingNotificationImpl : public FGlobalEditorProgressNotification
{
public:
	FShaderCompilingNotificationImpl()
		: FGlobalEditorProgressNotification(NSLOCTEXT("ShaderCompile", "ShaderCompileInProgress", "Compiling Shaders"))
	{

	}

protected:
	virtual bool AllowedToStartNotification() const override
	{
		return AllowShaderCompiling() && GShaderCompilingManager->ShouldDisplayCompilingNotification();
	}
	virtual int32 UpdateProgress()
	{
		const int32 RemainingJobs = GShaderCompilingManager->IsCompiling() ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
		if (RemainingJobs > 0)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("ShaderJobs"), FText::AsNumber(GShaderCompilingManager->GetNumRemainingJobs()));
			UpdateProgressMessage(FText::Format(NSLOCTEXT("ShaderCompile", "ShaderCompileInProgressFormat", "Compiling Shaders ({ShaderJobs})"), Args));

		}

		return RemainingJobs;
	}
};

/** Global notification object. */
FShaderCompilingNotificationImpl GShaderCompilingNotification;

