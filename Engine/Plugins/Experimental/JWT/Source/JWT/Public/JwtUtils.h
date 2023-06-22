// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IPlatformCrypto.h"


class FJwtUtils
{

public:

	/**
	 * Get the pointer to the current platform
	 * encryption context (e.g. OpenSSL or SwitchSSL).
	 *
	 * @return Pointer to the encryption context
	 */
	static TUniquePtr<FEncryptionContext> GetEncryptionContext();

};
