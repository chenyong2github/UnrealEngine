// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "Templates/SharedPointer.h"

#include "ICommonUIModule.h"
#include "CommonUITypes.h"
#include "CommonUIRichTextData.h"
#include "CommonTextBlock.h"
#include "CommonUISettings.generated.h"

class UMaterial;

UCLASS(config = Game, defaultconfig)
class COMMONUI_API UCommonUISettings : public UObject
{
	GENERATED_BODY()

public:
	UCommonUISettings(const FObjectInitializer& Initializer);

	// Called to load CommonUISetting data, if bAutoLoadData if set to false then game code must call LoadData().
	void LoadData();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// Called by the module startup to auto load CommonUISetting data if bAutoLoadData is true.
	void AutoLoadData();

	UCommonUIRichTextData* GetRichTextData() const;
	const FSlateBrush& GetDefaultThrobberBrush() const;
	UObject* GetDefaultImageResourceObject() const;

private:

	/** Controls if the data referenced is automatically loaded.
	 *  If False then game code must call LoadData() on it's own.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Default")
	bool bAutoLoadData;

	/** The Default Image Resource, newly created CommonImage Widgets will use this style. */
	UPROPERTY(config, EditAnywhere, Category = "Image", meta = (AllowedClasses = "Texture2D,MaterialInterface"))
	TSoftObjectPtr<UObject> DefaultImageResourceObject;

	/** The Default Throbber Material, newly created CommonLoadGuard Widget will use this style. */
	UPROPERTY(config, EditAnywhere, Category = "Throbber")
	TSoftObjectPtr<UMaterialInterface> DefaultThrobberMaterial;

	/** The Default Data for rich text to show inline icon and others. */
	UPROPERTY(config, EditAnywhere, Category = "RichText")
	TSoftClassPtr<UCommonUIRichTextData> DefaultRichTextDataClass;

private:
	void LoadEditorData();

	bool bDefaultDataLoaded;

	UPROPERTY(Transient)
	UObject* DefaultImageResourceObjectInstance;

	UPROPERTY(Transient)
	UMaterialInterface* DefaultThrobberMaterialInstance;

	UPROPERTY(Transient)
	FSlateBrush DefaultThrobberBrush;

	UPROPERTY(Transient)
	UCommonUIRichTextData* RichTextDataInstance;
};