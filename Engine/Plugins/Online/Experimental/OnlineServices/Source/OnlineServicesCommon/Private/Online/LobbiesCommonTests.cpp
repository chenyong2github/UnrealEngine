// Copyright Epic Games, Inc. All Rights Reserved.

#include "LobbiesCommonTests.h"

#include "Containers/Ticker.h"
#include "Misc/AutomationTest.h"
#include "Online/LobbiesCommon.h"
#include "Online/Auth.h"
#include "Online/OnlineAsyncOp.h"

namespace UE::Online {

class FLobbyEventCapture final
{
public:
	UE_NONCOPYABLE(FLobbyEventCapture);

	FLobbyEventCapture(FLobbyEvents& LobbyEvents)
		: LobbyJoinedHandle(LobbyEvents.OnLobbyJoined.Add(this, &FLobbyEventCapture::OnLobbyJoined))
		, LobbyLeftHandle(LobbyEvents.OnLobbyLeft.Add(this, &FLobbyEventCapture::OnLobbyLeft))
		, LobbyMemberJoinedHandle(LobbyEvents.OnLobbyMemberJoined.Add(this, &FLobbyEventCapture::OnLobbyMemberJoined))
		, LobbyMemberLeftHandle(LobbyEvents.OnLobbyMemberLeft.Add(this, &FLobbyEventCapture::OnLobbyMemberLeft))
		, LobbyLeaderChangedHandle(LobbyEvents.OnLobbyLeaderChanged.Add(this, &FLobbyEventCapture::OnLobbyLeaderChanged))
		, LobbySchemaChangedHandle(LobbyEvents.OnLobbySchemaChanged.Add(this, &FLobbyEventCapture::OnLobbySchemaChanged))
		, LobbyAttributesChangedHandle(LobbyEvents.OnLobbyAttributesChanged.Add(this, &FLobbyEventCapture::OnLobbyAttributesChanged))
		, LobbyMemberAttributesChangedHandle(LobbyEvents.OnLobbyMemberAttributesChanged.Add(this, &FLobbyEventCapture::OnLobbyMemberAttributesChanged))
		, LobbyInvitationAddedHandle(LobbyEvents.OnLobbyInvitationAdded.Add(this, &FLobbyEventCapture::OnLobbyInvitationAdded))
		, LobbyInvitationRemovedHandle(LobbyEvents.OnLobbyInvitationRemoved.Add(this, &FLobbyEventCapture::OnLobbyInvitationRemoved))
	{
	}

	void Empty()
	{
		LobbyJoined.Empty();
		LobbyLeft.Empty();
		LobbyMemberJoined.Empty();
		LobbyMemberLeft.Empty();
		LobbyLeaderChanged.Empty();
		LobbySchemaChanged.Empty();
		LobbyAttributesChanged.Empty();
		LobbyMemberAttributesChanged.Empty();
		LobbyInvitationAdded.Empty();
		LobbyInvitationRemoved.Empty();
		NextIndex = 0;
		TotalNotificationsReceived = 0;
	}

	uint32 GetTotalNotificationsReceived() const
	{
		return TotalNotificationsReceived;
	}

	template <typename NotificationType>
	struct TNotificationInfo
	{
		NotificationType Notification;
		int32 GlobalIndex = 0;
	};

	TArray<TNotificationInfo<FLobbyJoined>> LobbyJoined;
	TArray<TNotificationInfo<FLobbyLeft>> LobbyLeft;
	TArray<TNotificationInfo<FLobbyMemberJoined>> LobbyMemberJoined;
	TArray<TNotificationInfo<FLobbyMemberLeft>> LobbyMemberLeft;
	TArray<TNotificationInfo<FLobbyLeaderChanged>> LobbyLeaderChanged;
	TArray<TNotificationInfo<FLobbySchemaChanged>> LobbySchemaChanged;
	TArray<TNotificationInfo<FLobbyAttributesChanged>> LobbyAttributesChanged;
	TArray<TNotificationInfo<FLobbyMemberAttributesChanged>> LobbyMemberAttributesChanged;
	TArray<TNotificationInfo<FLobbyInvitationAdded>> LobbyInvitationAdded;
	TArray<TNotificationInfo<FLobbyInvitationRemoved>> LobbyInvitationRemoved;

private:
	template <typename ContainerType, typename NotificationType>
	void AddEvent(ContainerType& Container, const NotificationType& Notification)
	{
		Container.Add({Notification, NextIndex++});
		++TotalNotificationsReceived;
	}

	void OnLobbyJoined(const FLobbyJoined& Notification) { AddEvent(LobbyJoined, Notification); }
	void OnLobbyLeft(const FLobbyLeft& Notification) { AddEvent(LobbyLeft, Notification); }
	void OnLobbyMemberJoined(const FLobbyMemberJoined& Notification) { AddEvent(LobbyMemberJoined, Notification); }
	void OnLobbyMemberLeft(const FLobbyMemberLeft& Notification) { AddEvent(LobbyMemberLeft, Notification); }
	void OnLobbyLeaderChanged(const FLobbyLeaderChanged& Notification) { AddEvent(LobbyLeaderChanged, Notification); }
	void OnLobbySchemaChanged(const FLobbySchemaChanged& Notification) { AddEvent(LobbySchemaChanged, Notification); }
	void OnLobbyAttributesChanged(const FLobbyAttributesChanged& Notification) { AddEvent(LobbyAttributesChanged, Notification); }
	void OnLobbyMemberAttributesChanged(const FLobbyMemberAttributesChanged& Notification) { AddEvent(LobbyMemberAttributesChanged, Notification); }
	void OnLobbyInvitationAdded(const FLobbyInvitationAdded& Notification) { AddEvent(LobbyInvitationAdded, Notification); }
	void OnLobbyInvitationRemoved(const FLobbyInvitationRemoved& Notification) { AddEvent(LobbyInvitationRemoved, Notification); }

