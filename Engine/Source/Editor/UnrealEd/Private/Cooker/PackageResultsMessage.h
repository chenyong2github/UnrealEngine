// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CompactBinaryTCP.h"
#include "Containers/Array.h"
#include "CookTypes.h"
#include "Templates/UniquePtr.h"
#include "UObject/NameTypes.h"

class ITargetPlatform;
namespace UE::Cook { struct FPackageData; }
namespace UE::Cook { struct FPackageResultsMessage; }

namespace UE::Cook
{

/**
 * Helper struct for FPackageResultsMessage.
 * Holds replication information about the result of a Package's save, including per-platform results and
 * system-specific messages from other systems
 */
struct FPackageRemoteResult
{
public:
	/** Information about the results for a single platform */
	struct FPlatformResult
	{
	public:
		const ITargetPlatform* GetPlatform() const { return Platform; }
		void SetPlatform(const ITargetPlatform* InPlatform) { Platform = InPlatform; }

		TConstArrayView<UE::CompactBinaryTCP::FMarshalledMessage> GetMessages() const { return Messages; }
		TArray<UE::CompactBinaryTCP::FMarshalledMessage> ReleaseMessages();

		const FGuid& GetPackageGuid() const { return PackageGuid; }
		void SetPackageGuid(const FGuid& InPackageGuid) { PackageGuid = InPackageGuid; }

		FCbObjectView GetTargetDomainDependencies() const { return TargetDomainDependencies; }
		void SetTargetDomainDependencies(FCbObject&& InObject) { TargetDomainDependencies = MoveTemp(InObject); }

		bool IsSuccessful() const { return bSuccessful; }
		void SetSuccessful(bool bInSuccessful) { bSuccessful = bInSuccessful; }

	private:
		FCbObject TargetDomainDependencies;
		FGuid PackageGuid;
		TArray<UE::CompactBinaryTCP::FMarshalledMessage> Messages;
		const ITargetPlatform* Platform = nullptr;
		bool bSuccessful = false;

		friend FPackageRemoteResult;
		friend FPackageResultsMessage;
	};

	FName GetPackageName() const { return PackageName; }
	void SetPackageName(FName InPackageName) { PackageName = InPackageName; }

	ESuppressCookReason GetSuppressCookReason() const { return SuppressCookReason; }
	void SetSuppressCookReason(ESuppressCookReason InSuppressCookReason) { SuppressCookReason = InSuppressCookReason; }

	bool IsReferencedOnlyByEditorOnlyData() const { return bReferencedOnlyByEditorOnlyData; }
	void SetReferencedOnlyByEditorOnlyData(bool bInReferencedOnlyByEditorOnlyData) { bReferencedOnlyByEditorOnlyData = bInReferencedOnlyByEditorOnlyData; }

	void AddPackageMessage(const FGuid& MessageType, FCbObject&& Object);
	void AddPlatformMessage(const ITargetPlatform* TargetPlatform, const FGuid& MessageType, FCbObject&& Object);
	TConstArrayView<UE::CompactBinaryTCP::FMarshalledMessage> GetMessages() const { return Messages; }
	TArray<UE::CompactBinaryTCP::FMarshalledMessage> ReleaseMessages();

	TArray<FPlatformResult, TInlineAllocator<1>>& GetPlatforms() { return Platforms; }
	void SetPlatforms(TConstArrayView<ITargetPlatform*> OrderedSessionPlatforms);

private:
	TArray<FPlatformResult, TInlineAllocator<1>> Platforms;
	TArray<UE::CompactBinaryTCP::FMarshalledMessage> Messages;
	FName PackageName;
	/** If failure reason is InvalidSuppressCookReason, it was saved. Otherwise, holds the suppression reason */
	ESuppressCookReason SuppressCookReason;
	bool bReferencedOnlyByEditorOnlyData = false;

	friend FPackageResultsMessage;
};

/** Message from Client to Server giving the results for saved or refused-to-cook packages. */
struct FPackageResultsMessage : public UE::CompactBinaryTCP::IMessage
{
public:
	virtual void Write(FCbWriter& Writer) const override;
	virtual bool TryRead(FCbObjectView Object) override;
	virtual FGuid GetMessageType() const override { return MessageType; }

public:
	TArray<FPackageRemoteResult> Results;

	static FGuid MessageType;

private:
	static void WriteMessagesArray(FCbWriter& Writer,
		TConstArrayView<UE::CompactBinaryTCP::FMarshalledMessage> InMessages);
	static bool TryReadMessagesArray(FCbObjectView ObjectWithMessageField,
		TArray<UE::CompactBinaryTCP::FMarshalledMessage>& InMessages);
};

}
