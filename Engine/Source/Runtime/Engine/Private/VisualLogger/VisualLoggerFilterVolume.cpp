#include "VisualLogger/VisualLoggerFilterVolume.h"

AVisualLoggerFilterVolume::AVisualLoggerFilterVolume(const FObjectInitializer& ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bIsEditorOnlyActor = true;
}