	FOnlineEventDelegateHandle LobbyJoinedHandle;
	FOnlineEventDelegateHandle LobbyLeftHandle;
	FOnlineEventDelegateHandle LobbyMemberJoinedHandle;
	FOnlineEventDelegateHandle LobbyMemberLeftHandle;
	FOnlineEventDelegateHandle LobbyLeaderChangedHandle;
	FOnlineEventDelegateHandle LobbySchemaChangedHandle;
	FOnlineEventDelegateHandle LobbyAttributesChangedHandle;
	FOnlineEventDelegateHandle LobbyMemberAttributesChangedHandle;
	FOnlineEventDelegateHandle LobbyInvitationAddedHandle;
	FOnlineEventDelegateHandle LobbyInvitationRemovedHandle;
	int32 NextIndex = 0;
	uint32 TotalNotificationsReceived = 0;
};

//--------------------------------------------------------------------------------------------------
// Unit testing
//--------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClientLobbyDataTest, 
	"System.Engine.Online.ClientLobbyDataTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FClientLobbyDataTest::RunTest(const FString& Parameters)
{
	// 1. Applying snapshot with no local members does not fire events.
	{
		FLobbyEvents LobbyEvents;
		FLobbyEventCapture EventCapture(LobbyEvents);
		FOnlineLobbyIdHandle LobbyId(EOnlineServices::Null, 1);
		FClientLobbyData ClientData(LobbyId);

		FOnlineAccountIdHandle User1(EOnlineServices::Null, 1);
		FOnlineAccountIdHandle User2(EOnlineServices::Null, 2);
		FName LobbySchemaName = TEXT("SchemaName");
		int32 LobbyMaxMembers = 5;
		ELobbyJoinPolicy LobbyJoinPolicy = ELobbyJoinPolicy::PublicAdvertised;
		FLobbyAttributeId LobbyAttribute1Key = TEXT("LobbyAttribute1");
		FLobbyVariant LobbyAttribute1Value;
		LobbyAttribute1Value.Set(TEXT("Attribute1"));
		FLobbyAttributeId LobbyAttribute2Key = TEXT("LobbyAttribute2");
		FLobbyVariant LobbyAttribute2Value;
		LobbyAttribute2Value.Set(TEXT("Attribute2"));
		FLobbyAttributeId LobbyMemberAttribute1Key = TEXT("LobbyMemberAttribute1");
		FLobbyVariant LobbyMemberAttribute1Value;
		LobbyMemberAttribute1Value.Set(TEXT("MemberAttribute1"));
		FLobbyAttributeId LobbyMemberAttribute2Key = TEXT("LobbyMemberAttribute2");
		FLobbyVariant LobbyMemberAttribute2Value;
		LobbyMemberAttribute2Value.Set(TEXT("MemberAttribute2"));

		// 1.1 setup.
		{
			TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> LobbyMemberSnapshots;
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User1;
				MemberSnapshot->Attributes.Add(LobbyMemberAttribute1Key, LobbyMemberAttribute1Value);
				LobbyMemberSnapshots.Add(User1, MemberSnapshot);
			}
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User2;
				LobbyMemberSnapshots.Add(User2, MemberSnapshot);
			}

			FClientLobbySnapshot LobbySnapshot;
			LobbySnapshot.OwnerAccountId = User1;
			LobbySnapshot.SchemaName = LobbySchemaName;
			LobbySnapshot.MaxMembers = LobbyMaxMembers;
			LobbySnapshot.JoinPolicy = LobbyJoinPolicy;
			LobbySnapshot.Attributes.Add(LobbyAttribute1Key, LobbyAttribute1Value);
			LobbySnapshot.Members = {User1, User2};

			TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason> LeaveReasons;
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromServiceSnapshot(MoveTemp(LobbySnapshot), MoveTemp(LobbyMemberSnapshots), MoveTemp(LeaveReasons), &LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 1.1.1. Initial snapshot does not trigger any events.
		UTEST_EQUAL("Snapshot does not trigger any events.", EventCapture.GetTotalNotificationsReceived(), 0);
		UTEST_EQUAL("Verify lobby owner.", ClientData.GetPublicData().OwnerAccountId, User1);
		UTEST_EQUAL("Verify lobby schema.", ClientData.GetPublicData().SchemaName, LobbySchemaName);
		UTEST_EQUAL("Verify lobby max members.", ClientData.GetPublicData().MaxMembers, LobbyMaxMembers);
		UTEST_EQUAL("Verify lobby join policy.", ClientData.GetPublicData().JoinPolicy, LobbyJoinPolicy);
		UTEST_EQUAL("Verify lobby attributes.", ClientData.GetPublicData().Attributes.Num(), 1);
		UTEST_NOT_NULL("Verify lobby attributes.", ClientData.GetPublicData().Attributes.Find(LobbyAttribute1Key));
		UTEST_EQUAL("Verify lobby attributes.", ClientData.GetPublicData().Attributes[LobbyAttribute1Key], LobbyAttribute1Value);
		UTEST_EQUAL("Verify lobby members.", ClientData.GetPublicData().Members.Num(), 2);
		UTEST_NOT_NULL("Verify lobby members.", ClientData.GetPublicData().Members.Find(User1));
		UTEST_EQUAL("Verify lobby members.", ClientData.GetPublicData().Members[User1]->AccountId, User1);
		UTEST_NOT_NULL("Verify lobby members.", ClientData.GetPublicData().Members.Find(User2));
		UTEST_EQUAL("Verify lobby members.", ClientData.GetPublicData().Members[User2]->AccountId, User2);
		UTEST_EQUAL("Verify lobby member attributes.", ClientData.GetPublicData().Members[User1]->Attributes.Num(), 1);
		UTEST_NOT_NULL("Verify lobby member attributes.", ClientData.GetPublicData().Members[User1]->Attributes.Find(LobbyMemberAttribute1Key));
		UTEST_EQUAL("Verify lobby member attributes.", ClientData.GetPublicData().Members[User1]->Attributes[LobbyMemberAttribute1Key], LobbyMemberAttribute1Value);

		// 1.2 setup.
		{
			LobbyAttribute1Value.Set(TEXT("ModifiedAttribute1"));
			LobbyMemberAttribute1Value.Set(TEXT("ModifiedMemberAttribute1"));
			LobbySchemaName = TEXT("ModifiedSchemaName");

			TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> LobbyMemberSnapshots;
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User1;
				MemberSnapshot->Attributes.Add(LobbyMemberAttribute1Key, LobbyMemberAttribute1Value);
				LobbyMemberSnapshots.Add(User1, MemberSnapshot);
			}
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User2;
				LobbyMemberSnapshots.Add(User2, MemberSnapshot);
			}

			FClientLobbySnapshot LobbySnapshot;
			LobbySnapshot.OwnerAccountId = User2;
			LobbySnapshot.SchemaName = LobbySchemaName;
			LobbySnapshot.MaxMembers = LobbyMaxMembers;
			LobbySnapshot.JoinPolicy = LobbyJoinPolicy;
			LobbySnapshot.Attributes.Add(LobbyAttribute1Key, LobbyAttribute1Value);
			LobbySnapshot.Members = {User1, User2};

			TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason> LeaveReasons;
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromServiceSnapshot(MoveTemp(LobbySnapshot), MoveTemp(LobbyMemberSnapshots), MoveTemp(LeaveReasons), &LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 1.2.1. Snapshot with changed lobby attribute does not trigger lobby attribute changed.
		// 1.2.2. Snapshot with changed lobby member attribute does not trigger lobby member attribute changed.
		// 1.2.3. Snapshot with changed lobby schema does not trigger lobby schema changed.
		// 1.2.4. Snapshot with changed lobby leader does not trigger lobby leader changed.
		UTEST_EQUAL("Snapshot does not trigger any events.", EventCapture.GetTotalNotificationsReceived(), 0);
		UTEST_EQUAL("Verify lobby owner.", ClientData.GetPublicData().OwnerAccountId, User2);
		UTEST_EQUAL("Verify lobby schema.", ClientData.GetPublicData().SchemaName, LobbySchemaName);
		UTEST_EQUAL("Verify lobby max members.", ClientData.GetPublicData().MaxMembers, LobbyMaxMembers);
		UTEST_EQUAL("Verify lobby join policy.", ClientData.GetPublicData().JoinPolicy, LobbyJoinPolicy);
		UTEST_EQUAL("Verify lobby attributes.", ClientData.GetPublicData().Attributes.Num(), 1);
		UTEST_NOT_NULL("Verify lobby attributes.", ClientData.GetPublicData().Attributes.Find(LobbyAttribute1Key));
		UTEST_EQUAL("Verify lobby attributes.", ClientData.GetPublicData().Attributes[LobbyAttribute1Key], LobbyAttribute1Value);
		UTEST_EQUAL("Verify lobby members.", ClientData.GetPublicData().Members.Num(), 2);
		UTEST_NOT_NULL("Verify lobby members.", ClientData.GetPublicData().Members.Find(User1));
		UTEST_NOT_NULL("Verify lobby members.", ClientData.GetPublicData().Members.Find(User2));
		UTEST_EQUAL("Verify lobby member attributes.", ClientData.GetPublicData().Members[User1]->Attributes.Num(), 1);
		UTEST_NOT_NULL("Verify lobby member attributes.", ClientData.GetPublicData().Members[User1]->Attributes.Find(LobbyMemberAttribute1Key));
		UTEST_EQUAL("Verify lobby member attributes.", ClientData.GetPublicData().Members[User1]->Attributes[LobbyMemberAttribute1Key], LobbyMemberAttribute1Value);

		// 1.3 setup.
		{
			TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> LobbyMemberSnapshots;
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User1;
				LobbyMemberSnapshots.Add(User1, MemberSnapshot);
			}

			FClientLobbySnapshot LobbySnapshot;
			LobbySnapshot.OwnerAccountId = User1;
			LobbySnapshot.SchemaName = LobbySchemaName;
			LobbySnapshot.MaxMembers = LobbyMaxMembers;
			LobbySnapshot.JoinPolicy = LobbyJoinPolicy;
			LobbySnapshot.Members = {User1};

			TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason> LeaveReasons;
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromServiceSnapshot(MoveTemp(LobbySnapshot), MoveTemp(LobbyMemberSnapshots), MoveTemp(LeaveReasons), &LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 1.3.1. Snapshot with cleared lobby attribute does not trigger lobby attribute changed.
		// 1.3.2. Snapshot with cleared lobby member attribute does not trigger lobby member attribute changed.
		// 1.3.3. Snapshot with members leaving does not trigger lobby member left or lobby left.
		UTEST_EQUAL("Snapshot does not trigger any events.", EventCapture.GetTotalNotificationsReceived(), 0);
		UTEST_EQUAL("Verify lobby attributes.", ClientData.GetPublicData().Attributes.Num(), 0);
		UTEST_EQUAL("Verify lobby members.", ClientData.GetPublicData().Members.Num(), 1);
		UTEST_NOT_NULL("Verify lobby members.", ClientData.GetPublicData().Members.Find(User1));
		UTEST_EQUAL("Verify lobby member attributes.", ClientData.GetPublicData().Members[User1]->Attributes.Num(), 0);
	}

	// 2. Applying snapshot with local members present does fire events.
	{
		FLobbyEvents LobbyEvents;
		FLobbyEventCapture EventCapture(LobbyEvents);
		FOnlineLobbyIdHandle LobbyId(EOnlineServices::Null, 1);
		FClientLobbyData ClientData(LobbyId);

		FOnlineAccountIdHandle User1(EOnlineServices::Null, 1);
		FOnlineAccountIdHandle User2(EOnlineServices::Null, 2);
		FName LobbySchemaName = TEXT("SchemaName");
		int32 LobbyMaxMembers = 5;
		ELobbyJoinPolicy LobbyJoinPolicy = ELobbyJoinPolicy::PublicAdvertised;
		FLobbyAttributeId LobbyAttribute1Key = TEXT("LobbyAttribute1");
		FLobbyVariant LobbyAttribute1Value;
		LobbyAttribute1Value.Set(TEXT("Attribute1"));
		FLobbyAttributeId LobbyAttribute2Key = TEXT("LobbyAttribute2");
		FLobbyVariant LobbyAttribute2Value;
		LobbyAttribute2Value.Set(TEXT("Attribute2"));
		FLobbyAttributeId LobbyMemberAttribute1Key = TEXT("LobbyMemberAttribute1");
		FLobbyVariant LobbyMemberAttribute1Value;
		LobbyMemberAttribute1Value.Set(TEXT("MemberAttribute1"));
		FLobbyAttributeId LobbyMemberAttribute2Key = TEXT("LobbyMemberAttribute2");
		FLobbyVariant LobbyMemberAttribute2Value;
		LobbyMemberAttribute2Value.Set(TEXT("MemberAttribute2"));

		// 2.1 setup
		{
			TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> LobbyMemberSnapshots;
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User1;
				LobbyMemberSnapshots.Add(User1, MemberSnapshot);
			}
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User2;
				MemberSnapshot->Attributes.Add(LobbyMemberAttribute1Key, LobbyMemberAttribute1Value);
				LobbyMemberSnapshots.Add(User2, MemberSnapshot);
			}

			FClientLobbySnapshot LobbySnapshot;
			LobbySnapshot.OwnerAccountId = User1;
			LobbySnapshot.SchemaName = LobbySchemaName;
			LobbySnapshot.MaxMembers = LobbyMaxMembers;
			LobbySnapshot.JoinPolicy = LobbyJoinPolicy;
			LobbySnapshot.Attributes.Add(LobbyAttribute1Key, LobbyAttribute1Value);
			LobbySnapshot.Members = {User1, User2};

			TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason> LeaveReasons;
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromServiceSnapshot(MoveTemp(LobbySnapshot), MoveTemp(LobbyMemberSnapshots), MoveTemp(LeaveReasons), &LobbyEvents);
			UTEST_EQUAL("Check no members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 2.1.1. Initial snapshot does not trigger lobby joined or lobby member joined. - Member in snapshot is local member to simulate lobby creation.
		UTEST_EQUAL("Snapshot does not trigger any events.", EventCapture.GetTotalNotificationsReceived(), 0);
		UTEST_EQUAL("Verify lobby members.", ClientData.GetPublicData().Members.Num(), 2);
		UTEST_NOT_NULL("Verify lobby members.", ClientData.GetPublicData().Members.Find(User1));
		UTEST_NOT_NULL("Verify lobby members.", ClientData.GetPublicData().Members.Find(User2));

		// 2.2 setup
		{
			EventCapture.Empty();

			FClientLobbyDataChanges LobbyChanges;
			LobbyChanges.MutatedMembers.Add(User1, MakeShared<FClientLobbyMemberDataChanges>());
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromLocalChanges(MoveTemp(LobbyChanges), LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 2.2.1. Local member added to lobby through local change. Adding a local member will cause events to begin triggering.
		UTEST_EQUAL("Check lobby and member joined events were received.", EventCapture.GetTotalNotificationsReceived(), 3);
		UTEST_EQUAL("Check lobby joined event received.", EventCapture.LobbyJoined.Num(), 1);
		UTEST_EQUAL("Check lobby joined event received first.", EventCapture.LobbyJoined[0].GlobalIndex, 0);
		UTEST_EQUAL("Check lobby joined event is valid.", EventCapture.LobbyJoined[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check member joined events received.", EventCapture.LobbyMemberJoined.Num(), 2);
		UTEST_EQUAL("Check member joined event is valid.", EventCapture.LobbyMemberJoined[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_TRUE("Check member joined event is valid.", EventCapture.LobbyMemberJoined[0].Notification.Member->AccountId.IsValid());

		// 2.3 setup
		{
			EventCapture.Empty();

			LobbyAttribute1Value.Set(TEXT("ModifiedAttribute1"));
			LobbyMemberAttribute1Value.Set(TEXT("ModifiedMemberAttribute1"));
			LobbySchemaName = TEXT("ModifiedSchemaName");

			TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> LobbyMemberSnapshots;
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User1;
				LobbyMemberSnapshots.Add(User1, MemberSnapshot);
			}
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User2;
				MemberSnapshot->Attributes.Add(LobbyMemberAttribute1Key, LobbyMemberAttribute1Value);
				LobbyMemberSnapshots.Add(User2, MemberSnapshot);
			}

			FClientLobbySnapshot LobbySnapshot;
			LobbySnapshot.OwnerAccountId = User2;
			LobbySnapshot.SchemaName = LobbySchemaName;
			LobbySnapshot.MaxMembers = LobbyMaxMembers;
			LobbySnapshot.JoinPolicy = LobbyJoinPolicy;
			LobbySnapshot.Attributes.Add(LobbyAttribute1Key, LobbyAttribute1Value);
			LobbySnapshot.Members = {User1, User2};

			TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason> LeaveReasons;
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromServiceSnapshot(MoveTemp(LobbySnapshot), MoveTemp(LobbyMemberSnapshots), MoveTemp(LeaveReasons), &LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 2.3.1. Snapshot with changed lobby attribute triggers lobby attribute changed.
		// 2.3.2. Snapshot with changed lobby member attribute triggers lobby member attribute changed.
		// 2.3.3. Snapshot with changed lobby schema triggers lobby schema changed.
		// 2.3.4. Snapshot with changed lobby leader triggers lobby leader changed.
		UTEST_EQUAL("Check events received.", EventCapture.GetTotalNotificationsReceived(), 4);
		UTEST_EQUAL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged.Num(), 1);
		UTEST_EQUAL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged[0].Notification.ChangedAttributes.Num(), 1);
		UTEST_NOT_NULL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged[0].Notification.ChangedAttributes.Find(LobbyAttribute1Key));
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged.Num(), 1);
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.Member->AccountId, User2);
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.ChangedAttributes.Num(), 1);
		UTEST_NOT_NULL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.ChangedAttributes.Find(LobbyMemberAttribute1Key));
		UTEST_EQUAL("Check LobbySchemaChanged is valid.", EventCapture.LobbySchemaChanged.Num(), 1);
		UTEST_EQUAL("Check LobbySchemaChanged is valid.", EventCapture.LobbySchemaChanged[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyLeaderChanged is valid.", EventCapture.LobbyLeaderChanged.Num(), 1);
		UTEST_EQUAL("Check LobbyLeaderChanged is valid.", EventCapture.LobbyLeaderChanged[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyLeaderChanged is valid.", EventCapture.LobbyLeaderChanged[0].Notification.Leader->AccountId, User2);

		// 2.4 setup
		{
			EventCapture.Empty();

			TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> LobbyMemberSnapshots;
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User2;
				LobbyMemberSnapshots.Add(User2, MemberSnapshot);
			}

			FClientLobbySnapshot LobbySnapshot;
			LobbySnapshot.OwnerAccountId = User2;
			LobbySnapshot.SchemaName = LobbySchemaName;
			LobbySnapshot.MaxMembers = LobbyMaxMembers;
			LobbySnapshot.JoinPolicy = LobbyJoinPolicy;
			LobbySnapshot.Members = {User2};

			TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason> LeaveReasons;
			LeaveReasons.Add(User1, ELobbyMemberLeaveReason::Kicked);

			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromServiceSnapshot(MoveTemp(LobbySnapshot), MoveTemp(LobbyMemberSnapshots), MoveTemp(LeaveReasons), &LobbyEvents);
			UTEST_EQUAL("Check local member left.", Result.LeavingLocalMembers.Num(), 1);
			UTEST_EQUAL("Check local member left.", Result.LeavingLocalMembers[0], User1);
		}

		// 2.4.1. Snapshot with cleared lobby attribute triggers lobby attribute changed.
		// 2.4.2. Snapshot with cleared lobby member attribute triggers lobby member attribute changed.
		// 2.4.3. Snapshot with members leaving triggers lobby member left and lobby left.
		UTEST_EQUAL("Check events received.", EventCapture.GetTotalNotificationsReceived(), 5);
		UTEST_EQUAL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged.Num(), 1);
		UTEST_EQUAL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged[0].Notification.ChangedAttributes.Num(), 1);
		UTEST_NOT_NULL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged[0].Notification.ChangedAttributes.Find(LobbyAttribute1Key));
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged.Num(), 1);
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.Member->AccountId, User2);
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.ChangedAttributes.Num(), 1);
		UTEST_NOT_NULL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.ChangedAttributes.Find(LobbyMemberAttribute1Key));
		UTEST_EQUAL("Check LobbyMemberLeft valid.", EventCapture.LobbyMemberLeft.Num(), 2);
		FLobbyEventCapture::TNotificationInfo<FLobbyMemberLeft>* User1LeaveNotification = EventCapture.LobbyMemberLeft[0].Notification.Member->AccountId == User1 ? &EventCapture.LobbyMemberLeft[0] : &EventCapture.LobbyMemberLeft[1];
		FLobbyEventCapture::TNotificationInfo<FLobbyMemberLeft>* User2LeaveNotification = EventCapture.LobbyMemberLeft[0].Notification.Member->AccountId == User2 ? &EventCapture.LobbyMemberLeft[0] : &EventCapture.LobbyMemberLeft[1];
		UTEST_EQUAL("Check LobbyMemberLeft valid.", User1LeaveNotification->Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyMemberLeft valid.", User1LeaveNotification->Notification.Member->AccountId, User1);
		UTEST_EQUAL("Check LobbyMemberLeft valid.", User1LeaveNotification->Notification.Reason, ELobbyMemberLeaveReason::Kicked);
		UTEST_EQUAL("Check LobbyMemberLeft valid.", User2LeaveNotification->Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyMemberLeft valid.", User2LeaveNotification->Notification.Member->AccountId, User2);
		UTEST_EQUAL("Check LobbyMemberLeft valid.", User2LeaveNotification->Notification.Reason, ELobbyMemberLeaveReason::Left);
		UTEST_EQUAL("Check LobbyLeft valid.", EventCapture.LobbyLeft.Num(), 1);
		UTEST_EQUAL("Check LobbyLeft valid.", EventCapture.LobbyLeft[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyLeft ordering.", EventCapture.LobbyLeft[0].GlobalIndex, EventCapture.GetTotalNotificationsReceived() - 1);
		FLobbyEventCapture::TNotificationInfo<FLobbyMemberLeft>* LastMemberLeaveNotification = User1LeaveNotification->GlobalIndex > User2LeaveNotification->GlobalIndex ? User1LeaveNotification : User2LeaveNotification;
		UTEST_EQUAL("Check LobbyMemberLeft ordering.", LastMemberLeaveNotification->GlobalIndex, EventCapture.LobbyLeft[0].GlobalIndex - 1);
	}

	// 3. Snapshot supplemental
	{
		FLobbyEvents LobbyEvents;
		FLobbyEventCapture EventCapture(LobbyEvents);
		FOnlineLobbyIdHandle LobbyId(EOnlineServices::Null, 1);
		FClientLobbyData ClientData(LobbyId);

		FOnlineAccountIdHandle User1(EOnlineServices::Null, 1);
		FOnlineAccountIdHandle User2(EOnlineServices::Null, 2);
		FName LobbySchemaName = TEXT("SchemaName");
		int32 LobbyMaxMembers = 5;
		ELobbyJoinPolicy LobbyJoinPolicy = ELobbyJoinPolicy::PublicAdvertised;

		// 3.1 setup - initialize lobby data with initial snapshot.
		{
			TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> LobbyMemberSnapshots;
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User1;
				LobbyMemberSnapshots.Add(User1, MemberSnapshot);
			}
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User2;
				LobbyMemberSnapshots.Add(User2, MemberSnapshot);
			}

			FClientLobbySnapshot LobbySnapshot;
			LobbySnapshot.OwnerAccountId = User1;
			LobbySnapshot.SchemaName = LobbySchemaName;
			LobbySnapshot.MaxMembers = LobbyMaxMembers;
			LobbySnapshot.JoinPolicy = LobbyJoinPolicy;
			LobbySnapshot.Members = {User1, User2};

			TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason> LeaveReasons;
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromServiceSnapshot(MoveTemp(LobbySnapshot), MoveTemp(LobbyMemberSnapshots), MoveTemp(LeaveReasons), &LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 3.1 setup continued. Add local member to enable notifications.
		{
			FClientLobbyDataChanges LobbyChanges;
			LobbyChanges.MutatedMembers.Add(User1, MakeShared<FClientLobbyMemberDataChanges>());
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromLocalChanges(MoveTemp(LobbyChanges), LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 3.1 setup continued. Change snapshot for the test.
		{
			EventCapture.Empty();

			TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> LobbyMemberSnapshots;
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User1;
				LobbyMemberSnapshots.Add(User1, MemberSnapshot);
			}

			FClientLobbySnapshot LobbySnapshot;
			LobbySnapshot.OwnerAccountId = User1;
			LobbySnapshot.SchemaName = LobbySchemaName;
			LobbySnapshot.MaxMembers = LobbyMaxMembers;
			LobbySnapshot.JoinPolicy = LobbyJoinPolicy;
			LobbySnapshot.Members = {User1};

			TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason> LeaveReasons;
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromServiceSnapshot(MoveTemp(LobbySnapshot), MoveTemp(LobbyMemberSnapshots), MoveTemp(LeaveReasons), &LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 3.1. Test that Member left is fired even when a leave reason is not specified.
		UTEST_EQUAL("Check events received.", EventCapture.GetTotalNotificationsReceived(), 1);
		UTEST_EQUAL("Check LobbyMemberLeft valid.", EventCapture.LobbyMemberLeft.Num(), 1);
		UTEST_EQUAL("Check LobbyMemberLeft valid.", EventCapture.LobbyMemberLeft[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyMemberLeft valid.", EventCapture.LobbyMemberLeft[0].Notification.Member->AccountId, User2);
		UTEST_EQUAL("Check LobbyMemberLeft valid.", EventCapture.LobbyMemberLeft[0].Notification.Reason, ELobbyMemberLeaveReason::Disconnected);

		// 3.2 setup
		{
			EventCapture.Empty();

			TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> LobbyMemberSnapshots;
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User1;
				LobbyMemberSnapshots.Add(User1, MemberSnapshot);
			}
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User2;
				LobbyMemberSnapshots.Add(User2, MemberSnapshot);
			}

			FClientLobbySnapshot LobbySnapshot;
			LobbySnapshot.OwnerAccountId = User1;
			LobbySnapshot.SchemaName = LobbySchemaName;
			LobbySnapshot.MaxMembers = LobbyMaxMembers;
			LobbySnapshot.JoinPolicy = LobbyJoinPolicy;
			LobbySnapshot.Members = {User1};

			TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason> LeaveReasons;
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromServiceSnapshot(MoveTemp(LobbySnapshot), MoveTemp(LobbyMemberSnapshots), MoveTemp(LeaveReasons), &LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 3.2. Applying member snapshots for an account id not in the lobby members list does not fire lobby member joined.
		UTEST_EQUAL("Check no events received.", EventCapture.GetTotalNotificationsReceived(), 0);

		// 3.3 setup
		{
			EventCapture.Empty();

			TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> LobbyMemberSnapshots;
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User1;
				LobbyMemberSnapshots.Add(User1, MemberSnapshot);
			}
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User2;
				LobbyMemberSnapshots.Add(User2, MemberSnapshot);
			}

			FClientLobbySnapshot LobbySnapshot;
			LobbySnapshot.OwnerAccountId = User1;
			LobbySnapshot.SchemaName = LobbySchemaName;
			LobbySnapshot.MaxMembers = LobbyMaxMembers;
			LobbySnapshot.JoinPolicy = LobbyJoinPolicy;
			LobbySnapshot.Members = {User1, User2};

			TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason> LeaveReasons;
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromServiceSnapshot(MoveTemp(LobbySnapshot), MoveTemp(LobbyMemberSnapshots), MoveTemp(LeaveReasons), &LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 3.3. Check lobby member joined notification received for remote member after local member joined.
		UTEST_EQUAL("Check events received.", EventCapture.GetTotalNotificationsReceived(), 1);
		UTEST_EQUAL("Check LobbyMemberJoined valid.", EventCapture.LobbyMemberJoined.Num(), 1);
		UTEST_EQUAL("Check LobbyMemberJoined valid.", EventCapture.LobbyMemberJoined[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyMemberJoined valid.", EventCapture.LobbyMemberJoined[0].Notification.Member->AccountId, User2);
	}

	// 4. Applying lobby changes produces expected notifications.
	{
		FLobbyEvents LobbyEvents;
		FLobbyEventCapture EventCapture(LobbyEvents);
		FOnlineLobbyIdHandle LobbyId(EOnlineServices::Null, 1);
		FClientLobbyData ClientData(LobbyId);

		FOnlineAccountIdHandle User1(EOnlineServices::Null, 1);
		FOnlineAccountIdHandle User2(EOnlineServices::Null, 2);
		FName LobbySchemaName = TEXT("SchemaName");
		int32 LobbyMaxMembers = 5;
		ELobbyJoinPolicy LobbyJoinPolicy = ELobbyJoinPolicy::PublicAdvertised;
		FLobbyAttributeId LobbyAttribute1Key = TEXT("LobbyAttribute1");
		FLobbyVariant LobbyAttribute1Value;
		LobbyAttribute1Value.Set(TEXT("Attribute1"));
		FLobbyAttributeId LobbyAttribute2Key = TEXT("LobbyAttribute2");
		FLobbyVariant LobbyAttribute2Value;
		LobbyAttribute2Value.Set(TEXT("Attribute2"));
		FLobbyAttributeId LobbyMemberAttribute1Key = TEXT("LobbyMemberAttribute1");
		FLobbyVariant LobbyMemberAttribute1Value;
		LobbyMemberAttribute1Value.Set(TEXT("MemberAttribute1"));
		FLobbyAttributeId LobbyMemberAttribute2Key = TEXT("LobbyMemberAttribute2");
		FLobbyVariant LobbyMemberAttribute2Value;
		LobbyMemberAttribute2Value.Set(TEXT("MemberAttribute2"));

		// 4.1 setup. Local member in snapshot with remote member to simulate lobby join.
		{
			TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> LobbyMemberSnapshots;
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User1;
				MemberSnapshot->Attributes.Add(LobbyMemberAttribute1Key, LobbyMemberAttribute1Value);
				LobbyMemberSnapshots.Add(User1, MemberSnapshot);
			}
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User2;
				LobbyMemberSnapshots.Add(User2, MemberSnapshot);
			}

			FClientLobbySnapshot LobbySnapshot;
			LobbySnapshot.OwnerAccountId = User1;
			LobbySnapshot.SchemaName = LobbySchemaName;
			LobbySnapshot.MaxMembers = LobbyMaxMembers;
			LobbySnapshot.JoinPolicy = LobbyJoinPolicy;
			LobbySnapshot.Attributes.Add(LobbyAttribute1Key, LobbyAttribute1Value);
			LobbySnapshot.Members = {User1, User2};

			TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason> LeaveReasons;
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromServiceSnapshot(MoveTemp(LobbySnapshot), MoveTemp(LobbyMemberSnapshots), MoveTemp(LeaveReasons), &LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		UTEST_EQUAL("Check no events received.", EventCapture.GetTotalNotificationsReceived(), 0);

		// 4.1 setup continued.
		{
			LobbyAttribute1Value.Set(TEXT("MutatedAttribute1"));
			LobbySchemaName = TEXT("Beans");

			FClientLobbyDataChanges LobbyChanges;
			LobbyChanges.MutatedAttributes.Add(LobbyAttribute1Key, LobbyAttribute1Value);
			LobbyChanges.LobbySchema = LobbySchemaName;
			LobbyChanges.OwnerAccountId = User2;
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromLocalChanges(MoveTemp(LobbyChanges), LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 4.1.1. Local changes with changed lobby attribute does not trigger lobby attribute changed.
		// 4.1.2. Local changes with changed lobby schema does not trigger lobby schema changed.
		// 4.1.3. Local changes with changed lobby leader does not trigger lobby leader changed.
		UTEST_EQUAL("Check no events received.", EventCapture.GetTotalNotificationsReceived(), 0);

		// 4.2 setup.
		{
			FClientLobbyDataChanges LobbyChanges;
			LobbyChanges.MutatedMembers.Add(User1, MakeShared<FClientLobbyMemberDataChanges>());
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromLocalChanges(MoveTemp(LobbyChanges), LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 4.2.1. Local members added to lobby through local change triggers lobby joined and lobby member joined. On member joined triggered for remote member.
		UTEST_EQUAL("Check events received.", EventCapture.GetTotalNotificationsReceived(), 3);
		UTEST_EQUAL("Check LobbyJoined valid.", EventCapture.LobbyJoined.Num(), 1);
		UTEST_EQUAL("Check LobbyJoined valid.", EventCapture.LobbyJoined[0].GlobalIndex, 0);
		UTEST_EQUAL("Check LobbyMemberJoined valid.", EventCapture.LobbyMemberJoined.Num(), 2);
		FLobbyEventCapture::TNotificationInfo<FLobbyMemberJoined>* User1JoinNotification = EventCapture.LobbyMemberJoined[0].Notification.Member->AccountId == User1 ? &EventCapture.LobbyMemberJoined[0] : &EventCapture.LobbyMemberJoined[1];
		FLobbyEventCapture::TNotificationInfo<FLobbyMemberJoined>* User2JoinNotification = EventCapture.LobbyMemberJoined[0].Notification.Member->AccountId == User2 ? &EventCapture.LobbyMemberJoined[0] : &EventCapture.LobbyMemberJoined[1];
		UTEST_EQUAL("Check LobbyMemberJoined valid.", User1JoinNotification->Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyMemberJoined valid.", User1JoinNotification->Notification.Member->AccountId, User1);
		UTEST_EQUAL("Check LobbyMemberJoined valid.", User1JoinNotification->Notification.Member->Attributes.Num(), 1);
		UTEST_EQUAL("Check LobbyMemberJoined valid.", User2JoinNotification->Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyMemberJoined valid.", User2JoinNotification->Notification.Member->AccountId, User2);

		// 4.3 setup
		{
			EventCapture.Empty();

			LobbyAttribute1Value.Set(TEXT("DoubleMutatedAttribute1"));
			LobbyMemberAttribute1Value.Set(TEXT("DoubleMutatedMemberAttribute1"));
			LobbySchemaName = TEXT("Waffles");

			TSharedRef<FClientLobbyMemberDataChanges> User1Changes = MakeShared<FClientLobbyMemberDataChanges>();
			User1Changes->MutatedAttributes.Add(LobbyMemberAttribute1Key, LobbyMemberAttribute1Value);

			FClientLobbyDataChanges LobbyChanges;
			LobbyChanges.MutatedAttributes.Add(LobbyAttribute1Key, LobbyAttribute1Value);
			LobbyChanges.MutatedMembers.Add(User1, User1Changes);
			LobbyChanges.LobbySchema = LobbySchemaName;
			LobbyChanges.OwnerAccountId = User1;
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromLocalChanges(MoveTemp(LobbyChanges), LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 4.3.1. Local changes with changed lobby attribute triggers lobby attribute changed.
		// 4.3.2. Local changes with changed lobby member attribute triggers lobby member attribute changed.
		// 4.3.3. Local changes with changed lobby schema triggers lobby schema changed.
		// 4.3.4. Local changes with changed lobby leader triggers lobby leader changed.
		UTEST_EQUAL("Check events received.", EventCapture.GetTotalNotificationsReceived(), 4);
		UTEST_EQUAL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged.Num(), 1);
		UTEST_EQUAL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged[0].Notification.ChangedAttributes.Num(), 1);
		UTEST_NOT_NULL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged[0].Notification.ChangedAttributes.Find(LobbyAttribute1Key));
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged.Num(), 1);
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.Member->AccountId, User1);
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.ChangedAttributes.Num(), 1);
		UTEST_NOT_NULL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.ChangedAttributes.Find(LobbyMemberAttribute1Key));
		UTEST_EQUAL("Check LobbySchemaChanged is valid.", EventCapture.LobbySchemaChanged.Num(), 1);
		UTEST_EQUAL("Check LobbySchemaChanged is valid.", EventCapture.LobbySchemaChanged[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyLeaderChanged is valid.", EventCapture.LobbyLeaderChanged.Num(), 1);
		UTEST_EQUAL("Check LobbyLeaderChanged is valid.", EventCapture.LobbyLeaderChanged[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyLeaderChanged is valid.", EventCapture.LobbyLeaderChanged[0].Notification.Leader->AccountId, User1);

		// 4.4 setup
		{
			EventCapture.Empty();

			TSharedRef<FClientLobbyMemberDataChanges> User1Changes = MakeShared<FClientLobbyMemberDataChanges>();
			User1Changes->ClearedAttributes.Add(LobbyMemberAttribute1Key);

			FClientLobbyDataChanges LobbyChanges;
			LobbyChanges.ClearedAttributes.Add(LobbyAttribute1Key);
			LobbyChanges.MutatedMembers.Add(User1, User1Changes);
			LobbyChanges.LeavingMembers.Add(User1, ELobbyMemberLeaveReason::Left);
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromLocalChanges(MoveTemp(LobbyChanges), LobbyEvents);
			UTEST_EQUAL("Check local member left.", Result.LeavingLocalMembers.Num(), 1);
			UTEST_EQUAL("Check local member left.", Result.LeavingLocalMembers[0], User1);
		}

		// 4.4.1. Local changes with cleared lobby attribute triggers lobby attribute changed.
		// 4.4.2. Local changes with cleared member attribute triggers member attribute changed.
		// 4.4.1. Local member removed through local change results in member leave for all members followed by lobby leave.
		UTEST_EQUAL("Check events received.", EventCapture.GetTotalNotificationsReceived(), 5);
		UTEST_EQUAL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged.Num(), 1);
		UTEST_EQUAL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged[0].Notification.ChangedAttributes.Num(), 1);
		UTEST_NOT_NULL("Check LobbyAttributesChanged is valid.", EventCapture.LobbyAttributesChanged[0].Notification.ChangedAttributes.Find(LobbyAttribute1Key));
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged.Num(), 1);
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.Member->AccountId, User1);
		UTEST_EQUAL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.ChangedAttributes.Num(), 1);
		UTEST_NOT_NULL("Check LobbyMemberAttributesChanged is valid.", EventCapture.LobbyMemberAttributesChanged[0].Notification.ChangedAttributes.Find(LobbyMemberAttribute1Key));
		UTEST_EQUAL("Check LobbyMemberLeft valid.", EventCapture.LobbyMemberLeft.Num(), 2);
		FLobbyEventCapture::TNotificationInfo<FLobbyMemberLeft>* User1LeaveNotification = EventCapture.LobbyMemberLeft[0].Notification.Member->AccountId == User1 ? &EventCapture.LobbyMemberLeft[0] : &EventCapture.LobbyMemberLeft[1];
		FLobbyEventCapture::TNotificationInfo<FLobbyMemberLeft>* User2LeaveNotification = EventCapture.LobbyMemberLeft[0].Notification.Member->AccountId == User2 ? &EventCapture.LobbyMemberLeft[0] : &EventCapture.LobbyMemberLeft[1];
		UTEST_EQUAL("Check LobbyMemberLeft valid.", User1LeaveNotification->Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyMemberLeft valid.", User1LeaveNotification->Notification.Member->AccountId, User1);
		UTEST_EQUAL("Check LobbyMemberLeft valid.", User1LeaveNotification->Notification.Reason, ELobbyMemberLeaveReason::Left);
		UTEST_EQUAL("Check LobbyMemberLeft valid.", User2LeaveNotification->Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyMemberLeft valid.", User2LeaveNotification->Notification.Member->AccountId, User2);
		UTEST_EQUAL("Check LobbyMemberLeft valid.", User2LeaveNotification->Notification.Reason, ELobbyMemberLeaveReason::Left);
		UTEST_EQUAL("Check LobbyLeft valid.", EventCapture.LobbyLeft.Num(), 1);
		UTEST_EQUAL("Check LobbyLeft valid.", EventCapture.LobbyLeft[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyLeft ordering.", EventCapture.LobbyLeft[0].GlobalIndex, EventCapture.GetTotalNotificationsReceived() - 1);
		FLobbyEventCapture::TNotificationInfo<FLobbyMemberLeft>* LastMemberLeaveNotification = User1LeaveNotification->GlobalIndex > User2LeaveNotification->GlobalIndex ? User1LeaveNotification : User2LeaveNotification;
		UTEST_EQUAL("Check LobbyMemberLeft ordering.", LastMemberLeaveNotification->GlobalIndex, EventCapture.LobbyLeft[0].GlobalIndex - 1);
	}

	// 5. Local changes supplemental
	{
		FLobbyEvents LobbyEvents;
		FLobbyEventCapture EventCapture(LobbyEvents);
		FOnlineLobbyIdHandle LobbyId(EOnlineServices::Null, 1);
		FClientLobbyData ClientData(LobbyId);

		FOnlineAccountIdHandle User1(EOnlineServices::Null, 1);
		FOnlineAccountIdHandle User2(EOnlineServices::Null, 2);
		FName LobbySchemaName = TEXT("SchemaName");
		int32 LobbyMaxMembers = 5;
		ELobbyJoinPolicy LobbyJoinPolicy = ELobbyJoinPolicy::PublicAdvertised;
		FLobbyAttributeId LobbyAttribute1Key = TEXT("LobbyAttribute1");
		FLobbyVariant LobbyAttribute1Value;
		LobbyAttribute1Value.Set(TEXT("Attribute1"));
		FLobbyAttributeId LobbyAttribute2Key = TEXT("LobbyAttribute2");
		FLobbyVariant LobbyAttribute2Value;
		LobbyAttribute2Value.Set(TEXT("Attribute2"));
		FLobbyAttributeId LobbyMemberAttribute1Key = TEXT("LobbyMemberAttribute1");
		FLobbyVariant LobbyMemberAttribute1Value;
		LobbyMemberAttribute1Value.Set(TEXT("MemberAttribute1"));
		FLobbyAttributeId LobbyMemberAttribute2Key = TEXT("LobbyMemberAttribute2");
		FLobbyVariant LobbyMemberAttribute2Value;
		LobbyMemberAttribute2Value.Set(TEXT("MemberAttribute2"));

		// 5.1 setup.
		{
			TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>> LobbyMemberSnapshots;
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User1;
				MemberSnapshot->Attributes.Add(LobbyMemberAttribute1Key, LobbyMemberAttribute1Value);
				LobbyMemberSnapshots.Add(User1, MemberSnapshot);
			}
			{
				TSharedRef<FClientLobbyMemberSnapshot> MemberSnapshot = MakeShared<FClientLobbyMemberSnapshot>();
				MemberSnapshot->AccountId = User2;
				LobbyMemberSnapshots.Add(User2, MemberSnapshot);
			}

			FClientLobbySnapshot LobbySnapshot;
			LobbySnapshot.OwnerAccountId = User1;
			LobbySnapshot.SchemaName = LobbySchemaName;
			LobbySnapshot.MaxMembers = LobbyMaxMembers;
			LobbySnapshot.JoinPolicy = LobbyJoinPolicy;
			LobbySnapshot.Attributes.Add(LobbyAttribute1Key, LobbyAttribute1Value);
			LobbySnapshot.Members = {User1, User2};

			TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason> LeaveReasons;
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromServiceSnapshot(MoveTemp(LobbySnapshot), MoveTemp(LobbyMemberSnapshots), MoveTemp(LeaveReasons), &LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 5.1 setup continued.
		{
			FClientLobbyDataChanges LobbyChanges;
			LobbyChanges.MutatedMembers.Add(User1, MakeShared<FClientLobbyMemberDataChanges>());
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromLocalChanges(MoveTemp(LobbyChanges), LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 5.1 setup continued.
		{
			EventCapture.Empty();

			FClientLobbyDataChanges LobbyChanges;
			LobbyChanges.LeavingMembers.Add(User2, ELobbyMemberLeaveReason::Kicked);
			FApplyLobbyUpdateResult Result = ClientData.ApplyLobbyUpdateFromLocalChanges(MoveTemp(LobbyChanges), LobbyEvents);
			UTEST_EQUAL("Check no local members left.", Result.LeavingLocalMembers.Num(), 0);
		}

		// 5.1 Test local lobby owner kicking remote member.
		UTEST_EQUAL("Check events received.", EventCapture.GetTotalNotificationsReceived(), 1);
		UTEST_EQUAL("Check LobbyMemberLeft valid.", EventCapture.LobbyMemberLeft.Num(), 1);
		UTEST_EQUAL("Check LobbyMemberLeft valid.", EventCapture.LobbyMemberLeft[0].Notification.Lobby, ClientData.GetPublicDataPtr());
		UTEST_EQUAL("Check LobbyMemberLeft valid.", EventCapture.LobbyMemberLeft[0].Notification.Member->AccountId, User2);
		UTEST_EQUAL("Check LobbyMemberLeft valid.", EventCapture.LobbyMemberLeft[0].Notification.Reason, ELobbyMemberLeaveReason::Kicked);
	}

	return true;
}

//--------------------------------------------------------------------------------------------------
// Functional testing
//--------------------------------------------------------------------------------------------------

#if LOBBIES_FUNCTIONAL_TEST_ENABLED
namespace Private {

template <typename SecondaryOpType, typename OpType, typename Function>
TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&, typename SecondaryOpType::Params&&)> ConsumeStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, Function Func)
{
	return [Func](TOnlineAsyncOp<OpType>& InAsyncOp, typename SecondaryOpType::Params&& InParams) mutable
	{
		TPromise<void> Promise;
		auto Future = Promise.GetFuture();

		UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::ConsumeStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

		Func(MoveTempIfPossible(InParams))
		.Then([Promise = MoveTemp(Promise), Op = InAsyncOp.AsShared()](TFuture<TOnlineResult<SecondaryOpType>>&& Future) mutable
		{
			if (Future.Get().IsError())
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::ConsumeStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Future.Get().GetErrorValue().GetLogString());
				Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Future.Get().GetErrorValue())));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::ConsumeStepResult] Completed secondary operation %s."), SecondaryOpType::Name);
			}
			Promise.EmplaceValue();
		});

		return Future;
	};
}

template <typename SecondaryOpType, typename OpType, typename Function>
TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&)> ConsumeStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, Function Func, typename SecondaryOpType::Params&& InParams)
{
	return [Func, InParams = MoveTempIfPossible(InParams)](TOnlineAsyncOp<OpType>& InAsyncOp) mutable
	{
		TPromise<void> Promise;
		auto Future = Promise.GetFuture();

		UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::ConsumeStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

		Func(MoveTempIfPossible(InParams))
		.Then([Promise = MoveTemp(Promise), Op = InAsyncOp.AsShared()](TFuture<TOnlineResult<SecondaryOpType>>&& Future) mutable
		{
			if (Future.Get().IsError())
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::ConsumeStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Future.Get().GetErrorValue().GetLogString());
				Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Future.Get().GetErrorValue())));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::ConsumeStepResult] Completed secondary operation %s."), SecondaryOpType::Name);
			}
			Promise.EmplaceValue();
		});

		return Future;
	};
}

template <typename SecondaryOpType, typename OpType, typename Function>
TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&, typename SecondaryOpType::Params&&)> CaptureStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, const FString& ResultKey, Function Func)
{
	return [ResultKey, Func](TOnlineAsyncOp<OpType>& InAsyncOp, typename SecondaryOpType::Params&& InParams) mutable
	{
		TPromise<void> Promise;
		auto Future = Promise.GetFuture();

		UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::CaptureStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

		Func(MoveTempIfPossible(InParams))
		.Then([Promise = MoveTemp(Promise), Op = InAsyncOp.AsShared(), ResultKey](TFuture<TOnlineResult<SecondaryOpType>>&& Future) mutable
		{
			if (Future.Get().IsError())
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::CaptureStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Future.Get().GetErrorValue().GetLogString());
				Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Future.Get().GetErrorValue())));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::CaptureStepResult] Captured secondary operation %s as %s"), SecondaryOpType::Name, *ResultKey);
				Op->Data.Set(ResultKey, MoveTempIfPossible(Future.Get().GetOkValue()));
			}
			Promise.EmplaceValue();
		});

		return Future;
	};
}

