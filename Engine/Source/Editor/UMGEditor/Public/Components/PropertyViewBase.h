// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Widgets/SWidget.h"
#include "UObject/LazyObjectPtr.h"
#include "UObject/SoftObjectPath.h"
#include "PropertyViewBase.generated.h"

class SBorder;

/** Sets a delegate called when the property value changes */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPropertyValueChanged, FName, PropertyName);


/**
 * Base of property view allows you to display the value of an object properties.
 */
UCLASS(Abstract)
class UMGEDITOR_API UPropertyViewBase : public UWidget
{
	GENERATED_BODY()

protected:
	/** The object to view. */
	UPROPERTY(meta = (DisplayName="Object"))
	TLazyObjectPtr<UObject> LazyObject;

	UPROPERTY()
	FSoftObjectPath SoftObjectPath;

	/** Load the object (if it's an asset) when the widget is created. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "View")
	bool bAutoLoadAsset = true;

	/** Sets a delegate called when the property value changes */
	UPROPERTY(BlueprintAssignable, Category = "View|Event")
	FOnPropertyValueChanged OnPropertyChanged;


public:
	UFUNCTION(BlueprintCallable, Category = "View")
	UObject* GetObject() const;

	UFUNCTION(BlueprintCallable, Category = "View")
	void SetObject(UObject* NewObject);

protected:
	virtual void BuildContentWidget() PURE_VIRTUAL(UPropertyViewBase::BuildContentWidget, );
	virtual void OnObjectChanged() { }

	void AsynBuildContentWidget();
	TSharedPtr<SBorder> GetDisplayWidget() const { return DisplayedWidget; }
	void OnPropertyChangedBroadcast(FName PropertyName);

private:
	void InternalOnAssetLoaded(UObject* LoadedAsset);
	void InternalOnMapChange(uint32);
	void InternalPostLoadMapWithWorld(UWorld* LoadedWorld);

	//~ UWidget interface
public:
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual const FText GetPaletteCategory() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	//~ End of UWidget interface

	//~ UObject interface
public:
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End of UObject interface

private:
	TSharedPtr<SBorder> DisplayedWidget;
	FDelegateHandle AssetLoadedHandle;
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle MapChangeHandle;
	bool bIsAsyncBuildContentRequested = false;
};
