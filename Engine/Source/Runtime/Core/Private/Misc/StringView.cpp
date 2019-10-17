// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Misc/StringView.h"

FString 
FStringView::ToString() const
{
	return FString(Len(), Data());
}
