package com.epicgames.ue4.notifications;

import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.app.NotificationCompat;
import android.text.TextUtils;

import com.epicgames.ue4.GameActivity;
import com.epicgames.ue4.GameApplication;
import com.epicgames.ue4.LocalNotificationReceiver;
import com.epicgames.ue4.Logger;
import com.google.firebase.messaging.FirebaseMessagingService;
import com.google.firebase.messaging.RemoteMessage;

import org.json.JSONException;
import org.json.JSONObject;

import java.util.Map;
import java.util.Objects;
import java.util.Random;

import static com.epicgames.ue4.GameActivity.LocalNotificationGetID;
import static com.epicgames.ue4.LocalNotificationReceiver.KEY_LOCAL_NOTIFICATION_ACTION;
import static com.epicgames.ue4.LocalNotificationReceiver.KEY_LOCAL_NOTIFICATION_BODY;
import static com.epicgames.ue4.LocalNotificationReceiver.KEY_LOCAL_NOTIFICATION_ID;
import static com.epicgames.ue4.LocalNotificationReceiver.KEY_LOCAL_NOTIFICATION_TITLE;

public class EpicFirebaseMessagingService extends FirebaseMessagingService {

	private static final Logger Log = new Logger("UE4-" + EpicFirebaseMessagingService.class.getSimpleName());

	private static final String ATTR_TYPE = "type";
	private static final String PAYLOAD_P_KEY = "p";

	private static final String FIREBASE_ICON_NAME = "ic_notification";
	private static final String FIREBASE_ICON_TYPE = "mipmap";

	private static final String COMPONENT = "MessagingService";
	private static final String SYSTEM = "Notification";
	private static final String INCOMING_MESSAGE_ERROR = "IncomingMessageError";
	private static final String INCOMING_MESSAGE_WARNING = "IncomingMessageWarning";
	private static final String ATTR_ERROR = "Error";
	private static final String ATTR_WARNING = "Warning";

	// Firebase intent keys
	public static final String NOTIFICATION_ACTION = "ue4_fb.notificationAction";
	public static final String KEY_PUSH_NOTIFICATION = "ue4_fb.push";
	public static final String KEY_NOTIFICATION_BODY = "ue4_fb.body";
	private static final String KEY_MESSAGE_ID = "ue4_fb.messageId";
	private static final String KEY_NOTIFICATION_TYPE = "ue4_fb.type";

	@Override
	public void onMessageReceived(@NonNull RemoteMessage remoteMessage) {
		Log.verbose("Firebase onMessageReceived");
		super.onMessageReceived(remoteMessage);
		final String messageId = remoteMessage.getMessageId();
		final Map<String, String> data = remoteMessage.getData();
		logNotificationInfo(remoteMessage.getNotification());
		if (data != null) {
			EpicFirebaseNotificationMeta meta = null;
			try {
				meta = getContentInfo(messageId, data);
			} catch (JSONException e) {
				Log.error("Firebase unable to parse payload", e);
			}
			if (meta != null && messageId != null) {
				doNotify(messageId, meta);
			} else {
				Log.debug("containerType is empty");
				sendLocalNotification(remoteMessage);
			}
		}
	}

