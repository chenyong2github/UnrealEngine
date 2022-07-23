// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderPage/RenderPagePropsSource.h"
#include "RenderPageCollection.generated.h"


class UMoviePipelineOutputSetting;
class UMoviePipelineMasterConfig;
class ULevelSequence;
class URenderPageCollection;


/**
 * This struct contains the data for a certain remote control property.
 * 
 * It's currently simply a wrapper around a byte array.
 * This struct is needed so that that byte array can be used in another UPROPERTY container (TMap, TArray, etc).
 */
USTRUCT(BlueprintType)
struct RENDERPAGES_API FRenderPageRemoteControlPropertyData
{
	GENERATED_BODY()

public:
	FRenderPageRemoteControlPropertyData() = default;
	FRenderPageRemoteControlPropertyData(const TArray<uint8>& InBytes)
		: Bytes(InBytes)
	{}

public:
	/** The property data, as bytes. */
	UPROPERTY()
	TArray<uint8> Bytes;
};


/**
 * This class represents a render page.
 * It contains a level sequence and custom properties that will be applied while rendering.
 * 
 * Each render page must belong to a render page collection.
 */
UCLASS(BlueprintType)
class RENDERPAGES_API URenderPage : public UObject
{
	GENERATED_BODY()

public:
	URenderPage();

	/** Gets the calculated start frame, not taking the framerate of the render preset into account. */
	TOptional<int32> GetSequenceStartFrame() const;

	/** Gets the calculated end frame, not taking the framerate of the render preset into account. */
	TOptional<int32> GetSequenceEndFrame() const;

	/** Sets the custom start frame to match the given sequence start frame. */
	bool SetSequenceStartFrame(const int32 NewCustomStartFrame);

	/** Sets the custom end frame to match the given sequence end frame. */
	bool SetSequenceEndFrame(const int32 NewCustomEndFrame);

	/** Gets the calculated start frame. */
	TOptional<int32> GetStartFrame() const;

	/** Gets the calculated end frame. */
	TOptional<int32> GetEndFrame() const;

	/** Gets the calculated start time. */
	TOptional<double> GetStartTime() const;

	/** Gets the calculated end time. */
	TOptional<double> GetEndTime() const;

	/** Gets the calculated duration in seconds. */
	TOptional<double> GetDurationInSeconds() const;

	/** Gets the aspect ratio that this page will be rendered in. */
	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	double GetOutputAspectRatio() const;

	/** Checks whether the page contains data that matches the search terms. */
	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	bool MatchesSearchTerm(const FString& SearchTerm) const;

public:
	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	FGuid GetId() const { return Id; }

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void GenerateNewId() { Id = FGuid::NewGuid(); }


	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	int32 GetWaitFramesBeforeRendering() const { return WaitFramesBeforeRendering; }

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void SetWaitFramesBeforeRendering(const int32 NewWaitFramesBeforeRendering) { WaitFramesBeforeRendering = FMath::Max<int32>(0, NewWaitFramesBeforeRendering); }


	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	ULevelSequence* GetSequence() const { return Sequence; }

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void SetSequence(ULevelSequence* NewSequence) { Sequence = NewSequence; }


	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	bool GetIsCustomStartFrame() const { return bOverrideStartFrame; }

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void SetIsCustomStartFrame(const bool bNewOverrideStartFrame) { bOverrideStartFrame = bNewOverrideStartFrame; }

	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	int32 GetCustomStartFrame() const { return CustomStartFrame; }

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void SetCustomStartFrame(const int32 NewCustomStartFrame) { CustomStartFrame = NewCustomStartFrame; }


	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	bool GetIsCustomEndFrame() const { return bOverrideEndFrame; }

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void SetIsCustomEndFrame(const bool bNewOverrideEndFrame) { bOverrideEndFrame = bNewOverrideEndFrame; }

	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	int32 GetCustomEndFrame() const { return CustomEndFrame; }

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void SetCustomEndFrame(const int32 NewCustomEndFrame) { CustomEndFrame = NewCustomEndFrame; }


	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	bool GetIsCustomResolution() const { return bOverrideResolution; }

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void SetIsCustomResolution(const bool bNewOverrideResolution) { bOverrideResolution = bNewOverrideResolution; }

	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	FIntPoint GetCustomResolution() const { return CustomResolution; }

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void SetCustomResolution(const FIntPoint NewCustomResolution) { CustomResolution = NewCustomResolution; }


	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	FString GetPageId() const { return PageId; }