template <typename SecondaryOpType, typename OpType, typename Function>
TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&)> CaptureStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, const FString& ResultKey, Function Func, typename SecondaryOpType::Params&& InParams)
{
	return [ResultKey, Func, InParams = MoveTempIfPossible(InParams)](TOnlineAsyncOp<OpType>& InAsyncOp) mutable
	{
		TPromise<void> Promise;
		auto Future = Promise.GetFuture();

		UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::CaptureStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

		Func(MoveTempIfPossible(InParams))
		.Then([Promise = MoveTemp(Promise), Op = InAsyncOp.AsShared(), ResultKey](TFuture<TOnlineResult<SecondaryOpType>>&& Future) mutable
		{
			if (Future.Get().IsError())
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::CaptureStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Future.Get().GetErrorValue().GetLogString());
				Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Future.Get().GetErrorValue())));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::CaptureStepResult] Captured secondary operation %s as %s"), SecondaryOpType::Name, *ResultKey);
				Op->Data.Set(ResultKey, MoveTempIfPossible(Future.Get().GetOkValue()));
			}
			Promise.EmplaceValue();
		});

		return Future;
	};
}

template <typename SecondaryOpType, typename OpType, typename Function>
TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&, typename SecondaryOpType::Params&&)> ConsumeOperationStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, Function Func)
{
	return [Func](TOnlineAsyncOp<OpType>& InAsyncOp, typename SecondaryOpType::Params&& InParams) mutable -> TFuture<void>
	{
		auto Promise = MakeShared<TPromise<void>>();
		auto Future = Promise->GetFuture();

		UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::ConsumeOperationStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

		Func(MoveTempIfPossible(InParams))
		.OnComplete([Promise, Op = InAsyncOp.AsShared()](const TOnlineResult<SecondaryOpType>& Result) mutable -> void
		{
			if (Result.IsError())
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::ConsumeOperationStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Result.GetErrorValue().GetLogString());
				Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Result.GetErrorValue())));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::ConsumeOperationStepResult] Completed secondary operation %s."), SecondaryOpType::Name);
			}
			Promise->EmplaceValue();
		});

		return Future;
	};
}