	private void doNotify(String messageId, EpicFirebaseNotificationMeta meta) {
		createChannel(meta);
		Intent intent = getDefaultIntent(messageId, meta);
		NotificationCompat.Builder notificationBuilder = getNotificationBuilder(meta, intent);
		NotificationManager notificationManager;
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
			notificationManager = getSystemService(NotificationManager.class);
		} else {
			notificationManager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
		}
		if (notificationManager != null) {
			if (GameApplication.isAppInBackground()) {
				notificationManager.notify(new Random(System.currentTimeMillis()).nextInt(), notificationBuilder.build());
				Log.verbose("Push notification notify");
			} else {
				this.startActivity(intent);
				Log.verbose("Push notification sent to activity while app is in the foreground");
			}
		}
	}

	@NonNull
	private NotificationCompat.Builder getNotificationBuilder(@NonNull EpicFirebaseNotificationMeta meta, @NonNull Intent defaultIntent) {
		return new NotificationCompat.Builder(this, meta.getChannelType())
			.setSmallIcon(meta.getNotificationResId())
			.setContentTitle(meta.getTitle())
			.setContentText(meta.getMessage())
			.setContentInfo(meta.getContentInfo())
			.setContentIntent(getPendingIntentIntent(defaultIntent))
			.setAutoCancel(true)
			.setPriority(NotificationCompat.PRIORITY_DEFAULT);
	}

	@Nullable
	private EpicFirebaseNotificationMeta getContentInfo(@Nullable String messageId, @NonNull Map<String, String> messageData) throws JSONException {
		@Nullable EpicFirebaseNotificationMeta meta = null;
		String payload = messageData.get(PAYLOAD_P_KEY);
		logMessageData(messageData);
		if (!TextUtils.isEmpty(payload)) {
			String type = getType(payload);
			Log.verbose("Message Received ( " + messageId + ") type = " + type + " : " + payload);

//			meta = new EpicFirebaseNotificationMeta();
		} else {
			Log.warn("Firebase message received ( " + messageId + ") - NO PAYLOAD");
		}
		return meta;
	}

	private void logNotificationInfo(@Nullable RemoteMessage.Notification notification) {
		if(notification == null) {
			Log.verbose("Firebase no notification data");
		} else {
			JSONObject notificationData = new JSONObject();
			try {
				notificationData.put("title", getSafeString(notification.getTitle()));
				notificationData.put("body", getSafeString(notification.getBody()));
				notificationData.put("body_loc_key", getSafeString(notification.getBodyLocalizationKey()));
				notificationData.put("click_action", getSafeString(notification.getClickAction()));
				notificationData.put("color", getSafeString(notification.getColor()));
				notificationData.put("icon", getSafeString(notification.getIcon()));
				notificationData.put("sound", getSafeString(notification.getSound()));
				notificationData.put("tag", getSafeString(notification.getTag()));
				notificationData.put("title_loc_key", getSafeString(notification.getTitleLocalizationKey()));
				String [] bodyArgs = notification.getBodyLocalizationArgs();
				if(bodyArgs != null) {
					int i = 0;
					for(String arg : bodyArgs) {
						notificationData.put("bodyArg" + i, getSafeString(arg));
						i++;
					}
				}
				Uri link = notification.getLink();
				if(link != null) {
					notificationData.put("title_loc_key", getSafeString(link.toString()));
				}
				String [] titleArgs = notification.getTitleLocalizationArgs();
				if(titleArgs != null) {
					int i = 0;
					for(String arg : titleArgs) {
						notificationData.put("titleArg" + i, getSafeString(arg));
						i++;
					}
				}
				
			} catch (Exception e) {
				Log.error("Unable to log notification", e);
			}
			Log.verbose("Firebase Notification data " + getSafeString(notificationData.toString()));
		}
	}
	
	private void logMessageData(Map<String, String> messageData) {
		JSONObject jsonObject = new JSONObject();
		for (Map.Entry<String, String> entry : messageData.entrySet()) {
			if (!"p".equals(entry.getKey())) {
				try {
					jsonObject.put(entry.getKey(), entry.getValue());
				} catch (JSONException e) {
					Log.error("Unable to add key:" + getSafeString(entry.getKey()) + " value:" + getSafeString(entry.getValue()));
				}
			}
		}
		Log.verbose("Firebase notification meta: " + jsonObject.toString());
	}

	public static String getSafeString(@Nullable String string) {
		return Objects.toString(string, "<null>");
	}

	public static int getNotificationIconId(@NonNull Context context) {
		int notificationIconID = context.getResources().getIdentifier(FIREBASE_ICON_NAME, FIREBASE_ICON_TYPE, context.getPackageName());
		if (notificationIconID == 0) {
			notificationIconID = LocalNotificationReceiver.getNotificationIconID(context);
		}
		return notificationIconID;
	}

	@NonNull
	private Intent getDefaultIntent(@NonNull String messageId, @NonNull EpicFirebaseNotificationMeta meta) {
		Intent defaultIntent = new Intent(this, GameActivity.class);
		defaultIntent.setAction(NOTIFICATION_ACTION);
		defaultIntent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
		defaultIntent.putExtra(KEY_MESSAGE_ID, messageId);
		defaultIntent.putExtra(KEY_PUSH_NOTIFICATION, true);
		defaultIntent.putExtra(KEY_NOTIFICATION_TYPE, meta.getType());
		defaultIntent.putExtra(KEY_NOTIFICATION_BODY, meta.getPayload());
		return defaultIntent;
	}


	@NonNull
	private PendingIntent getPendingIntentIntent(@NonNull Intent defaultIntent) {
		return PendingIntent.getActivity(this, 1, defaultIntent, PendingIntent.FLAG_UPDATE_CURRENT);
	}

	public void createChannel(@NonNull EpicFirebaseNotificationMeta meta) {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
			NotificationManager notificationManager = getSystemService(NotificationManager.class);
			if (notificationManager != null) {
				NotificationChannel channel = notificationManager.getNotificationChannel(meta.getChannelType());
				if (channel == null) {
					int importance = NotificationManager.IMPORTANCE_DEFAULT;
					channel = new NotificationChannel(meta.getChannelType(), meta.getChannelTitle(), importance);
					channel.setDescription(meta.getChannelDescription());
					notificationManager.createNotificationChannel(channel);
				}
			}
		}
	}

	private void sendLocalNotification(@NonNull RemoteMessage remoteMessage) {
		RemoteMessage.Notification notification = remoteMessage.getNotification();
		if (notification != null) {
			Intent notificationIntent = new Intent(this, LocalNotificationReceiver.class);
			int notificationID = LocalNotificationGetID(this);
			notificationIntent.putExtra(KEY_LOCAL_NOTIFICATION_ID, notificationID);
			notificationIntent.putExtra(KEY_LOCAL_NOTIFICATION_TITLE, notification.getTitle());
			notificationIntent.putExtra(KEY_LOCAL_NOTIFICATION_BODY, notification.getBody());
			notificationIntent.putExtra(KEY_LOCAL_NOTIFICATION_ACTION, notification.getClickAction());
			sendBroadcast(notificationIntent);
		}
	}

	@NonNull
	static String getType(@NonNull String payload) throws JSONException {
		JSONObject o = new JSONObject(payload);
		return o.optString(ATTR_TYPE);
	}
}
