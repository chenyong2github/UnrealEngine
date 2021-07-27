// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/HUD.h"
#include "Blueprint/UserWidget.h"
#include "WorldBuildingReplaySpectatorHUD.generated.h"

UCLASS()
class UWorldBuildingViewPointWidget : public UUserWidget
{
	GENERATED_UCLASS_BODY()

public:
	virtual void NativeConstruct() override;

	FString GetSelectedViewPoint() const;
	void SetAvailableViewPoints(const TArray<FString>& ViewPoints);

	void NextViewPoint();
	void PreviousViewPoint();

private:
	void SetSelectedViewPointIndex(int32 Index);

	UPROPERTY(meta = (BindWidget))
	class UComboBoxString* ComboViewPoints;

	TArray<FString> LastViewPoints;
};

UCLASS()
class AWorldBuildingReplaySpectatorHUD : public AHUD
{
	GENERATED_UCLASS_BODY()

	virtual void BeginPlay() override;

	FString GetSelectedViewPoint() const;
	void SetAvailableViewPoints(const TArray<FString>& StreamingSources);

	void NextViewPoint();
	void PreviousViewPoint();

protected:
	UPROPERTY(EditAnywhere, Category = "Widget")
	TSubclassOf<UWorldBuildingViewPointWidget> ViewPointWidgetClass;

	UWorldBuildingViewPointWidget* ViewPointWidget;
};