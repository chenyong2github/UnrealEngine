// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class IGLTFJsonWriter;

struct GLTFEXPORTER_API IGLTFJsonValue
{
	virtual ~IGLTFJsonValue() = default;

	virtual void WriteValue(IGLTFJsonWriter& Writer) const = 0;
};
