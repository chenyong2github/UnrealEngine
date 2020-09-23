// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchboardEditorStyle.h"

#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"

const FName FSwitchboardEditorStyle::NAME_SwitchboardBrush = "Img.Switchboard.Small";


FSwitchboardEditorStyle::FSwitchboardEditorStyle()
	: FSlateStyleSet("SwitchboardEditorStyle")
{
	FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("Switchboard"))->GetContentDir();
	SetContentRoot(ContentDir);
	SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	Set(NAME_SwitchboardBrush, new FSlateImageBrush(RootToContentDir(TEXT("Icons/Switchboard_40x.png")), FVector2D(40.0f, 40.0f)));
	
	FSlateStyleRegistry::RegisterSlateStyle(*this);
}

FSwitchboardEditorStyle::~FSwitchboardEditorStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}

FSwitchboardEditorStyle& FSwitchboardEditorStyle::Get()
{
	static FSwitchboardEditorStyle Inst;
	return Inst;
}
