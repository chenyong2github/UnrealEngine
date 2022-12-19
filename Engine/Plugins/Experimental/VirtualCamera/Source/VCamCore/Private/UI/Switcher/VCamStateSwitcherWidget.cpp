// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Switcher/VCamStateSwitcherWidget.h"

#include "UI/Switcher/WidgetConnectionConfig.h"

DEFINE_LOG_CATEGORY(LogVCamStateSwitcher);

FName UVCamStateSwitcherWidget::DefaultState("Default");

bool UVCamStateSwitcherWidget::SetCurrentState(FName NewState, bool bForceUpdate)
{
	if (NewState == CurrentState && !bForceUpdate)
	{
		return false;
	}
	
	const FVCamWidgetConnectionState* StateConfig = States.Find(NewState);
	if (!StateConfig)
	{
		UE_LOG(LogVCamStateSwitcher, Warning, TEXT("Unknown connection state %s"), *NewState.ToString());
		return false;
	}

	const FName OldState = CurrentState;
	OnPreStateChanged.Broadcast(this, OldState, NewState);
	
	for (const FWidgetConnectionConfig& WidgetConfig : StateConfig->WidgetConfigs)
	{
		UVCamWidget* Widget = WidgetConfig.ResolveWidget(this);
		if (!Widget)
		{
			UE_CLOG(!WidgetConfig.HasNoWidgetSet(), LogVCamStateSwitcher, Warning, TEXT("Failed to find widget %s (NewState: %s)"), *WidgetConfig.Widget.ToString(), *NewState.ToString());
			continue;
		}

		EConnectionUpdateResult UpdateResult;
		Widget->UpdateConnectionTargets(WidgetConfig.ConnectionTargets, true, UpdateResult);
	}
	
	CurrentState = NewState;
	OnPostStateChanged.Broadcast(this, OldState, CurrentState);
	return true;
}

TArray<FName> UVCamStateSwitcherWidget::GetStates() const
{
	TArray<FName> Keys;
	States.GenerateKeyArray(Keys);
	return Keys;
}

bool UVCamStateSwitcherWidget::GetStateInfo(FName State, FVCamWidgetConnectionState& OutStateInfo) const
{
	if (const FVCamWidgetConnectionState* StateInfo = States.Find(State))
	{
		OutStateInfo = *StateInfo;
		return true;
	}
	return false;
}

#if WITH_EDITOR
void UVCamStateSwitcherWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!States.Contains(DefaultState))
	{
		States.Add(DefaultState, {});
	}
	
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif 

void UVCamStateSwitcherWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	constexpr bool bForceUpdate = true;
	const bool bIsCurrentStateValid = SetCurrentState(CurrentState, bForceUpdate); 
	if (!bIsCurrentStateValid)
	{
		SetCurrentState(DefaultState, bForceUpdate);
	}
}
