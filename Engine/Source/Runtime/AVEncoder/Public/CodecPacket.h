// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VideoCommon.h"
#include <HAL/CriticalSection.h>

namespace AVEncoder
{

class AVENCODER_API FCodecPacketImpl : public FCodecPacket
{
public:
	~FCodecPacketImpl();

	// clone packet if a longer term copy is needed
	const FCodecPacket* Clone() const override;
	// release a cloned copy
	void ReleaseClone() const override;

	class FClone : public FCodecPacket
	{
	public:
		~FClone();

		void Copy(const FCodecPacketImpl& InOriginal);

		// clone packet if a longer term copy is needed
		const FCodecPacket* Clone() const override;
		// release a cloned copy
		void ReleaseClone() const override;

	private:
		mutable FThreadSafeCounter		RefCounter = 0;
	};
private:
	mutable FCriticalSection			ProtectClone;
	mutable const FClone*				MyClone = nullptr;
};


} /* namespace AVEncoder */