	static FString PurgePageIdOrReturnEmptyString(const FString& NewPageId);
	static FString PurgePageId(const FString& NewPageId);
	static FString PurgePageIdOrGenerateUniqueId(URenderPageCollection* PageCollection, const FString& NewPageId);

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void SetPageId(const FString& NewPageId) { PageId = PurgePageId(NewPageId); }
	void SetPageIdRaw(const FString& NewPageId) { PageId = NewPageId; }


	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	FString GetPageName() const { return PageName; }

	static FString PurgePageName(const FString& NewPageName);

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void SetPageName(const FString& NewPageName) { PageName = NewPageName; }
	void SetPageNameRaw(const FString& NewPageName) { PageName = NewPageName; }


	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	bool GetIsEnabled() const { return bIsEnabled; }

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void SetIsEnabled(const bool bEnabled) { bIsEnabled = bEnabled; }


	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	FString GetOutputDirectory() const;
	FString GetOutputDirectoryRaw() const { return OutputDirectory; }

	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	FString GetOutputDirectoryForDisplay() const { return OutputDirectory; }

	static FString PurgeOutputDirectory(const FString& NewOutputDirectory);

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void SetOutputDirectory(const FString& NewOutputDirectory) { OutputDirectory = PurgeOutputDirectory(NewOutputDirectory); }
	void SetOutputDirectoryRaw(const FString& NewOutputDirectory) { OutputDirectory = NewOutputDirectory; }


	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	UMoviePipelineMasterConfig* GetRenderPreset() const { return RenderPreset; }

	UFUNCTION(BlueprintPure, Category="Render Pages | Page")
	UMoviePipelineOutputSetting* GetRenderPresetOutputSettings() const;

	UFUNCTION(BlueprintCallable, Category="Render Pages | Page")
	void SetRenderPreset(UMoviePipelineMasterConfig* NewRenderPreset) { RenderPreset = NewRenderPreset; }


	bool HasRemoteControlValue(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity) const;
	bool ConstGetRemoteControlValue(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity, TArray<uint8>& OutBinaryArray) const;
	bool GetRemoteControlValue(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity, TArray<uint8>& OutBinaryArray);
	bool SetRemoteControlValue(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity, const TArray<uint8>& BinaryArray);
	TMap<FString, FRenderPageRemoteControlPropertyData>& GetRemoteControlValuesRef() { return RemoteControlValues; }

private:
	/** The unique ID of this render page. */
	UPROPERTY()
	FGuid Id;

	/** Waits the given number of frames before it will render this page. This can be set to a higher amount when the renderer has to wait for your code to complete (such as construction scripts etc). Try increasing this value when rendering doesn't produce the output you expect it to. */
	UPROPERTY(EditInstanceOnly, Category="Render Pages|Page", Meta=(AllowPrivateAccess="true", ClampMin="0"))
	int32 WaitFramesBeforeRendering;

	/** The level sequence, this is what will be rendered during rendering. A render page without a level sequence can't be rendered. */
	UPROPERTY(EditInstanceOnly, Category="Render Pages|Page", Meta=(AllowPrivateAccess="true"))
	TObjectPtr<ULevelSequence> Sequence;

	/** If this is true, the CustomStartFrame property will override the start frame of the level sequence. */
	UPROPERTY(EditInstanceOnly, Category="Render Pages|Page", Meta=(AllowPrivateAccess="true", InlineEditConditionToggle))
	bool bOverrideStartFrame;

	/** If bOverrideStartFrame is true, this property will override the start frame of the level sequence. */
	UPROPERTY(EditInstanceOnly, Category="Render Pages|Page", Meta=(AllowPrivateAccess="true", EditCondition="bOverrideStartFrame"))
	int32 CustomStartFrame;

	/** If this is true, the CustomEndFrame property will override the end frame of the level sequence. */
	UPROPERTY(EditInstanceOnly, Category="Render Pages|Page", Meta=(AllowPrivateAccess="true", InlineEditConditionToggle))
	bool bOverrideEndFrame;

