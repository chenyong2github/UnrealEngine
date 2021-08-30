// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceControlOperations.h"

#include "Containers/StringView.h"
#include "ISourceControlModule.h"
#include "Misc/PathViews.h"
#include "Misc/Paths.h"

FDownloadFile::FDownloadFile(FStringView InTargetDirectory, EVerbosity InVerbosity)
	: Verbosity(InVerbosity)
	, TargetDirectory(InTargetDirectory)
{
	FPaths::NormalizeDirectoryName(TargetDirectory);

	// Due to the asynchronous nature of the source control api, it might be some time before
	// the TargetDirectory is actually used. So we do a validation pass on it now to try and 
	// give errors close to the point that they were created. The caller is still free to try
	// and use the FDownloadFile but with an invalid path it probably won't work.
	FText Reason;
	if (!FPaths::ValidatePath(TargetDirectory, &Reason))
	{
		UE_LOG(LogSourceControl, Error, TEXT("Path '%s' passed to FDownloadFile is invalid due to: %s"), 
			*TargetDirectory, *Reason.ToString());
	}
}

FCreateWorkspace::FCreateWorkspace(FStringView InWorkspaceName, FStringView InWorkspaceRoot)
	: WorkspaceName(InWorkspaceName)
{
	TStringBuilder<512> AbsoluteWorkspaceName;
	FPathViews::ToAbsolutePath(InWorkspaceRoot, AbsoluteWorkspaceName);

	WorkspaceRoot = AbsoluteWorkspaceName.ToString();
}
