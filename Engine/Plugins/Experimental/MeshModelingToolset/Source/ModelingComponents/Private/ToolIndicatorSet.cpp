// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "ToolIndicatorSet.h"

UToolIndicatorSet::UToolIndicatorSet()
{
	Owner = nullptr;
}


UToolIndicatorSet::~UToolIndicatorSet()
{
	// if either of these is false, we did not disconnect properly
	check(Indicators.Num() == 0);
	check(Owner == nullptr);
}


void UToolIndicatorSet::Connect(UInteractiveTool* Tool)
{
	check(Tool != nullptr);
	this->Owner = Tool;
}


void UToolIndicatorSet::Disconnect()
{
	for (UObject* object : Indicators)
	{
		IToolIndicator* Indicator = Cast<IToolIndicator>(object);
		if (Indicator != nullptr)
		{
			Indicator->Disconnect();
		}
	}
	Indicators.Reset();

	this->Owner = nullptr;
}



void UToolIndicatorSet::AddIndicator(IToolIndicator* Indicator)
{
	check(Owner != nullptr);
	check(Indicator != nullptr);

	Indicator->Connect(Owner);

	Indicators.Add( Cast<UObject>(Indicator) );
}



void UToolIndicatorSet::Render(IToolsContextRenderAPI* RenderAPI)
{
	for (UObject* object : Indicators)
	{
		IToolIndicator* Indicator = Cast<IToolIndicator>(object);
		if (Indicator != nullptr)
		{
			Indicator->Render(RenderAPI);
		}
	}
}

void UToolIndicatorSet::Tick(float DeltaTime)
{
	for (UObject* object : Indicators)
	{
		IToolIndicator* Indicator = Cast<IToolIndicator>(object);
		if (Indicator != nullptr)
		{
			Indicator->Tick(DeltaTime);
		}
	}
}