	/** If bOverrideEndFrame is true, this property will override the end frame of the level sequence. */
	UPROPERTY(EditInstanceOnly, Category="Render Pages|Page", Meta=(AllowPrivateAccess="true", EditCondition="bOverrideEndFrame"))
	int32 CustomEndFrame;

	/** If this is true, the CustomResolution property will override the resolution of the render. */
	UPROPERTY()
	bool bOverrideResolution;

	/** If bOverrideResolution is true, this property will override the resolution of the render. */
	UPROPERTY()
	FIntPoint CustomResolution;

	/** If this is true, this render page will be rendered during a batch rendering, otherwise it will be skipped. */
	UPROPERTY()
	bool bIsEnabled;

	/** The 'ID' of this page, this 'ID' is set by users. During rendering it will place all the output files of this render page into a folder called after this 'ID', this means that this string can only contain file-safe characters. */
	UPROPERTY()
	FString PageId;

	/** The name of this page, this can be anything, it's set by the user, it serves as a way for the user to understand what page this is. */
	UPROPERTY()
	FString PageName;

	/** This is the folder in which the output files (of rendering) are placed into. To be more specific, the output files are placed in: {OutputDirectory}/{PageId}/, this folder will be created if it doesn't exist at the time of rendering. */
	UPROPERTY()
	FString OutputDirectory;

	/** The MRQ render preset. The pages are rendered using the MRQ (Movie Render Queue) plugin. This 'preset' contains the configuration of that plugin. */
	UPROPERTY()
	TObjectPtr<UMoviePipelineMasterConfig> RenderPreset;

	/** The Remote Control plugin can be used to customize and modify the way a page is rendered. If Remote Control is being used, the property values of this page will be stored in this map (remote control entity id -> value as bytes). */
	UPROPERTY()
	TMap<FString, FRenderPageRemoteControlPropertyData> RemoteControlValues;
};


/**
 * This class represents a collection of render pages.
 * A render page collection is the asset that is shown in the content browser, it's the asset that can be opened and edited using the editor.
 */
UCLASS(Blueprintable, BlueprintType, EditInlineNew, Meta=(DontUseGenericSpawnObject="true"))
class RENDERPAGES_API URenderPageCollection : public UObject
{
	GENERATED_BODY()

public:
	URenderPageCollection();

	//UObject interface
	virtual UWorld* GetWorld() const override;
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	virtual void PostLoad() override;
	//~ End UObject Interface

	/** Should be called when the editor closes this asset. */
	void OnClose() { SaveValuesToCDO(); }

public:
	static TArray<FString> GetBlueprintImplementableEvents() { return {TEXT("ReceivePreRender"), TEXT("ReceivePostRender")}; }

protected:
	/** Event for when rendering begins for a page. */
	UFUNCTION(BlueprintImplementableEvent, Meta=(DisplayName="PreRender"))
	void ReceivePreRender(URenderPage* Page);

	/** Event for when rendering ends for a page. */
	UFUNCTION(BlueprintImplementableEvent, Meta=(DisplayName="PostRender"))
	void ReceivePostRender(URenderPage* Page);

public:
	/** Overridable native event for when rendering begins for a page. */
	virtual void PreRender(URenderPage* Page);

	/** Overridable native event for when rendering ends for a page. */
	virtual void PostRender(URenderPage* Page);

private:
	/**
	 * Because render page collection assets are blueprints (assets that also have a blueprint graph), the render page collection data is not stored directly in the asset data that you see in the content browser.
	 * Instead, the data that is stored (and load) is the CDO (class default object).
	 * Because of that, any data that needs to persist needs to be copied over to the CDO during a save, and data you'd like to load from it needs to be copied from the CDO during a load.
	 */

	/** Obtains the CDO, could return itself if this is called on the CDO instance. */
	URenderPageCollection* GetCDO() { return (HasAnyFlags(RF_ClassDefaultObject) ? this : GetClass()->GetDefaultObject<URenderPageCollection>()); }

	/** Copied values over into the CDO. */
	void SaveValuesToCDO() { CopyValuesToOrFromCDO(true); }

