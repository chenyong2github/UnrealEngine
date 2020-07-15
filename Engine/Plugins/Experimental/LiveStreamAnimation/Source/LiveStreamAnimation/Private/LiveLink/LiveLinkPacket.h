// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LiveStreamAnimationFwd.h"
#include "LiveLink/LiveStreamAnimationLiveLinkSourceOptions.h"
#include "LiveLink/LiveStreamAnimationLiveLinkFrameData.h"
#include "LiveStreamAnimationHandle.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Templates/UniquePtr.h"
#include "LiveStreamAnimationPacket.h"


namespace LiveStreamAnimation
{
	struct FWriteToStreamParams;
	struct FReadFromStreamParams;

	/** The types of Packets we'll process for Live Link. */
	enum class ELiveLinkPacketType : uint8
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
	class FLiveLinkPacket
	{
	public:

		static constexpr ELiveStreamAnimationPacketType GetAnimationPacketType()
		{
			return ELiveStreamAnimationPacketType::LiveLink;
		}

		virtual ~FLiveLinkPacket() = 0;

		ELiveLinkPacketType GetPacketType() const
		{
			return PacketType;
		}

		FLiveStreamAnimationHandle GetSubjectHandle() const
		{
			return SubjectHandle;
		}

		/**
		 * Writes a LiveLinkPacket to the given archive.
		 * 
		 * @param InWriter	The archive to write into.
		 * @param InPacket	The packet to write.
		 */
		static void WriteToStream(class FArchive& InWriter, const FLiveLinkPacket& InPacket);

		/**
		 * Reads a LiveLinkPacket from the given archive.
		 * The type read can be determined by using GetPacketType() on the resulting packet.
		 * If we fail to read the packet, nullptr will be returned.
		 * 
		 * @param InReader	The archive to read from.
		 * 
		 * @return The read packet, or null if serialization failed.
		 */ 
		static TUniquePtr<FLiveLinkPacket> ReadFromStream(class FArchive& InReader);

	protected:

		FLiveLinkPacket(const ELiveLinkPacketType InPacketType, const FLiveStreamAnimationHandle InSubjectHandle)
			: PacketType(InPacketType)
			, SubjectHandle(InSubjectHandle)
		{
		}

	private:

		const ELiveLinkPacketType PacketType;
		const FLiveStreamAnimationHandle SubjectHandle;
	};

	class FLiveLinkAddOrUpdateSubjectPacket : public FLiveLinkPacket
	{
	public:

		virtual ~FLiveLinkAddOrUpdateSubjectPacket() {}

		const FLiveStreamAnimationLiveLinkStaticData& GetStaticData() const
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
		static TUniquePtr<FLiveLinkAddOrUpdateSubjectPacket> CreatePacket(
			const FLiveStreamAnimationHandle InSubjectHandle,
			FLiveStreamAnimationLiveLinkStaticData&& InStaticData);

	private:

		friend FLiveLinkPacket;
		static void WriteToStream(struct FWriteToStreamParams&);
		static TUniquePtr<FLiveLinkPacket> ReadFromStream(struct FReadFromStreamParams&);

		FLiveLinkAddOrUpdateSubjectPacket(
			const FLiveStreamAnimationHandle InSubjectHandle,
			FLiveStreamAnimationLiveLinkStaticData&& InStaticData)

			: FLiveLinkPacket(ELiveLinkPacketType::AddOrUpdateSubject, InSubjectHandle)
			, StaticData(MoveTemp(InStaticData))
		{
		}

		const FLiveStreamAnimationLiveLinkStaticData StaticData;
	};

	class FLiveLinkRemoveSubjectPacket : public FLiveLinkPacket
	{
	public:

		virtual ~FLiveLinkRemoveSubjectPacket() {}

		/**
		 * Creates a new RemoveSubject Packet.
		 * May return null if the passed in parameters aren't valid.
		 *
		 * @param InSubjectHandle	Subject to remove.
		 *
		 * @return The newly created packet.
		 */
		static TUniquePtr<FLiveLinkRemoveSubjectPacket> CreatePacket(const FLiveStreamAnimationHandle InSubjectHandle);

	private:

		friend FLiveLinkPacket;
		static void WriteToStream(struct FWriteToStreamParams&);
		static TUniquePtr<FLiveLinkPacket> ReadFromStream(struct FReadFromStreamParams&);

		FLiveLinkRemoveSubjectPacket(const FLiveStreamAnimationHandle InSubjectHandle)
			: FLiveLinkPacket(ELiveLinkPacketType::RemoveSubject, InSubjectHandle)
		{
		}
	};

	class FLiveLinkAnimationFramePacket : public FLiveLinkPacket
	{
	public:

		virtual ~FLiveLinkAnimationFramePacket() {}


		const FLiveStreamAnimationLiveLinkFrameData& GetFrameData() const
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
		static TUniquePtr<FLiveLinkAnimationFramePacket> CreatePacket(
			const FLiveStreamAnimationHandle InSubjectHandle,
			FLiveStreamAnimationLiveLinkFrameData&& InFrameData);

	private:

		friend FLiveLinkPacket;
		static void WriteToStream(struct FWriteToStreamParams&);
		static TUniquePtr<FLiveLinkPacket> ReadFromStream(struct FReadFromStreamParams&);

		FLiveLinkAnimationFramePacket(
			const FLiveStreamAnimationHandle InSubjectHandle,
			FLiveStreamAnimationLiveLinkFrameData&& InFrameData)

			: FLiveLinkPacket(ELiveLinkPacketType::AnimationFrame, InSubjectHandle)
			, FrameData(MoveTemp(InFrameData))
		{
		}

		const FLiveStreamAnimationLiveLinkFrameData FrameData;
	};
}