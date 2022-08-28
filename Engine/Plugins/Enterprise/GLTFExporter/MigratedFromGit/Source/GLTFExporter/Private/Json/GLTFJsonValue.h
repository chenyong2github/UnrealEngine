// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class IGLTFJsonWriter;

struct IGLTFJsonValue
{
	virtual ~IGLTFJsonValue() = default;

	virtual void WriteValue(IGLTFJsonWriter& Writer) const = 0;
};