template <typename SecondaryOpType, typename OpType, typename Function>
TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&)> ConsumeOperationStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, Function Func, typename SecondaryOpType::Params&& InParams)
{
	return [Func, InParams = MoveTempIfPossible(InParams)](TOnlineAsyncOp<OpType>& InAsyncOp) mutable
	{
		auto Promise = MakeShared<TPromise<void>>();
		auto Future = Promise->GetFuture();

		UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::ConsumeOperationStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

		Func(MoveTempIfPossible(InParams))
		.OnComplete([Promise, Op = InAsyncOp.AsShared()](const TOnlineResult<SecondaryOpType>& Result) mutable
		{
			if (Result.IsError())
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::ConsumeOperationStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Result.GetErrorValue().GetLogString());
				Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Result.GetErrorValue())));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::ConsumeOperationStepResult] Completed secondary operation %s."), SecondaryOpType::Name);
			}
			Promise->EmplaceValue();
		});

		return Future;
	};
}

template <typename SecondaryOpType, typename OpType, typename Function>
TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&, typename SecondaryOpType::Params&&)> CaptureOperationStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, const FString& ResultKey, Function Func)
{
	return [ResultKey, Func](TOnlineAsyncOp<OpType>& InAsyncOp, typename SecondaryOpType::Params&& InParams) mutable -> TFuture<void>
	{
		auto Promise = MakeShared<TPromise<void>>();
		auto Future = Promise->GetFuture();

		UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::CaptureOperationStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

		Func(MoveTempIfPossible(InParams))
		.OnComplete([Promise, Op = InAsyncOp.AsShared(), ResultKey](const TOnlineResult<SecondaryOpType>& Result) mutable -> void
		{
			if (Result.IsError())
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::CaptureOperationStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Result.GetErrorValue().GetLogString());
				Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Result.GetErrorValue())));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::CaptureOperationStepResult] Captured secondary operation %s as %s"), SecondaryOpType::Name, *ResultKey);
				Op->Data.Set(ResultKey, Result.GetOkValue());
			}
			Promise->EmplaceValue();
		});

		return Future;
	};
}

