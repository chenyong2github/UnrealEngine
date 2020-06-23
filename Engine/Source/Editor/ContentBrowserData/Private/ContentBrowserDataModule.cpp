// Copyright Epic Games, Inc. All Rights Reserved.

#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSubsystem.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"

class FContentBrowserDataModule : public IContentBrowserDataModule
{
public:
	virtual UContentBrowserDataSubsystem* GetSubsystem() const override
	{
		return GEditor ? GEditor->GetEditorSubsystem<UContentBrowserDataSubsystem>() : nullptr;
	}
};

IMPLEMENT_MODULE(FContentBrowserDataModule, ContentBrowserData);
