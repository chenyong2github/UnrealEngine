// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AssetRegistry/AssetData.h"
#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Sound/SoundEffectPreset.h"
#include "UObject/Class.h"
#include "UObject/Interface.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "AudioEditorSubsystem.generated.h"


// Forward Declarations
class FText;
class UUserWidget;


UINTERFACE(Blueprintable)
class AUDIOEDITOR_API UAudioWidgetInterface : public UInterface
{
	GENERATED_BODY()
};

class AUDIOEDITOR_API IAudioWidgetInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintImplementableEvent)
	FText GetEditorName();

	UFUNCTION(BlueprintImplementableEvent)
	FName GetIconBrushName();

	// Returns the class of object the compiled widget supports
	UFUNCTION(BlueprintImplementableEvent)
	UClass* GetClass();

	UFUNCTION(BlueprintImplementableEvent)
	void OnConstructed(UObject* Object);

	UFUNCTION(BlueprintImplementableEvent)
	void OnPropertyChanged(UObject* Object, FName PropertyName);

};

UCLASS()
class AUDIOEDITOR_API UAudioEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

private:
	static const TArray<FAssetData> GetWidgetBlueprintAssetData();
	static bool ImplementsInterface(const FAssetData& InAssetData, UClass* InInterfaceClass);

public:
	// Returns user widgets that implement an AudioWidgetInterface.  Optionally, constructs only widgets that implement
	// the provided AudioWidgetInterface type and/or widgets that support the given Object's parent class.
	TArray<UUserWidget*> CreateUserWidgets(TSubclassOf<UAudioWidgetInterface> InWidgetClass, UClass* InObjectClass = nullptr) const;
};
