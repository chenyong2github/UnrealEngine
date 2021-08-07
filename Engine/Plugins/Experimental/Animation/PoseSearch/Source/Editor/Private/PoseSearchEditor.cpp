// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchEditor.h"
#include "Animation/AnimSequence.h"
#include "Modules/ModuleManager.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "IAnimationEditor.h"
#include "IPersonaToolkit.h"
#include "IPersonaPreviewScene.h"
#include "PoseSearchDebugger.h"
#include "Trace/PoseSearchTraceAnalyzer.h"
#include "Trace/PoseSearchTraceModule.h"


//////////////////////////////////////////////////////////////////////////
// FEditorCommands

namespace UE { namespace PoseSearch {

struct FEditorCommands
{
	static void DrawSearchIndex();
};

void FEditorCommands::DrawSearchIndex()
{
	FDebugDrawParams DrawParams;
	DrawParams.DefaultLifeTime = 60.0f;
	DrawParams.Flags = EDebugDrawFlags::DrawSearchIndex;

	TArray<UObject*> EditedAssets = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->GetAllEditedAssets();
	for (UObject* EditedAsset : EditedAssets)
	{
		if (UAnimSequence* Sequence = Cast<UAnimSequence>(EditedAsset))
		{
			const UPoseSearchSequenceMetaData* MetaData = Sequence->FindMetaDataByClass<UPoseSearchSequenceMetaData>();
			if (MetaData)
			{
				DrawParams.SequenceMetaData = MetaData;
				IAssetEditorInstance* EditorInstance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Sequence, true /*bFocusIfOpen*/);
				if (EditorInstance && EditorInstance->GetEditorName() == TEXT("AnimationEditor"))
				{
					IAnimationEditor* Editor = static_cast<IAnimationEditor*>(EditorInstance);
					TSharedRef<IPersonaToolkit> Toolkit = Editor->GetPersonaToolkit();
					TSharedRef<IPersonaPreviewScene> Scene = Toolkit->GetPreviewScene();

					DrawParams.World = Scene->GetWorld();
					Draw(DrawParams);
				}
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////
// FPoseSearchEditorModule

class FEditorModule : public IPoseSearchEditorModuleInterface
{
public:
	void StartupModule() override;
	void ShutdownModule() override;

private:
	TArray<class IConsoleObject*> ConsoleCommands;
	
	/** Creates the view for the Rewind Debugger */
	TSharedPtr<FDebuggerViewCreator> DebuggerViewCreator;
	/** Enables dedicated PoseSearch trace module */
	TSharedPtr<FTraceModule> TraceModule;
};

void FEditorModule::StartupModule()
{
	if (GIsEditor && !IsRunningCommandlet())
	{
		FDebugger::Initialize();
		TraceModule = MakeShared<FTraceModule>();
		DebuggerViewCreator = MakeShared<FDebuggerViewCreator>();

		IModularFeatures::Get().RegisterModularFeature("RewindDebuggerViewCreator", DebuggerViewCreator.Get());
		IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, TraceModule.Get());
		
		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("a.PoseSearch.DrawSearchIndex"),
			TEXT("Draw the search index for the selected asset"),
			FConsoleCommandDelegate::CreateStatic(&FEditorCommands::DrawSearchIndex),
			ECVF_Default
		));
	}
}

void FEditorModule::ShutdownModule()
{
	for (IConsoleObject* ConsoleCmd : ConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ConsoleCmd);
	}
	ConsoleCommands.Empty();
	
	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, TraceModule.Get());
	FDebugger::Shutdown();
}

}} // namespace UE::PoseSearch

IMPLEMENT_MODULE(UE::PoseSearch::FEditorModule, PoseSearchEditor);
