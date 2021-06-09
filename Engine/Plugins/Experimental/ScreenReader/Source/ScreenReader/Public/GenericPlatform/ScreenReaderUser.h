// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Announcement/ScreenReaderAnnouncement.h"
#include "GenericPlatform/ScreenReaderReply.h"

class IAccessibleWidget;
class FScreenReaderAnnouncementChannel;

/**
* A user of the screen reader. Corresponds to a hardware device that users use.
* This class is a facade that acts as a one stop shop for all screen reading services on a per user basis.
* Multiple screen reader users can exist simultaneoulsy to facilitate local multiplayer and users can opt in or out 
* of receiving screen reader feedback by registering and unregistering with the screen reader respectively.
* A screen user is inactive by default when it is first registered with teh screen reader. Users must explicitly activate the screen reader user after successful registration with the screen reader to use its services.
* Responsibilities of the class are:
* 1. Text to speech (TTS) requests - Users can request an announcement to be spoken via text to speech to  screen reader user
* 2. Accessible focus handling - A screen reader user holds information about the accessible widget it is currently focused on
* @see FScreenReaderBase, FScreenReaderAnnouncement
*/
class SCREENREADER_API FScreenReaderUser
{
public:
	explicit FScreenReaderUser(int32 InUserId);
	~FScreenReaderUser();
	/**
	* Requests an announcement to be spoken to the screen reader user. This is the main mechanism to provide text to speech auditory feedback
	* to users.
	*Calling this function does not guarantee that the announcement will be spoken via text to speech and be 
	* heard by a user.
	* All announcements spoken via text to speech will be asynchronous and iwll not block the game thread. 
	* If the screen reader user is active and no announcements are currently spoken, the announcement will be spoken immediately.
	* If another announcement is currently being spoken, the passed in announcement could be queued or interrupt the currently spoken announcement.
	* Examples:
	* TSharedRef<FScreenReaderUser> MyUser = ScreenReader->GetUser(MyScreenReaderUserId);
	* // All announcements to be spoken should be localized to provide language support for the text to speech system.
	* static const FText MyText = LOCTEXT("ExampleUserFeedback", "Feedback to user.");
	* // This makes the announcement interrupt any currently spoken announcement with lower priority and is played immediately. The announcement will be uninterruptable and guarantees the user will hear the announcement.
	* // Use this sparingly for only critical information that the user needs to hear such as registration to the screen reader or important in game events.
	* MyUser->RequestSpeak(FScreenReaderAnnouncement(MyText.ToString(), FScreenReaderAnnouncementInfo::Important()));
	* // This makes the announcement a feedback for a user action or event. It is interruptable.
	* // Use this to alert users to an action they performed 
	* MyUser->RequestSpeak(FScreenReaderAnnouncement(MyText.ToString(), FScreenReaderAnnouncementInfo::UserFeedback()));
	* @param InAnnouncement - The announcement requested to be spoken to the screen reader user
	* @return FScreenReaderReply::Handled() if the request was successfully processed. Else return FScreenReader::Unhandled()
	* @see FScreenReaderAnnouncement, FScreenReaderAnnouncementInfo
	*/
	FScreenReaderReply RequestSpeak(FScreenReaderAnnouncement InAnnouncement);
	/**
	* Immediately stops speaking any currently spoken announcement.
	* Does nothing if there is no announcement currently being spoken. 
	* @return FScreenReader::Handled() if the request to stop speaking is successfully processed. Else returns FScreenReaderReply::Unhandled()
	*/
	FScreenReaderReply StopSpeaking();
	/** Returns true if an announcement is currently being spoken. Else returns false.*/
	bool IsSpeaking() const;
	/**
	* Requests the information about an accessibility widget to be read out to a user.
	* The same guarantees about the announcement being spoken in RequestSpeak() apply for this function.
	* @param InWidget The widget to ahve its contents spoken to a user.
	* @return FScreenReaderReply::Handled() if the widget's information is successfully spoken. Else returns FScreenReaderReply::Unhandled();
	*/
	FScreenReaderReply RequestSpeakWidget(const TSharedRef<IAccessibleWidget>& InWidget);

	/**
	* Activates the screen reader and allow users to use its services such as requesting announcements to be spoken via text to speech.
	* When screen reader users are firstr registered with a screen reader, they are inactive by default. Users must explicitly activate the screen reader user.
	*/
	void Activate();
	/** 
	* Deactivates the screen reader and disables all announcement and text to speech services
	* making them do nothing.
	*/

	void Deactivate();
	/** Returns true if the screen reader user is active. Else returns false.*/
	bool IsActive() const { return bActive; }

	/** Retursn the user Id associated with this screen reader user. */
	int32 GetUserId() const { return UserId; }
	/** Returns the accessible widget the screen reader user is currently focused on. */
	TSharedPtr<IAccessibleWidget> GetAccessibleFocus() const { return AccessibleFocus.Pin(); }
	/** Sets the accessible the screenr eader user is currently focused on */
	void SetAccessibleFocus(const TSharedRef<IAccessibleWidget>& InAccessibleFocus) { AccessibleFocus = InAccessibleFocus; }
private:
	int32 UserId;
	/** Responsible for handling all incoming announcement requests and speaking them via text to speech if possible */
	TUniquePtr<FScreenReaderAnnouncementChannel> AnnouncementChannel;
	TWeakPtr<IAccessibleWidget> AccessibleFocus;
	bool bActive;
};