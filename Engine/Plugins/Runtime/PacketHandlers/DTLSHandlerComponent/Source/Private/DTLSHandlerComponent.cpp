// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DTLSHandlerComponent.h"
#include "Engine/NetConnection.h"
#include "Ssl.h"

#define UI UI_ST
THIRD_PARTY_INCLUDES_START
#include <openssl/ssl.h>
#include <openssl/dtls1.h>
THIRD_PARTY_INCLUDES_END
#undef UI

DEFINE_LOG_CATEGORY(LogDTLSHandler);

IMPLEMENT_MODULE(FDTLSHandlerComponentModule, DTLSHandlerComponent)

TAutoConsoleVariable<int32> CVarPreSharedKeys(TEXT("DTLS.PreSharedKeys"), 1, TEXT("If non-zero, use pre-shared keys, otherwise self-signed certificates will be generated."));

void FDTLSHandlerComponentModule::StartupModule()
{
	FPacketHandlerComponentModuleInterface::StartupModule();

	FSslModule& SslModule = FModuleManager::LoadModuleChecked<FSslModule>("SSL");
	if (!SslModule.GetSslManager().InitializeSsl())
	{
		SSL_library_init();
		SSL_load_error_strings();        
		OpenSSL_add_all_algorithms();
	}
}

void FDTLSHandlerComponentModule::ShutdownModule()
{
	FSslModule& SslModule = FModuleManager::LoadModuleChecked<FSslModule>("SSL");
	SslModule.GetSslManager().ShutdownSsl();

	FPacketHandlerComponentModuleInterface::ShutdownModule();
}

TSharedPtr<HandlerComponent> FDTLSHandlerComponentModule::CreateComponentInstance(FString& Options)
{
	TSharedRef<HandlerComponent> ReturnVal = MakeShared<FDTLSHandlerComponent>();

	return ReturnVal;
}

FDTLSHandlerComponent::FDTLSHandlerComponent()
	: FEncryptionComponent(FName(TEXT("DTLSHandlerComponent")))
	, InternalState(EDTLSHandlerState::Unencrypted)
{
}

void FDTLSHandlerComponent::SetEncryptionData(const FEncryptionData& EncryptionData)
{
	const bool bPreSharedKeys = (CVarPreSharedKeys.GetValueOnAnyThread() != 0);

	if (bPreSharedKeys)
	{
		PreSharedKey.Reset(new FDTLSPreSharedKey());
		PreSharedKey->SetPreSharedKey(EncryptionData.Key);
	}
	else
	{
		if (Handler->Mode == Handler::Mode::Server)
		{
			CertId = EncryptionData.Identifier;
		}
		else
		{
			RemoteFingerprint.Reset(new FDTLSFingerprint());

			if (EncryptionData.Fingerprint.Num() == FDTLSFingerprint::Length)
			{
				FMemory::Memcpy(RemoteFingerprint->Data, EncryptionData.Fingerprint.GetData(), EncryptionData.Fingerprint.Num());
			}
			else
			{
				UE_LOG(LogDTLSHandler, Error, TEXT("SetEncryptionData: Invalid fingerprint size: got %d expected %d"), EncryptionData.Fingerprint.Num(), FDTLSFingerprint::Length);
			}
		}
	}
}

void FDTLSHandlerComponent::EnableEncryption()
{
	UE_LOG(LogDTLSHandler, Verbose, TEXT("EnableEncryption"));

	EDTLSContextType ContextType = (Handler->Mode == Handler::Mode::Server) ? EDTLSContextType::Server : EDTLSContextType::Client;

	DTLSContext.Reset(new FDTLSContext(ContextType));
	if (DTLSContext.IsValid())
	{
		check(Handler);

		const int32 MaxPacketSize = (MaxOutgoingBits - GetReservedPacketBits()) / 8;

		if (DTLSContext->Initialize(MaxPacketSize, CertId, this))
		{
			InternalState = EDTLSHandlerState::Handshaking;
		}
		else
		{
			UE_LOG(LogDTLSHandler, Error, TEXT("EnableEncryption: Failed to initialize context."));
			DTLSContext.Release();
		}
	}
	else
	{
		UE_LOG(LogDTLSHandler, Error, TEXT("EnableEncryption: Invalid context."));
	}
}

void FDTLSHandlerComponent::DisableEncryption()
{
	UE_LOG(LogDTLSHandler, Verbose, TEXT("DisableEncryption"));

	DTLSContext.Release();
}

bool FDTLSHandlerComponent::IsEncryptionEnabled() const
{
	return DTLSContext.IsValid();
}

void FDTLSHandlerComponent::Initialize()
{
	SetActive(true);
	SetState(Handler::Component::State::Initialized);
	Initialized();
}

