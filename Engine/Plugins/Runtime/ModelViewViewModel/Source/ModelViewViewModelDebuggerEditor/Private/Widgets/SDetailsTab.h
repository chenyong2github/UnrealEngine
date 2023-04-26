// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;

namespace UE::MVVM
{

class SDetailsTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDetailsTab) { }
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);
	void SetObjects(const TArray<UObject*>& InObjects);

private:
	TSharedPtr<IDetailsView> DetailView;
};

} //namespace