template <typename SecondaryOpType, typename OpType, typename Function>
TFunction<TFuture<void>(TOnlineAsyncOp<OpType>&)> CaptureOperationStepResult(TOnlineAsyncOp<OpType>& InAsyncOp, const FString& ResultKey, Function Func, typename SecondaryOpType::Params&& InParams)
{
	return [ResultKey, Func, InParams = MoveTempIfPossible(InParams)](TOnlineAsyncOp<OpType>& InAsyncOp) mutable
	{
		auto Promise = MakeShared<TPromise<void>>();
		auto Future = Promise->GetFuture();

		UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::CaptureOperationStepResult] Starting secondary operation %s."), SecondaryOpType::Name);

		Func(MoveTempIfPossible(InParams))
		.OnComplete([Promise, Op = InAsyncOp.AsShared(), ResultKey](const TOnlineResult<SecondaryOpType>& Result) mutable
		{
			if (Result.IsError())
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::CaptureOperationStepResult] Failed secondary operation %s. Error: %s"), SecondaryOpType::Name, *Result.GetErrorValue().GetLogString());
				Op->SetError(Errors::RequestFailure(MoveTempIfPossible(Result.GetErrorValue())));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[LobbiesFunctionalTest::CaptureOperationStepResult] Captured secondary operation %s as %s"), SecondaryOpType::Name, *ResultKey);
				Op->Data.Set(ResultKey, Result.GetOkValue());
			}
			Promise->EmplaceValue();
		});

		return Future;
	};
}