bool FDTLSHandlerComponent::IsValid() const
{
	return true;
}

void FDTLSHandlerComponent::Incoming(FBitReader& Packet)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("PacketHandler DTLS Decrypt"), STAT_PacketHandler_DTLS_Decrypt, STATGROUP_Net);

	// check encrypted bit
	if (Packet.ReadBit() != 0)
	{
		if (DTLSContext.IsValid())
		{
			const int32 HandshakeBit = Packet.ReadBit();
			const int32 PayloadBytes = Packet.GetBytesLeft();

			check(PayloadBytes > 0);
			check(PayloadBytes <= sizeof(TempBuffer));

			TempBuffer[PayloadBytes - 1] = 0;
			Packet.SerializeBits(TempBuffer, Packet.GetBitsLeft());

			if (InternalState == EDTLSHandlerState::Handshaking)
			{
				if (HandshakeBit == 1)
				{
					// feed the incoming data to the context
					const int32 BytesWritten = BIO_write(DTLSContext->GetInBIO(), TempBuffer, PayloadBytes);
					if (BytesWritten != PayloadBytes)
					{
						UE_LOG(LogDTLSHandler, Warning, TEXT("Failed to write entire incoming packet to input BIO: %d / %d"), BytesWritten, PayloadBytes);
						Packet.SetError();
						return;
					}
				}
				else
				{
					UE_LOG(LogDTLSHandler, Warning, TEXT("Ignoring non-handshake packet while handshake is still in progress."));
					Packet.SetData(nullptr, 0);
				}
			}
			else if (InternalState == EDTLSHandlerState::Encrypted)
			{
				if (HandshakeBit == 0)
				{
					const int32 BytesWritten = BIO_write(DTLSContext->GetInBIO(), TempBuffer, PayloadBytes);
					if (BytesWritten == PayloadBytes)
					{
						const int32 BytesRead = SSL_read(DTLSContext->GetSSLPtr(), TempBuffer, sizeof(TempBuffer));
						if (BytesRead > 0)
						{
							// Look for the termination bit that was written in Outgoing() to determine the exact bit size.
							uint8 LastByte = TempBuffer[BytesRead - 1];
							if (LastByte != 0)
							{
								int32 BitSize = (BytesRead * 8) - 1;

								// Bit streaming, starts at the Least Significant Bit, and ends at the MSB.
								while (!(LastByte & 0x80))
								{
									LastByte *= 2;
									BitSize--;
								}

								Packet.SetData(TempBuffer, BitSize);
							}
							else
							{
								UE_LOG(LogTemp, Error, TEXT("DTLS Error"));
								Packet.SetError();
							}
						}
						else
						{
							const int32 ErrorCode = SSL_get_error(DTLSContext->GetSSLPtr(), BytesRead);
							UE_LOG(LogDTLSHandler, Error, TEXT("SSL_read error: %d"), ErrorCode);
							Packet.SetError();
						}
					}
					else
					{
						UE_LOG(LogDTLSHandler, Error, TEXT("Failed to write entire incoming packet to input BIO: %d / %d"), BytesWritten, PayloadBytes);
						Packet.SetError();
					}
				}
				else
				{
					UE_LOG(LogDTLSHandler, Warning, TEXT("Ignoring handshake packet received after completion."));
					Packet.SetData(nullptr, 0);
				}
			}
			else
			{
				UE_LOG(LogDTLSHandler, Error, TEXT("Attempted to process packet with handler in invalid state"));
				Packet.SetError();
			}
		}
		else
		{
			UE_LOG(LogDTLSHandler, Error, TEXT("Invalid DTLS context."));
			Packet.SetError();
		}
	}
}

