// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Types/ISlateMetaData.h"

class FEditableTextMetaData : public ISlateMetaData
{
public:

	SLATE_METADATA_TYPE(FEditableTextMetaData, ISlateMetaData)

	static TSharedRef<ISlateMetaData> MakeShared() { return MakeShareable(new FEditableTextMetaData()); }

};