// todo: TFuture<void> does not work as expected with "Then".
TFuture<int> AwaitSleepFor(double Seconds)
{
	TSharedRef<TPromise<int>> Promise = MakeShared<TPromise<int>>();
	TFuture<int> Future = Promise->GetFuture();

	FTSTicker::GetCoreTicker().AddTicker(
	TEXT("LobbiesFunctionalTest::AwaitSleepFor"),
	Seconds,
	[Promise](float)
	{
		Promise->EmplaceValue();
		return false;
	});

	return Future;
}

// todo: TFuture<void> does not work as expected with "Then".
TFuture<int> AwaitNextGameTick()
{
	return AwaitSleepFor(0.f);
}

TFuture<TDefaultErrorResultInternal<FOnlineLobbyIdHandle>> AwaitInvitation(
	FLobbyEvents& LobbyEvents,
	FOnlineAccountIdHandle TargetAccountId,
	FOnlineLobbyIdHandle LobbyId,
	float TimeoutSeconds)
{
	// todo: Make this nicer - current implementation has issue on shutdown.
	struct AwaitInvitationState
	{
		TPromise<TDefaultErrorResultInternal<FOnlineLobbyIdHandle>> Promise;
		FOnlineEventDelegateHandle OnLobbyInvitationAddedHandle;
		FTSTicker::FDelegateHandle OnAwaitExpiredHandle;
		bool bSignaled = false;
	};

	TSharedRef<AwaitInvitationState> AwaitState = MakeShared<AwaitInvitationState>();

	AwaitState->OnLobbyInvitationAddedHandle = LobbyEvents.OnLobbyInvitationAdded.Add(
	[TargetAccountId, LobbyId, AwaitState](const FLobbyInvitationAdded& Notification)
	{
		FOnlineEventDelegateHandle DelegateHandle = MoveTemp(AwaitState->OnLobbyInvitationAddedHandle);

		if (!AwaitState->bSignaled)
		{
			if (Notification.Lobby->LobbyId == LobbyId && Notification.LocalUserId == TargetAccountId)
			{
				FTSTicker::GetCoreTicker().RemoveTicker(AwaitState->OnAwaitExpiredHandle);
				AwaitState->bSignaled = true;

				AwaitNextGameTick()
				.Then([AwaitState, LobbyId](TFuture<int>&&)
				{
					AwaitState->Promise.EmplaceValue(LobbyId);
				});
			}
		}
	});

	AwaitState->OnAwaitExpiredHandle = FTSTicker::GetCoreTicker().AddTicker(
	TEXT("LobbiesFunctionalTest::AwaitInvitation"),
	TimeoutSeconds,
	[AwaitState](float)
	{
		if (!AwaitState->bSignaled)
		{
			// Todo: Errors.
			AwaitState->bSignaled = true;
			AwaitState->OnLobbyInvitationAddedHandle.Unbind();
			AwaitState->Promise.EmplaceValue(Errors::NotImplemented());
			FTSTicker::GetCoreTicker().RemoveTicker(AwaitState->OnAwaitExpiredHandle);
		}

		return false;
	});

	return AwaitState->Promise.GetFuture();
}

struct FFunctionalTestLoginUser
{
	static constexpr TCHAR Name[] = TEXT("FunctionalTestLoginUser");

	struct Params
	{
		IAuth* AuthInterface = nullptr;
		FPlatformUserId PlatformUserId;
		FString Type;
		FString Id;
		FString Token;
	};

	struct Result
	{
		TSharedPtr<FAccountInfo> AccountInfo;
	};
};

struct FFunctionalTestLogoutUser
{
	static constexpr TCHAR Name[] = TEXT("FunctionalTestLogoutUser");

	struct Params
	{
		IAuth* AuthInterface = nullptr;
		FPlatformUserId PlatformUserId;
	};

	struct Result
	{
	};
};

struct FFunctionalTestLogoutAllUsers
{
	static constexpr TCHAR Name[] = TEXT("FunctionalTestLogoutAllUsers");

	struct Params
	{
		IAuth* AuthInterface = nullptr;
	};

	struct Result
	{
	};
};

TFuture<TOnlineResult<FFunctionalTestLoginUser>> FunctionalTestLoginUser(FFunctionalTestLoginUser::Params&& Params)
{
	if (!Params.AuthInterface)
	{
		return MakeFulfilledPromise<TOnlineResult<FFunctionalTestLoginUser>>(Errors::MissingInterface()).GetFuture();
	}

	TSharedRef<TPromise<TOnlineResult<FFunctionalTestLoginUser>>> Promise = MakeShared<TPromise<TOnlineResult<FFunctionalTestLoginUser>>>();
	TFuture<TOnlineResult<FFunctionalTestLoginUser>> Future = Promise->GetFuture();

	FAuthLogin::Params LoginParams;
	LoginParams.PlatformUserId = Params.PlatformUserId;
	LoginParams.CredentialsType = Params.Type;
	LoginParams.CredentialsId = Params.Id;
	LoginParams.CredentialsToken.Set<FString>(Params.Token);

	Params.AuthInterface->Login(MoveTemp(LoginParams))
	.OnComplete([Promise, PlatformUserId = Params.PlatformUserId](const TOnlineResult<FAuthLogin>& LoginResult)
	{
		if (LoginResult.IsError())
		{
			Promise->EmplaceValue(Errors::RequestFailure());
		}
		else
		{
			Promise->EmplaceValue(FFunctionalTestLoginUser::Result{LoginResult.GetOkValue().AccountInfo});
		}
	});

	return Future;
}

TFuture<TOnlineResult<FFunctionalTestLogoutUser>> FunctionalTestLogoutUser(FFunctionalTestLogoutUser::Params&& Params)
{
	if (!Params.AuthInterface)
	{
		return MakeFulfilledPromise<TOnlineResult<FFunctionalTestLogoutUser>>(Errors::MissingInterface()).GetFuture();
	}

	FAuthGetAccountByPlatformUserId::Params GetAccountParams;
	GetAccountParams.PlatformUserId = Params.PlatformUserId;
	TOnlineResult<FAuthGetAccountByPlatformUserId> Result = Params.AuthInterface->GetAccountByPlatformUserId(MoveTemp(GetAccountParams));
	if (Result.IsError())
	{
		// Ignore errors for now. This function should ignore logged out users.
		if (Result.GetErrorValue() == Errors::Unknown())
		{
			return MakeFulfilledPromise<TOnlineResult<FFunctionalTestLogoutUser>>(FFunctionalTestLogoutUser::Result{}).GetFuture();
		}
		else
		{
			return MakeFulfilledPromise<TOnlineResult<FFunctionalTestLogoutUser>>(Errors::Unknown(MoveTemp(Result.GetErrorValue()))).GetFuture();
		}
	}

	TSharedRef<FAccountInfo> AccountInfo = Result.GetOkValue().AccountInfo;
	if (AccountInfo->LoginStatus == ELoginStatus::NotLoggedIn)
	{
		return MakeFulfilledPromise<TOnlineResult<FFunctionalTestLogoutUser>>(Errors::InvalidUser()).GetFuture();
	}

	TSharedRef<TPromise<TOnlineResult<FFunctionalTestLogoutUser>>> Promise = MakeShared<TPromise<TOnlineResult<FFunctionalTestLogoutUser>>>();
	TFuture<TOnlineResult<FFunctionalTestLogoutUser>> Future = Promise->GetFuture();

	FAuthLogout::Params LogoutParams;
	LogoutParams.LocalUserId = AccountInfo->UserId;

	Params.AuthInterface->Logout(MoveTemp(LogoutParams))
	.OnComplete([Promise, PlatformUserId = Params.PlatformUserId](const TOnlineResult<FAuthLogout>& LogoutResult)
	{
		if (LogoutResult.IsError())
		{
			Promise->EmplaceValue(Errors::RequestFailure());
		}
		else
		{
			Promise->EmplaceValue(FFunctionalTestLogoutUser::Result{});
		}
	});

	return Future;
}