	/** Copied values over from the CDO. */
	void LoadValuesFromCDO() { CopyValuesToOrFromCDO(false); }

	/** Copied values to or from the CDO, based on whether bToCDO is true or false. */
	void CopyValuesToOrFromCDO(const bool bToCDO);

public:
	FGuid GetId() const { return Id; }
	void GenerateNewId() { Id = FGuid::NewGuid(); }

	void SetPropsSource(ERenderPagePropsSourceType InPropsSourceType, UObject* InPropsSourceOrigin = nullptr);
	URenderPagePropsSourceBase* GetPropsSource() const;

	template<typename Type>
	Type* GetPropsSource() const
	{
		static_assert(TIsDerivedFrom<Type, URenderPagePropsSourceBase>::IsDerived, "Type needs to derive from URenderPagePropsSourceBase.");
		return Cast<Type>(GetPropsSource());
	}

	ERenderPagePropsSourceType GetPropsSourceType() const { return PropsSourceType; }
	UObject* GetPropsSourceOrigin() const;

public:
	void AddRenderPage(URenderPage* RenderPage);
	void RemoveRenderPage(URenderPage* RenderPage);
	void InsertRenderPage(URenderPage* RenderPage, int32 Index);
	bool HasRenderPage(URenderPage* RenderPage) const;
	int32 GetIndexOfRenderPage(URenderPage* RenderPage) const;
	TArray<TObjectPtr<URenderPage>>& GetRenderPagesRef() { return RenderPages; }
	TArray<URenderPage*> GetRenderPages() const;
	TArray<URenderPage*> GetEnabledRenderPages() const;
	TArray<URenderPage*> GetDisabledRenderPages() const;
	void InsertRenderPageBefore(URenderPage* RenderPage, URenderPage* BeforeRenderPage);
	void InsertRenderPageAfter(URenderPage* RenderPage, URenderPage* AfterRenderPage);

private:
	DECLARE_MULTICAST_DELEGATE(FOnRenderPageCollectionPreSave);
	FOnRenderPageCollectionPreSave& OnPreSaveCDO() { return GetCDO()->OnRenderPageCollectionPreSaveDelegate; }

private:
	/** The delegate for when this collection is about to save. */
	FOnRenderPageCollectionPreSave OnRenderPageCollectionPreSaveDelegate;

private:
	/** The unique ID of this render page collection. */
	UPROPERTY()
	FGuid Id;


	/** The type of the properties that a page can have. */
	UPROPERTY(EditInstanceOnly, Category="Render Pages|Page Collection", Meta=(DisplayName="Properties Type", AllowPrivateAccess="true"))
	ERenderPagePropsSourceType PropsSourceType;

	/** The remote control properties that a page can have, only use this if PropsSourceType is ERenderPagePropsSourceType::RemoteControl. */
	UPROPERTY(EditInstanceOnly, Category="Render Pages|Page Collection", Meta=(DisplayName="Remote Control Preset", AllowPrivateAccess="true", EditCondition="PropsSourceType == ERenderPagePropsSourceType::RemoteControl", EditConditionHides))
	TObjectPtr<URemoteControlPreset> PropsSourceOrigin_RemoteControl;


	/** The render pages of this collection. */
	UPROPERTY(Instanced)
	TArray<TObjectPtr<URenderPage>> RenderPages;


	/** GetPropsSource calls are somewhat expensive, we speed that up by caching the result (the PropsSource) that has been last outputted by that function. */
	UPROPERTY(Transient)
	mutable TObjectPtr<URenderPagePropsSourceBase> CachedPropsSource;

	/** GetPropsSource calls are somewhat expensive, we speed that up by caching the PropsSourceType last used in that function. */
	UPROPERTY(Transient)
	mutable ERenderPagePropsSourceType CachedPropsSourceType;

	/** GetPropsSource calls are somewhat expensive, we speed that up by caching the PropsSourceOrigin last used in that function. */
	UPROPERTY(Transient)
	mutable TWeakObjectPtr<UObject> CachedPropsSourceOriginWeakPtr;


	/** GetWorld calls can be expensive, we speed them up by caching the last found world until it goes away. */
	mutable TWeakObjectPtr<UWorld> CachedWorldWeakPtr;
};
