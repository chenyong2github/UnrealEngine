#pragma once
#include "CoreMinimal.h"

class FSceneViewFamily;

class SLevelViewportToolBar;
struct FToolMenuSection;
class SLevelViewport;

class UNREALED_API ICustomEditorStaticScreenPercentage
{
public:
	virtual ~ICustomEditorStaticScreenPercentage() {};

	virtual void SetupEditorViewFamily(FSceneViewFamily& ViewFamily, float PreviewResolutionFraction, bool bPreviewCustomTemporalUpscaler) = 0;
	
	struct FViewportMenuEntryArguments
	{
		FToolMenuSection* Section;
		SLevelViewportToolBar* ToolBar;
		TSharedPtr<SLevelViewport> Viewport;
	};
	virtual bool GenerateEditorViewportOptionsMenuEntry(const FViewportMenuEntryArguments& Arguments) = 0;
};

extern UNREALED_API ICustomEditorStaticScreenPercentage* GCustomEditorStaticScreenPercentage;
