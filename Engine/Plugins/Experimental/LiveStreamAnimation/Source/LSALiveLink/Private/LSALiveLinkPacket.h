// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LSALiveLinkSourceOptions.h"
#include "LSALiveLinkFrameData.h"
#include "LiveStreamAnimationHandle.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Templates/UniquePtr.h"

/** The types of Packets we'll process for Live Link. */
enum class ELSALiveLinkPacketType : uint8
{
	AddOrUpdateSubject,	//! Used to add a new Live Link Subject, or to update the skeleton
						//! data of an already existing Live Link Subject.

	RemoveSubject,		//! Used to remove a Live Link Subject.

	AnimationFrame		//! Used to send a new animation update for a given subject.
						//! Typically sent unreliably.
};

/**
	* Generic packet that is used as a base for all Live Link packets.
	* @see ELiveLinkPacketType for the types of packets.
	*/
class FLSALiveLinkPacket
{
public:

	virtual ~FLSALiveLinkPacket() = 0;

	ELSALiveLinkPacketType GetPacketType() const
	{
		return PacketType;
	}

	FLiveStreamAnimationHandle GetSubjectHandle() const
	{
		return SubjectHandle;
	}

	bool IsReliable() const
	{
		return bReliable;
	}

	/**
		* Writes a LiveLinkPacket to the given archive.
		* 
		* @param InWriter	The archive to write into.
		* @param InPacket	The packet to write.
		*/
	static void WriteToStream(class FArchive& InWriter, const FLSALiveLinkPacket& InPacket);

	/**
		* Reads a LiveLinkPacket from the given archive.
		* The type read can be determined by using GetPacketType() on the resulting packet.
		* If we fail to read the packet, nullptr will be returned.
		* 
		* @param InReader	The archive to read from.
		* 
		* @return The read packet, or null if serialization failed.
		*/ 
	static TUniquePtr<FLSALiveLinkPacket> ReadFromStream(class FArchive& InReader);

protected:

	FLSALiveLinkPacket(
		const ELSALiveLinkPacketType InPacketType,
		const FLiveStreamAnimationHandle InSubjectHandle,
		const bool bInReliable)

		: PacketType(InPacketType)
		, SubjectHandle(InSubjectHandle)
		, bReliable(bInReliable)
	{
	}

private:

	const ELSALiveLinkPacketType PacketType;
	const FLiveStreamAnimationHandle SubjectHandle;
	const bool bReliable;
};

class FLSALiveLinkAddOrUpdateSubjectPacket : public FLSALiveLinkPacket
{
public:

	virtual ~FLSALiveLinkAddOrUpdateSubjectPacket() {}

	const FLSALiveLinkStaticData& GetStaticData() const
	{
		return StaticData;
	}

	/**
		* Creates a new AddOrUpdateSubject Packet.
		* May return null if the passed in parameters aren't valid.
		* 
		* @param InSubjectHandle	Handle that should be assigned to this subject.
		* @param InSubjectName		The Name for the Live Link Subject.
		* @param InStaticData		Live Link data needed to instantiate the subject.
		* 
		* 
		* @return The newly created packet.
		*/
	static TUniquePtr<FLSALiveLinkAddOrUpdateSubjectPacket> CreatePacket(
		const FLiveStreamAnimationHandle InSubjectHandle,
		FLSALiveLinkStaticData&& InStaticData);

private:

	friend FLSALiveLinkPacket;
	static void WriteToStream(struct FWriteToStreamParams&);
	static TUniquePtr<FLSALiveLinkPacket> ReadFromStream(struct FReadFromStreamParams&);

	FLSALiveLinkAddOrUpdateSubjectPacket(
		const FLiveStreamAnimationHandle InSubjectHandle,
		FLSALiveLinkStaticData&& InStaticData)

		: FLSALiveLinkPacket(ELSALiveLinkPacketType::AddOrUpdateSubject, InSubjectHandle, true)
		, StaticData(MoveTemp(InStaticData))
	{
	}

	const FLSALiveLinkStaticData StaticData;
};

class FLSALiveLinkRemoveSubjectPacket : public FLSALiveLinkPacket
{
public:

	virtual ~FLSALiveLinkRemoveSubjectPacket() {}

	/**
		* Creates a new RemoveSubject Packet.
		* May return null if the passed in parameters aren't valid.
		*
		* @param InSubjectHandle	Subject to remove.
		*
		* @return The newly created packet.
		*/
	static TUniquePtr<FLSALiveLinkRemoveSubjectPacket> CreatePacket(const FLiveStreamAnimationHandle InSubjectHandle);

private:

	friend FLSALiveLinkPacket;
	static void WriteToStream(struct FWriteToStreamParams&);
	static TUniquePtr<FLSALiveLinkPacket> ReadFromStream(struct FReadFromStreamParams&);

	FLSALiveLinkRemoveSubjectPacket(const FLiveStreamAnimationHandle InSubjectHandle)
		: FLSALiveLinkPacket(ELSALiveLinkPacketType::RemoveSubject, InSubjectHandle, true)
	{
	}
};

class FLSALiveLinkAnimationFramePacket : public FLSALiveLinkPacket
{
public:

	virtual ~FLSALiveLinkAnimationFramePacket() {}


	const FLSALiveLinkFrameData& GetFrameData() const
	{
		return FrameData;
	}

	/**
		* Creates a new AnimationFrame Packet.
		* May return null if the passed in parameters aren't valid.
		*
		* @param InSubjectHandle	Subject to update.
		* @param InOptions			The options to use when serializing the animation data.
		* @param InFrameData		The Animation data that we'll read from.
		*
		* @return The newly created packet.
		*/
	static TUniquePtr<FLSALiveLinkAnimationFramePacket> CreatePacket(
		const FLiveStreamAnimationHandle InSubjectHandle,
		FLSALiveLinkFrameData&& InFrameData);

private:

	friend FLSALiveLinkPacket;
	static void WriteToStream(struct FWriteToStreamParams&);
	static TUniquePtr<FLSALiveLinkPacket> ReadFromStream(struct FReadFromStreamParams&);

	FLSALiveLinkAnimationFramePacket(
		const FLiveStreamAnimationHandle InSubjectHandle,
		FLSALiveLinkFrameData&& InFrameData)

		: FLSALiveLinkPacket(ELSALiveLinkPacketType::AnimationFrame, InSubjectHandle, true)
		, FrameData(MoveTemp(InFrameData))
	{
	}

	const FLSALiveLinkFrameData FrameData;
};