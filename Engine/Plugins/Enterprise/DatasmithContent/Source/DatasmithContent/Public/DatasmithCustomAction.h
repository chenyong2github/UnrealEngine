// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/StrongObjectPtr.h"
#include "AssetData.h"
#include "DatasmithCustomAction.generated.h"




class IDatasmithCustomAction
{
public:
	virtual ~IDatasmithCustomAction() = default;

	virtual const FText& GetLabel() { return FText::GetEmpty(); };
	virtual const FText& GetTooltip() { return FText::GetEmpty(); };

	virtual bool CanApplyOnAssets(const TArray<FAssetData>& SelectedAssets) { return false; }
	virtual void ApplyOnAssets(const TArray<FAssetData>& SelectedAssets) {}
};


/** #ueent_doc */
UCLASS(Abstract)
class DATASMITHCONTENT_API UDatasmithCustomActionBase : public UObject, public IDatasmithCustomAction
{
	GENERATED_BODY()

public:

#if WITH_EDITORONLY_DATA
#endif
};



class DATASMITHCONTENT_API FDatasmithCustomActionManager
{
public:
	FDatasmithCustomActionManager();

	TArray<UDatasmithCustomActionBase*> GetApplicableActions(const TArray<FAssetData>& SelectedAssets);

private:
	TArray<TStrongObjectPtr<UDatasmithCustomActionBase>> RegisteredActions;
};


