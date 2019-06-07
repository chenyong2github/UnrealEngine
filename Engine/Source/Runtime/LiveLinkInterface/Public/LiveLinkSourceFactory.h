// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "LiveLinkSourceFactory.generated.h"

class ILiveLinkSource;
class ILiveLinkClient;
class SWidget;

UCLASS(Abstract)
class LIVELINKINTERFACE_API ULiveLinkSourceFactory : public UObject
{
	GENERATED_BODY()

public:
	virtual FText GetSourceDisplayName() const PURE_VIRTUAL( ULiveLinkSourceFactory::GetSourceDisplayName, return FText(); );
	virtual FText GetSourceTooltip() const PURE_VIRTUAL(ULiveLinkSourceFactory::GetSourceTooltip, return FText(); );

	enum class EMenuType
	{
		SubPanel,	// In the UI, a sub menu will used
		MenuEntry,	// In the UI, a button will be used
		Disabled,	// In the UI, the button will be used but it will be disabled
	};

	/**
	 * How the factory should be visible in the LiveLink UI.
	 * If SubPanel, BuildCreationPanel should be implemented.
	 */
	virtual EMenuType GetMenuType() const { return EMenuType::Disabled; }

	DECLARE_DELEGATE_TwoParams(FOnLiveLinkSourceCreated, TSharedPtr<ILiveLinkSource> /*Created source*/, FString /*ConnectionString*/);
	/** Build a UI that will create a Live Link source. */
	virtual TSharedPtr<SWidget> BuildCreationPanel(FOnLiveLinkSourceCreated OnLiveLinkSourceCreated) const;

	/** Create a new source from a ConnectionString */
	virtual TSharedPtr<ILiveLinkSource> CreateSource(const FString& ConnectionString) const PURE_VIRTUAL(ULiveLinkSourceFactory::CreateSource, return TSharedPtr<ILiveLinkSource>(); );
};
