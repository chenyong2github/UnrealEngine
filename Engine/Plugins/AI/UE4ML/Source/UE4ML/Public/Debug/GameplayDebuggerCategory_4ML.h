// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_GAMEPLAY_DEBUGGER

#include "GameplayDebuggerCategory.h"
#include "4MLTypes.h"


class U4MLAgent;

class FGameplayDebuggerCategory_4ML : public FGameplayDebuggerCategory
{
public:
	FGameplayDebuggerCategory_4ML();

	virtual void CollectData(APlayerController* OwnerPC, AActor* DebugActor) override;
	virtual void DrawData(APlayerController* OwnerPC, FGameplayDebuggerCanvasContext& CanvasContext) override;

	static TSharedRef<FGameplayDebuggerCategory> MakeInstance();

protected:
	void ResetProps();

	void OnShowNextAgent();
	void OnRequestAvatarUpdate();
	void OnSetAvatarAsDebugAgent();

	void OnCurrentSessionChanged();
	void OnAgentAvatarChanged(U4MLAgent& Agent, AActor* OldAvatar);
	void OnBeginAgentRemove(U4MLAgent& Agent);

	void Init();

protected:
	AActor* CachedDebugActor;
	F4ML::FAgentID CachedAgentID;
};

#endif // WITH_GAMEPLAY_DEBUGGER
