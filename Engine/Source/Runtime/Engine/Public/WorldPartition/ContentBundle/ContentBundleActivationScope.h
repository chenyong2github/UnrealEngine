// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldPartition/ContentBundle/ContentBundleEditorSubsystemInterface.h"

class FContentBundleEditor;

#if WITH_EDITOR

class FContentBundleActivationScope
{
public:
	FContentBundleActivationScope(FGuid InContentBundleGuid)
	{
		if (InContentBundleGuid.IsValid())
		{
			IContentBundleEditorSubsystemInterface* ContentBundleEditorSubsystem = IContentBundleEditorSubsystemInterface::Get();
			if (TSharedPtr<FContentBundleEditor> ContentBundleEditor = ContentBundleEditorSubsystem->GetEditorContentBundle(InContentBundleGuid))
			{
				check(!ContentBundleEditorSubsystem->IsEditingContentBundle());

				ContentBundleEditorSubsystem->ActivateContentBundleEditing(ContentBundleEditor);
				ContentBundleGuid = InContentBundleGuid;
			}
		}
	}
	~FContentBundleActivationScope()
	{
		if (ContentBundleGuid.IsValid())
		{
			IContentBundleEditorSubsystemInterface* ContentBundleEditorSubsystem = IContentBundleEditorSubsystemInterface::Get();
			if (TSharedPtr<FContentBundleEditor> ContentBundleEditor = ContentBundleEditorSubsystem->GetEditorContentBundle(ContentBundleGuid))
			{
				ContentBundleEditorSubsystem->DeactivateContentBundleEditing(ContentBundleEditor);
			}
		}
	}

private:
	FGuid ContentBundleGuid;
};

#endif
