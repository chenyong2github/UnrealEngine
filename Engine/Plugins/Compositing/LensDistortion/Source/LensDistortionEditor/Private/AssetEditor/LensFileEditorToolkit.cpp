// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetEditor/LensFileEditorToolkit.h"

#include "LensFile.h"
#include "Toolkits/SimpleAssetEditor.h"
#include "UI/LensDistortionEditorStyle.h"


#define LOCTEXT_NAMESPACE "LensFileEditorToolkit"


TSharedRef<FLensFileEditorToolkit> FLensFileEditorToolkit::CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, ULensFile* InLensFile)
{
	TSharedRef<FLensFileEditorToolkit> NewEditor = MakeShared<FLensFileEditorToolkit>();

	NewEditor->InitLensFileEditor(Mode, InitToolkitHost, InLensFile);

	return NewEditor;
}

void FLensFileEditorToolkit::InitLensFileEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, ULensFile* InLensFile)
{
	TArray<UObject*> ObjectsToEdit;
	ObjectsToEdit.Add(InLensFile);

	Super::InitEditor(Mode, InitToolkitHost, ObjectsToEdit, FGetDetailsViewObjects());
}

FLensFileEditorToolkit::~FLensFileEditorToolkit()
{

}


void FLensFileEditorToolkit::SaveAsset_Execute()
{
	Super::SaveAsset_Execute();
}

void FLensFileEditorToolkit::SaveAssetAs_Execute()
{
	Super::SaveAssetAs_Execute();
}

bool FLensFileEditorToolkit::OnRequestClose()
{
	return Super::OnRequestClose();
}

#undef LOCTEXT_NAMESPACE