void FDTLSHandlerComponent::UpdateHandshake()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("PacketHandler DTLS Handshake"), STAT_PacketHandler_DTLS_Handshake, STATGROUP_Net);

	if ((InternalState == EDTLSHandlerState::Handshaking) && DTLSContext.IsValid())
	{
		SSL* SSLPtr = DTLSContext->GetSSLPtr();

		if (!DTLSContext->IsHandshakeComplete())
		{
			const int32 HandshakeResult = SSL_do_handshake(SSLPtr);
			if (HandshakeResult != 1)
			{
				int SSLError = SSL_get_error(SSLPtr, HandshakeResult);

				if (SSLError != SSL_ERROR_NONE && SSLError != SSL_ERROR_WANT_READ && SSLError != SSL_ERROR_WANT_WRITE)
				{
					UE_LOG(LogDTLSHandler, Error, TEXT("UpdateHandshake:  Handshaking failed with error: %d"), SSLError);
					DTLSContext.Reset();
					return;
				}
			}

			int32 Pending = BIO_ctrl_pending(DTLSContext->GetFilterBIO());
			while (Pending > 0)
			{
				check(Pending <= sizeof(TempBuffer));

				const int32 BytesRead = BIO_read(DTLSContext->GetOutBIO(), TempBuffer, Pending);
				if (BytesRead > 0)
				{
					check(BytesRead == Pending);
					check(BytesRead <= MAX_PACKET_SIZE);

					FBitWriter OutPacket(0, true);
					OutPacket.WriteBit(1);	// encryption enabled
					OutPacket.WriteBit(1);	// handshake packet
					OutPacket.SerializeBits(TempBuffer, BytesRead * 8);

					// SendHandlerPacket is a low level send and is not reliable
					FOutPacketTraits Traits;
					Handler->SendHandlerPacket(this, OutPacket, Traits);
				}
				else
				{
					UE_LOG(LogDTLSHandler, Error, TEXT("BIO_read error: %d"), BytesRead);
					return;
				}

				Pending = BIO_ctrl_pending(DTLSContext->GetFilterBIO());
			}
		}
		else
		{
			InternalState = EDTLSHandlerState::Encrypted;
			UE_LOG(LogDTLSHandler, Log, TEXT("UpdateHandshake:  Handshaking completed"));
		}
	}
}

void FDTLSHandlerComponent::Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("PacketHandler DTLS Encrypt"), STAT_PacketHandler_DTLS_Encrypt, STATGROUP_Net);

	const bool bEncryptionEnabled = IsEncryptionEnabled();

	FBitWriter NewPacket(Packet.GetNumBits() + 2, true);
	NewPacket.WriteBit(bEncryptionEnabled ? 1 : 0);

	if (bEncryptionEnabled)
	{
		if (DTLSContext.IsValid())
		{
			if (InternalState == EDTLSHandlerState::Encrypted)
			{
				NewPacket.WriteBit(0); // handshake bit

				// Termination bit
				Packet.WriteBit(1);

				const int32 BytesWritten = SSL_write(DTLSContext->GetSSLPtr(), Packet.GetData(), Packet.GetNumBytes());
				if (BytesWritten != Packet.GetNumBytes())
				{
					UE_LOG(LogDTLSHandler, Warning, TEXT("Failed to write entire outgoing packet to SSL: %d / %d"), BytesWritten, Packet.GetNumBytes());
					Packet.SetError();
					return;
				}

				int32 Pending = BIO_ctrl_pending(DTLSContext->GetFilterBIO());
				if (Pending > 0)
				{
					check(Pending <= sizeof(TempBuffer));

					const int32 BytesRead = BIO_read(DTLSContext->GetOutBIO(), TempBuffer, Pending);
					if (ensure(BytesRead == Pending))
					{
						NewPacket.SerializeBits(TempBuffer, BytesRead * 8);
					}
					else
					{
						UE_LOG(LogDTLSHandler, Error, TEXT("BIO_read error: %d"), BytesRead);
						Packet.SetError();
					}
				}
				else
				{
					UE_LOG(LogDTLSHandler, Error, TEXT("BIO_ctrl_pending error: %d"), Pending);
					Packet.SetError();
				}
			}
			else if (InternalState == EDTLSHandlerState::Handshaking)
			{
				// not a warning as it is expected that this could happen during handshaking
				UE_LOG(LogDTLSHandler, Log, TEXT("Attempted to send packet during handshaking, dropping."));
				Packet.Reset();
				return;
			}
			else
			{
				UE_LOG(LogDTLSHandler, Error, TEXT("Attempted to send packet while handler was in invalid state"));
				Packet.SetError();
			}
		}
		else
		{
			UE_LOG(LogDTLSHandler, Error, TEXT("Invalid DTLS context."));
			Packet.SetError();
		}
	}
	else
	{
		NewPacket.SerializeBits(Packet.GetData(), Packet.GetNumBits());
	}

	if (!Packet.IsError())
	{
		Packet = MoveTemp(NewPacket);
	}
}

void FDTLSHandlerComponent::Tick(float DeltaTime)
{
	UpdateHandshake();
}

int32 FDTLSHandlerComponent::GetReservedPacketBits() const
{
	const int32 BlockSizeInBytes = 32;

	// Worst case includes the encryption bit, handshake bit, the termination bit, padding up to the next whole byte, a block of padding, and the header size
	return 3 + 7 + (BlockSizeInBytes * 8) + FMath::Max(DTLS1_RT_HEADER_LENGTH, DTLS1_HM_HEADER_LENGTH) * 8;
}

void FDTLSHandlerComponent::CountBytes(FArchive& Ar) const
{
	FEncryptionComponent::CountBytes(Ar);

	const SIZE_T SizeOfThis = sizeof(*this) - sizeof(FEncryptionComponent);
	Ar.CountBytes(SizeOfThis, SizeOfThis);
}