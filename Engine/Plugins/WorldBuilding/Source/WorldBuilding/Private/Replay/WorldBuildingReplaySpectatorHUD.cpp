// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldBuildingReplaySpectatorHUD.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Components/ComboBoxString.h"

UWorldBuildingViewPointWidget::UWorldBuildingViewPointWidget(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{

}

void UWorldBuildingViewPointWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void UWorldBuildingViewPointWidget::NextViewPoint()
{
	if(ComboViewPoints && ComboViewPoints->GetOptionCount())
	{
		SetSelectedViewPointIndex((ComboViewPoints->GetSelectedIndex() + 1) % ComboViewPoints->GetOptionCount());
	}
}

void UWorldBuildingViewPointWidget::PreviousViewPoint()
{
	if (ComboViewPoints)
	{
		const int32 Previous = ComboViewPoints->GetSelectedIndex() - 1;
		SetSelectedViewPointIndex(Previous < 0 ? ComboViewPoints->GetOptionCount() - 1 : Previous);
	}
}

void UWorldBuildingViewPointWidget::SetSelectedViewPointIndex(int32 Index)
{
	check(ComboViewPoints);
	if (Index >= 0 && Index < ComboViewPoints->GetOptionCount())
	{
		ComboViewPoints->SetSelectedIndex(Index);
	}
}

FString UWorldBuildingViewPointWidget::GetSelectedViewPoint() const
{
	if (ComboViewPoints)
	{
		return ComboViewPoints->GetSelectedOption();
	}

	return FString();
}

void UWorldBuildingViewPointWidget::SetAvailableViewPoints(const TArray<FString>& ViewPoints)
{
	if (ComboViewPoints)
	{
		if (LastViewPoints != ViewPoints)
		{
			FString CurrentSelection = ComboViewPoints->GetSelectedOption();
			ComboViewPoints->ClearSelection();
			ComboViewPoints->ClearOptions();
			for (const FString& Option : ViewPoints)
			{
				ComboViewPoints->AddOption(Option);
			}

			int32 OptionIndex = ComboViewPoints->FindOptionIndex(CurrentSelection);
			SetSelectedViewPointIndex(OptionIndex != INDEX_NONE ? OptionIndex : 0);

			LastViewPoints = ViewPoints;
		}
	}
}

AWorldBuildingReplaySpectatorHUD::AWorldBuildingReplaySpectatorHUD(const FObjectInitializer& ObjectInitializer) 
	: Super(ObjectInitializer)
{
}

void AWorldBuildingReplaySpectatorHUD::BeginPlay()
{
	Super::BeginPlay();

	if (ViewPointWidgetClass)
	{
		ViewPointWidget = CreateWidget<UWorldBuildingViewPointWidget>(GetWorld(), ViewPointWidgetClass);

		if (ViewPointWidget)
		{
			ViewPointWidget->AddToViewport();
		}
	}
}

FString AWorldBuildingReplaySpectatorHUD::GetSelectedViewPoint() const
{
	if (ViewPointWidget)
	{
		return ViewPointWidget->GetSelectedViewPoint();
	}

	return FString();
}

void AWorldBuildingReplaySpectatorHUD::SetAvailableViewPoints(const TArray<FString>& ViewPoints)
{
	if (ViewPointWidget)
	{
		ViewPointWidget->SetAvailableViewPoints(ViewPoints);
	}
}

void AWorldBuildingReplaySpectatorHUD::NextViewPoint()
{
	if (ViewPointWidget)
	{
		ViewPointWidget->NextViewPoint();
	}
}

void AWorldBuildingReplaySpectatorHUD::PreviousViewPoint()
{
	if (ViewPointWidget)
	{
		ViewPointWidget->PreviousViewPoint();
	}
}