TFuture<TOnlineResult<FFunctionalTestLogoutAllUsers>> FunctionalTestLogoutAllUsers(FFunctionalTestLogoutAllUsers::Params&& Params)
{
	TArray<TFuture<TOnlineResult<FFunctionalTestLogoutUser>>> LogoutFutures;
	for (int32 index = 0; index < MAX_LOCAL_PLAYERS; ++index)
	{
		LogoutFutures.Emplace(FunctionalTestLogoutUser(FFunctionalTestLogoutUser::Params{Params.AuthInterface, FPlatformMisc::GetPlatformUserForUserIndex(index)}));
	}

	TSharedRef<TPromise<TOnlineResult<FFunctionalTestLogoutAllUsers>>> Promise = MakeShared<TPromise<TOnlineResult<FFunctionalTestLogoutAllUsers>>>();
	TFuture<TOnlineResult<FFunctionalTestLogoutAllUsers>> Future = Promise->GetFuture();

	WhenAll(MoveTempIfPossible(LogoutFutures))
	.Then([Promise = MoveTemp(Promise)](TFuture<TArray<TOnlineResult<FFunctionalTestLogoutUser>>>&& Results)
	{
		bool HasAnyError = false;
		for (TOnlineResult<FFunctionalTestLogoutUser>& Result : Results.Get())
		{
			HasAnyError |= Result.IsError();
		}

		if (HasAnyError)
		{
			Promise->EmplaceValue(Errors::RequestFailure());
		}
		else
		{
			Promise->EmplaceValue(FFunctionalTestLogoutAllUsers::Result{});
		}
	});

	return Future;
}

template <typename OperationType>
class FBindOperation
{
private:
	class FBindImplBase
	{
	public:
		virtual ~FBindImplBase() = default;
		virtual TOnlineAsyncOpHandle<OperationType> operator()(typename OperationType::Params&& Params) const = 0;
	};

	template <typename ObjectType, typename Callable>
	class FBindImpl : public FBindImplBase
	{
	public:
		FBindImpl(ObjectType* Object, Callable Func)
			: Object(Object)
			, Func(Func)
		{
		}

		virtual ~FBindImpl() = default;

		virtual TOnlineAsyncOpHandle<OperationType> operator()(typename OperationType::Params&& Params) const
		{
			return (Object->*Func)(MoveTempIfPossible(Params));
		}

	private:
		ObjectType* Object;
		Callable Func;
	};

public:
	template <typename ObjectType, typename Callable>
	FBindOperation(ObjectType* Object, Callable Func)
		: Impl(MakeShared<FBindImpl<ObjectType, Callable>>(Object, MoveTempIfPossible(Func)))
	{
	}

	TOnlineAsyncOpHandle<OperationType> operator()(typename OperationType::Params&& Params) const
	{
		check(Impl);
		return (*Impl)(MoveTempIfPossible(Params));
	}

private:
	TSharedPtr<FBindImplBase> Impl;
};

} // Private

template <typename DataType, typename OpType>
const DataType& GetOpDataChecked(const TOnlineAsyncOp<OpType>& Op, const FString& Key)
{
	const DataType* Data = Op.Data.template Get<DataType>(Key);
	check(Data);
	return *Data;
}

struct FFunctionalTestConfig
{
	FString TestAccount1Type;
	FString TestAccount1Id;
	FString TestAccount1Token;

	FString TestAccount2Type;
	FString TestAccount2Id;
	FString TestAccount2Token;

	float InvitationWaitSeconds = 10.f;
	float FindMatchReplicationDelay = 5.f;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FFunctionalTestConfig)
	ONLINE_STRUCT_FIELD(FFunctionalTestConfig, TestAccount1Type),
	ONLINE_STRUCT_FIELD(FFunctionalTestConfig, TestAccount1Id),
	ONLINE_STRUCT_FIELD(FFunctionalTestConfig, TestAccount1Token),

	ONLINE_STRUCT_FIELD(FFunctionalTestConfig, TestAccount2Type),
	ONLINE_STRUCT_FIELD(FFunctionalTestConfig, TestAccount2Id),
	ONLINE_STRUCT_FIELD(FFunctionalTestConfig, TestAccount2Token)
END_ONLINE_STRUCT_META()

/* Meta */ }

