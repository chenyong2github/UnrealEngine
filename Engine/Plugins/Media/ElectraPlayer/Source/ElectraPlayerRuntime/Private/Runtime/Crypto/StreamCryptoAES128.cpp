// Copyright Epic Games, Inc. All Rights Reserved.

#include "StreamCryptoAES128.h"

#include "3rdParty/tiny-AES-c/aes.h"

namespace Electra
{

class FStreamDecrypterAES128 : public IStreamDecrypterAES128
{
public:
	FStreamDecrypterAES128();
	virtual ~FStreamDecrypterAES128();

	virtual EResult CBCInit(const TArray<uint8>& Key, const TArray<uint8>* OptionalIV=nullptr) override;
	virtual EResult CBCDecryptInPlace(int32& OutNumBytes, uint8* InOutData, int32 NumBytes16, bool bIsFinalBlock) override;

	virtual int32 CBCGetEncryptionDataSize(int32 PlaintextSize) override;
	virtual EResult CBCEncryptInPlace(int32& OutNumBytes, uint8* InOutData, int32 NumBytes, bool bIsFinalData) override;
private:
	TinyAES128::AES_ctx		Context;
	bool					bIsInitialized;
};

/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/

TSharedPtr<IStreamDecrypterAES128, ESPMode::ThreadSafe> IStreamDecrypterAES128::Create()
{
	return TSharedPtr<IStreamDecrypterAES128, ESPMode::ThreadSafe>(new FStreamDecrypterAES128);
}

/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/

const TCHAR* IStreamDecrypterAES128::GetResultText(EResult ResultCode)
{
	switch(ResultCode)
	{
		case EResult::Ok:				return TEXT("Ok");
		case EResult::NotInitialized:	return TEXT("Not initialized");
		case EResult::BadKeyLength:		return TEXT("Bad key length");
		case EResult::BadIVLength:		return TEXT("Bad IV length");
		case EResult::BadDataLength:	return TEXT("Bad data length");
		case EResult::InvalidArg:		return TEXT("Invalid argument");
		case EResult::BadHexChar:		return TEXT("Invalid hex char");
		default:						return TEXT("?");
	}
}


IStreamDecrypterAES128::EResult IStreamDecrypterAES128::ConvHexStringToBin(TArray<uint8>& OutBinData, const char* InHexString)
{
	if (InHexString)
	{
		int32 InLen = FCStringAnsi::Strlen(InHexString);
		// Length must be even since we are only converting full bytes.
		if ((InLen & 1) == 0)
		{
			OutBinData.Init(0, InLen / 2);
			uint8* RawData = OutBinData.GetData() + (InLen/2 - 1);
			int32 hi = 0;
			while(--InLen >= 0)
			{
				uint8 v = InHexString[InLen];
				if (v >= '0' && v <= '9')
				{
					v -= '0';
				}
				else if (v >= 'a' && v <= 'f')
				{
					v = v - 'a' + 10;
				}
				else if (v >= 'A' && v <= 'F')
				{
					v = v - 'A' + 10;
				}
				else
				{
					return IStreamDecrypterAES128::EResult::BadHexChar;
				}
				if (hi)
				{
					*RawData |= (v << 4);
					--RawData;
				}
				else
				{
					*RawData |= v;
				}
				hi ^= 1;
			}
			return IStreamDecrypterAES128::EResult::Ok;
		}

		return IStreamDecrypterAES128::EResult::BadDataLength;
	}
	return IStreamDecrypterAES128::EResult::InvalidArg;
}

void IStreamDecrypterAES128::MakePaddedIVFromUInt64(TArray<uint8>& OutBinData, uint64 lower64Bits)
{
	OutBinData.Init(0, 16);
	for(int32 i=7; i>=0; --i)
	{
		OutBinData[8 + i] = (uint8)(lower64Bits & 0xff);
		lower64Bits >>= 8;
	}
}

/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/

FStreamDecrypterAES128::FStreamDecrypterAES128()
{
	bIsInitialized = false;
}

FStreamDecrypterAES128::~FStreamDecrypterAES128()
{
}


IStreamDecrypterAES128::EResult FStreamDecrypterAES128::CBCInit(const TArray<uint8>& Key, const TArray<uint8>* OptionalIV)
{
	bIsInitialized = false;
	if (Key.Num() == 16)
	{
		if (OptionalIV && OptionalIV->Num() != 16)
		{
			return IStreamDecrypterAES128::EResult::BadIVLength;
		}
		if (OptionalIV)
		{
			TinyAES128::AES_init_ctx_iv(&Context, (const uint8_t*)Key.GetData(), (const uint8_t*)OptionalIV->GetData());
		}
		else
		{
			TinyAES128::AES_init_ctx(&Context, (const uint8_t*)Key.GetData());
		}
		bIsInitialized = true;
		return IStreamDecrypterAES128::EResult::Ok;
	}
	return IStreamDecrypterAES128::EResult::BadKeyLength;
}

IStreamDecrypterAES128::EResult FStreamDecrypterAES128::CBCDecryptInPlace(int32& OutNumBytes, uint8* InOutData, int32 NumBytes16, bool bIsFinalBlock)
{
	OutNumBytes = 0;
	if (bIsInitialized)
	{
		// Because encryption adds PCKS7 padding the number of encrypted bytes is always a multiple of 16.
		if ((NumBytes16 & 15) == 0)
		{
			if (NumBytes16)
			{
				TinyAES128::AES_CBC_decrypt_buffer(&Context, (uint8_t*)InOutData, (uint32_t)NumBytes16);
				OutNumBytes = NumBytes16;
				if (bIsFinalBlock)
				{
					// Get the last byte of the last block which contains the PCKS7 number of padding bytes added.
					// The actual number of decrypted bytes is less that amount.
					OutNumBytes -= (int32)InOutData[NumBytes16 - 1];
				}
			}
			return IStreamDecrypterAES128::EResult::Ok;
		}
		return IStreamDecrypterAES128::EResult::BadDataLength;
	}
	return IStreamDecrypterAES128::EResult::NotInitialized;
}

int32 FStreamDecrypterAES128::CBCGetEncryptionDataSize(int32 PlaintextSize)
{
	// PKCS7 padding adds as many bytes as are required to get to the next 16 byte multiple.
	// If already a 16 byte multiple a whole 16 byte pad block is added, so encrypting 0 bytes results in an output of 16 bytes!
	int32 NumRemaining = PlaintextSize & 15;
	return NumRemaining ? ((PlaintextSize + 15) & ~15) : PlaintextSize + 16;
}

IStreamDecrypterAES128::EResult FStreamDecrypterAES128::CBCEncryptInPlace(int32& OutNumBytes, uint8* InOutData, int32 NumBytes, bool bIsFinalData)
{
	OutNumBytes = 0;
	if (bIsInitialized)
	{
		int32 NumRemaining = NumBytes & 15;
		int32 NumBlockBytes = NumBytes & ~15;
		// We cannot encrypt a non-16 byte multiple on any but the final block.
		if (NumRemaining && !bIsFinalData)
		{
			return IStreamDecrypterAES128::EResult::BadDataLength;
		}
		if (NumBlockBytes)
		{
			TinyAES128::AES_CBC_encrypt_buffer(&Context, (uint8_t*)InOutData, (uint32_t)NumBlockBytes);
			OutNumBytes = NumBlockBytes;
		}
		// The input to be encrypted must be overallocated to accommodate the extra bytes due to the final PCKS7 padding block.
		// The size of the final encrypted data can be obtained through CBCGetExtraEncryptionDataSize().
		if (bIsFinalData)
		{
			// Append the padding value
			FMemory::Memset(InOutData + NumBytes, (uint8)(16 - NumRemaining), 16 - NumRemaining);
			TinyAES128::AES_CBC_encrypt_buffer(&Context, (uint8_t*)(InOutData + NumBlockBytes), 16);
			OutNumBytes += 16;
		}
		return IStreamDecrypterAES128::EResult::Ok;
	}
	return IStreamDecrypterAES128::EResult::NotInitialized;
}


} // namespace Electra


