// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonValue.h"
#include "Json/GLTFJsonWriter.h"

struct IGLTFJsonArray : IGLTFJsonValue
{
	virtual void WriteValue(IGLTFJsonWriter& Writer) const override final
	{
		Writer.StartArray();
		WriteArray(Writer);
		Writer.EndArray();
	}

	virtual void WriteArray(IGLTFJsonWriter& Writer) const = 0;
};