TOnlineAsyncOpHandle<FFunctionalTestLobbies> RunLobbyFunctionalTest(IAuth& AuthInterface, FLobbiesCommon& LobbiesCommon, FLobbyEvents& LobbyEvents)
{
	static const FString ConfigName = TEXT("FunctionalTest");
	static const FString User1KeyName = TEXT("User1");
	static const FString User2KeyName = TEXT("User2");
	static const FString CreateLobbyKeyName = TEXT("CreateLobby");
	static const FString FindLobbyKeyName = TEXT("FindLobby");
	static const FString LobbyEventCaptureKeyName = TEXT("LobbyEventCapture");
	static const FString ConfigNameKeyName = TEXT("Config");
	static const FString SearchKeyName = TEXT("SearchKey");

	struct FSearchParams
	{
		int64 LobbyCreateTime = 0;
	};

	TSharedRef<FFunctionalTestConfig> TestConfig = MakeShared<FFunctionalTestConfig>();
	LobbiesCommon.LoadConfig(*TestConfig, ConfigName);

	TOnlineAsyncOpRef<FFunctionalTestLobbies> Op = LobbiesCommon.GetOp<FFunctionalTestLobbies>(FFunctionalTestLobbies::Params{});

	// Set up event capturing.
	Op->Data.Set(LobbyEventCaptureKeyName, MakeShared<FLobbyEventCapture>(LobbyEvents));

	Op->Then(Private::ConsumeStepResult<Private::FFunctionalTestLogoutAllUsers>(*Op, &Private::FunctionalTestLogoutAllUsers, Private::FFunctionalTestLogoutAllUsers::Params{&AuthInterface}))
	.Then(Private::CaptureStepResult<Private::FFunctionalTestLoginUser>(*Op, User1KeyName, &Private::FunctionalTestLoginUser, Private::FFunctionalTestLoginUser::Params{&AuthInterface, FPlatformMisc::GetPlatformUserForUserIndex(0), TestConfig->TestAccount1Type, TestConfig->TestAccount1Id, TestConfig->TestAccount1Token}))
	.Then(Private::CaptureStepResult<Private::FFunctionalTestLoginUser>(*Op, User2KeyName, &Private::FunctionalTestLoginUser, Private::FFunctionalTestLoginUser::Params{&AuthInterface, FPlatformMisc::GetPlatformUserForUserIndex(1), TestConfig->TestAccount2Type, TestConfig->TestAccount2Id, TestConfig->TestAccount2Token}))

	//----------------------------------------------------------------------------------------------
	// Test 1:
	//    Step 1: Create a lobby with primary user.
	//    Step 2: Modify lobby attribute.
	//    Step 3: Modify lobby member attribute.
	//    Step 4: Leave lobby with primary user.
	//----------------------------------------------------------------------------------------------

	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const Private::FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);

		// Create a lobby
		FCreateLobby::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LocalName = TEXT("test");
		Params.SchemaName = TEXT("test");
		Params.MaxMembers = 2;
		Params.JoinPolicy = ELobbyJoinPolicy::InvitationOnly;
		Params.Attributes.Add(TEXT("test_attribute_key"), TEXT("test_attribute_value"));
		Params.LocalUsers.Emplace(FJoinLobbyLocalUserData{User1Info.AccountInfo->UserId, {{TEXT("test_attribute_key"), TEXT("test_attribute_value")}}});
		return Params;
	})
	.Then(Private::CaptureOperationStepResult<FCreateLobby>(*Op, CreateLobbyKeyName, Private::FBindOperation<FCreateLobby>(&LobbiesCommon, &FLobbiesCommon::CreateLobby)))
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		const Private::FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);
		const FCreateLobby::Result& CreateResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		// Check both lobby joined events were received and in the correct order.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 2
			|| EventCapture->LobbyJoined.Num() != 1 || EventCapture->LobbyMemberJoined.Num() != 1
			|| EventCapture->LobbyJoined[0].GlobalIndex > EventCapture->LobbyMemberJoined[0].GlobalIndex)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}

		// Check lobby attributes are set to the expected values.
		if (!CreateResult.Lobby->Attributes.OrderIndependentCompareEqual(
			TMap<FLobbyAttributeId, FLobbyVariant>{{TEXT("test_attribute_key"), TEXT("test_attribute_value")}}))
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}

		const TSharedRef<const FLobbyMember>* MemberData = CreateResult.Lobby->Members.Find(User1Info.AccountInfo->UserId);
		if (CreateResult.Lobby->Members.Num() == 1 && MemberData != nullptr)
		{
			if (!(**MemberData).Attributes.OrderIndependentCompareEqual(
				TMap<FLobbyAttributeId, FLobbyVariant>{{TEXT("test_attribute_key"), TEXT("test_attribute_value")}}))
			{
				InAsyncOp.SetError(Errors::Cancelled());
			}
		}
		else
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const Private::FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FModifyLobbyAttributes::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		Params.MutatedAttributes = {{TEXT("test_attribute_key"), TEXT("mutated_test_attribute_value")}};
		return Params;
	})
	.Then(Private::ConsumeOperationStepResult<FModifyLobbyAttributes>(*Op, Private::FBindOperation<FModifyLobbyAttributes>(&LobbiesCommon, &FLobbiesCommon::ModifyLobbyAttributes)))
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check that modification event was received.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->LobbyAttributesChanged.Num() == 1)
		{
			const FLobbyAttributesChanged& Notification = EventCapture->LobbyAttributesChanged[0].Notification;
			if (!Notification.ChangedAttributes.Difference(TSet<FLobbyAttributeId>{TEXT("test_attribute_key")}).IsEmpty() ||
				!Notification.Lobby->Attributes.OrderIndependentCompareEqual(TMap<FLobbyAttributeId, FLobbyVariant>{{TEXT("test_attribute_key"), TEXT("mutated_test_attribute_value")}}))
			{
				InAsyncOp.SetError(Errors::Cancelled());
			}
		}
		else
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const Private::FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FModifyLobbyMemberAttributes::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		Params.MutatedAttributes = {{TEXT("test_attribute_key"), TEXT("mutated_test_attribute_value")}};
		return Params;
	})
	.Then(Private::ConsumeOperationStepResult<FModifyLobbyMemberAttributes>(*Op, Private::FBindOperation<FModifyLobbyMemberAttributes>(&LobbiesCommon, &FLobbiesCommon::ModifyLobbyMemberAttributes)))
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		const Private::FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);

		// Check that modification event was received.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->LobbyMemberAttributesChanged.Num() == 1)
		{
			const FLobbyMemberAttributesChanged& Notification = EventCapture->LobbyMemberAttributesChanged[0].Notification;
			if (const TSharedRef<const FLobbyMember>* MemberData = Notification.Lobby->Members.Find(User1Info.AccountInfo->UserId))
			{
				if (!Notification.ChangedAttributes.Difference(TSet<FLobbyAttributeId>{TEXT("test_attribute_key")}).IsEmpty() ||
					!(**MemberData).Attributes.OrderIndependentCompareEqual(TMap<FLobbyAttributeId, FLobbyVariant>{{TEXT("test_attribute_key"), TEXT("mutated_test_attribute_value")}}))
				{
					InAsyncOp.SetError(Errors::Cancelled());
				}
			}
			else
			{
				InAsyncOp.SetError(Errors::Cancelled());
			}
		}
		else
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const Private::FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FLeaveLobby::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		return Params;
	})
	.Then(Private::ConsumeOperationStepResult<FLeaveLobby>(*Op, Private::FBindOperation<FLeaveLobby>(&LobbiesCommon, &FLobbiesCommon::LeaveLobby)))
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check for expected events.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 2
			|| EventCapture->LobbyLeft.Num() != 1 || EventCapture->LobbyMemberLeft.Num() != 1
			|| EventCapture->LobbyLeft[0].GlobalIndex < EventCapture->LobbyMemberLeft[0].GlobalIndex)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})

	//----------------------------------------------------------------------------------------------
	// Test 2:
	//    Step 1: Create a lobby with primary user.
	//    Step 2: Primary user invites secondary user.
	//    Step 3: Join lobby with secondary user.
	//    Step 4: Leave lobby with secondary user.
	//    Step 5: Leave lobby with primary user.
	//----------------------------------------------------------------------------------------------

	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const Private::FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);

		FCreateLobby::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LocalName = TEXT("test");
		Params.SchemaName = TEXT("test");
		Params.MaxMembers = 2;
		Params.JoinPolicy = ELobbyJoinPolicy::InvitationOnly;
		//Params.Attributes;
		Params.LocalUsers.Emplace(FJoinLobbyLocalUserData{User1Info.AccountInfo->UserId, {}});
		return Params;
	})
	.Then(Private::CaptureOperationStepResult<FCreateLobby>(*Op, CreateLobbyKeyName, Private::FBindOperation<FCreateLobby>(&LobbiesCommon, &FLobbiesCommon::CreateLobby)))
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check both lobby joined events were received and in the correct order.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 2
			|| EventCapture->LobbyJoined.Num() != 1 || EventCapture->LobbyMemberJoined.Num() != 1
			|| EventCapture->LobbyJoined[0].GlobalIndex > EventCapture->LobbyMemberJoined[0].GlobalIndex)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const Private::FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);
		const Private::FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FInviteLobbyMember::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		Params.TargetUserId = User2Info.AccountInfo->UserId;
		return Params;
	})
	.Then(Private::ConsumeOperationStepResult<FInviteLobbyMember>(*Op, Private::FBindOperation<FInviteLobbyMember>(&LobbiesCommon, &FLobbiesCommon::InviteLobbyMember)))
	.Then([TestConfig, LobbyEventsPtr = &LobbyEvents](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		const Private::FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);
		return Private::AwaitInvitation(*LobbyEventsPtr, User2Info.AccountInfo->UserId, CreateLobbyResult.Lobby->LobbyId, TestConfig->InvitationWaitSeconds);
	})
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp, TDefaultErrorResultInternal<FOnlineLobbyIdHandle>&& Result)
	{
		if (Result.IsError())
		{
			InAsyncOp.SetError(Errors::Cancelled(Result.GetErrorValue()));
		}
	})
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const Private::FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FJoinLobby::Params Params;
		Params.LocalUserId = User2Info.AccountInfo->UserId;
		Params.LocalName = TEXT("test");
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		Params.LocalUsers.Emplace(FJoinLobbyLocalUserData{User2Info.AccountInfo->UserId, {}});
		return Params;
	})
	.Then(Private::ConsumeOperationStepResult<FJoinLobby>(*Op, Private::FBindOperation<FJoinLobby>(&LobbiesCommon, &FLobbiesCommon::JoinLobby)))
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check both lobby joined events were received and in the correct order.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 1 || EventCapture->LobbyMemberJoined.Num() != 1)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const Private::FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FLeaveLobby::Params Params;
		Params.LocalUserId = User2Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		return Params;
	})
	.Then(Private::ConsumeOperationStepResult<FLeaveLobby>(*Op, Private::FBindOperation<FLeaveLobby>(&LobbiesCommon, &FLobbiesCommon::LeaveLobby)))
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check for expected events.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 1 || EventCapture->LobbyMemberLeft.Num() != 1)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const Private::FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FLeaveLobby::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		return Params;
	})
	.Then(Private::ConsumeOperationStepResult<FLeaveLobby>(*Op, Private::FBindOperation<FLeaveLobby>(&LobbiesCommon, &FLobbiesCommon::LeaveLobby)))
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check for expected events.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 2
			|| EventCapture->LobbyLeft.Num() != 1 || EventCapture->LobbyMemberLeft.Num() != 1
			|| EventCapture->LobbyLeft[0].GlobalIndex < EventCapture->LobbyMemberLeft[0].GlobalIndex)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})

	//----------------------------------------------------------------------------------------------
	// Test 3: Simple search
	//    Step 1: Create a lobby with primary user.
	//    Step 2: Secondary user searches for lobbies.
	//    Step 3: Join lobby with secondary user.
	//    Step 4: Leave lobby with secondary user.
	//    Step 5: Leave lobby with primary user.
	//----------------------------------------------------------------------------------------------

	// todo: additional functional tests for search.
	// find by lobby id, find by user, find by attributes. Search types are mutually exclusive and
	// should return invalid args if multiple search types are passed for a search.

	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const Private::FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);

		TSharedRef<FSearchParams> SearchParams = MakeShared<FSearchParams>();
		SearchParams->LobbyCreateTime = static_cast<int64>(FPlatformTime::Seconds());
		InAsyncOp.Data.Set(SearchKeyName, TSharedRef<FSearchParams>(SearchParams));

		FCreateLobby::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LocalName = TEXT("test");
		Params.SchemaName = TEXT("test");
		Params.MaxMembers = 2;
		Params.JoinPolicy = ELobbyJoinPolicy::PublicAdvertised;
		Params.Attributes = {{ TEXT("LobbyCreateTime"), SearchParams->LobbyCreateTime }};
		Params.LocalUsers.Emplace(FJoinLobbyLocalUserData{User1Info.AccountInfo->UserId, {}});
		return Params;
	})
	.Then(Private::CaptureOperationStepResult<FCreateLobby>(*Op, CreateLobbyKeyName, Private::FBindOperation<FCreateLobby>(&LobbiesCommon, &FLobbiesCommon::CreateLobby)))
	.Then([TestConfig](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check both lobby joined events were received and in the correct order.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 2
			|| EventCapture->LobbyJoined.Num() != 1 || EventCapture->LobbyMemberJoined.Num() != 1
			|| EventCapture->LobbyJoined[0].GlobalIndex > EventCapture->LobbyMemberJoined[0].GlobalIndex)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}

		// Wait some time so lobby creation can propagate before searching for it on another client.
		return Private::AwaitSleepFor(TestConfig->FindMatchReplicationDelay);
	})
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp, int)
	{
	})
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		const Private::FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);
		const TSharedRef<FSearchParams>& SearchParams = GetOpDataChecked<TSharedRef<FSearchParams>>(InAsyncOp, SearchKeyName);

		// Search for lobby from create. Searching by attribute will also limit results by bucket id.
		FFindLobbies::Params Params;
		Params.LocalUserId = User2Info.AccountInfo->UserId;
		Params.Filters = {{TEXT("LobbyCreateTime"), ELobbyComparisonOp::Equals, SearchParams->LobbyCreateTime}};
		return Params;
	})
	.Then(Private::CaptureOperationStepResult<FFindLobbies>(*Op, FindLobbyKeyName, Private::FBindOperation<FFindLobbies>(&LobbiesCommon, &FLobbiesCommon::FindLobbies)))
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const Private::FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);
		const FFindLobbies::Result& FindResults = GetOpDataChecked<FFindLobbies::Result>(InAsyncOp, FindLobbyKeyName);

		if (FindResults.Lobbies.Num() != 1)
		{
			InAsyncOp.SetError(Errors::Cancelled());
			return FJoinLobby::Params{};
		}

		FJoinLobby::Params Params;
		Params.LocalUserId = User2Info.AccountInfo->UserId;
		Params.LocalName = TEXT("test");
		Params.LobbyId = FindResults.Lobbies[0]->LobbyId;
		Params.LocalUsers.Emplace(FJoinLobbyLocalUserData{User2Info.AccountInfo->UserId, {}});
		return Params;
	})
	.Then(Private::ConsumeOperationStepResult<FJoinLobby>(*Op, Private::FBindOperation<FJoinLobby>(&LobbiesCommon, &FLobbiesCommon::JoinLobby)))
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check both lobby joined events were received and in the correct order.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 1 || EventCapture->LobbyMemberJoined.Num() != 1)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const Private::FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FLeaveLobby::Params Params;
		Params.LocalUserId = User2Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		return Params;
	})
	.Then(Private::ConsumeOperationStepResult<FLeaveLobby>(*Op, Private::FBindOperation<FLeaveLobby>(&LobbiesCommon, &FLobbiesCommon::LeaveLobby)))
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check for expected events.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 1|| EventCapture->LobbyMemberLeft.Num() != 1)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const Private::FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<Private::FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FLeaveLobby::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		return Params;
	})
	.Then(Private::ConsumeOperationStepResult<FLeaveLobby>(*Op, Private::FBindOperation<FLeaveLobby>(&LobbiesCommon, &FLobbiesCommon::LeaveLobby)))
	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check for expected events.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 2
			|| EventCapture->LobbyLeft.Num() != 1 || EventCapture->LobbyMemberLeft.Num() != 1
			|| EventCapture->LobbyLeft[0].GlobalIndex < EventCapture->LobbyMemberLeft[0].GlobalIndex)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})

	//----------------------------------------------------------------------------------------------
	// Complete
	//----------------------------------------------------------------------------------------------

	.Then([](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		InAsyncOp.SetResult(FFunctionalTestLobbies::Result{});
	})
	.Enqueue(LobbiesCommon.GetServices().GetParallelQueue());

	return Op->GetHandle();
}

#endif // LOBBIES_FUNCTIONAL_TEST_ENABLED

/* UE::Online */ }
