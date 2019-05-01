package com.epicgames.ue4.notifications;

import android.content.Context;
import android.content.SharedPreferences;
import android.support.annotation.NonNull;
import android.support.annotation.Nullable;
import android.text.TextUtils;

import com.epicgames.ue4.Logger;
import com.google.firebase.FirebaseApp;
import com.google.firebase.iid.FirebaseInstanceId;
import com.google.firebase.iid.FirebaseInstanceIdService;

public class EpicFirebaseInstanceIDService extends FirebaseInstanceIdService {
	private static final Logger Log = new Logger("UE4-" + EpicFirebaseInstanceIDService.class.getSimpleName());
	private static final String PREFS_FILE_FIREBASE = "com.epicgames.firebase";
	private static final String KEY_FIREBASE_TOKEN = "firebasetoken";
	private static final String KEY_IS_UPDATED_TOKEN = "isUpdatedToken";
	private static final String KEY_IS_REGISTERED = "isRegistered";

	@Override
	public void onTokenRefresh() {
		String firebaseToken = getFirebaseInstanceToken();
		Log.debug("Refreshed Firebase token: " + firebaseToken);
		if (TextUtils.isEmpty(firebaseToken)) {
			Log.error("Firebase token is empty or null");
		} else {
			saveFirebaseToken(this, firebaseToken);
		}
	}

	private static String getFirebaseInstanceToken() {
		FirebaseInstanceId id = getFirebaseInstanceId();
		return (id == null) ? "" : id.getToken();
	}
	
	private static FirebaseInstanceId getFirebaseInstanceId() {
		try {
			FirebaseApp app = FirebaseApp.getInstance();
			return FirebaseInstanceId.getInstance(app);
		} catch (Exception e) {
			Log.error("FirebaseApp doesn't exist");
			return null;
		}
	}
	
	private static void saveFirebaseToken(@NonNull Context context, @NonNull String firebaseToken) {
		Log.debug("Firebase token to save : " + firebaseToken);
		String storedToken = getFirebaseTokenFromCache(context);
		boolean isUpdatedToken = !TextUtils.isEmpty(storedToken);
		SharedPreferences sharedPreferences = context.getSharedPreferences(PREFS_FILE_FIREBASE, Context.MODE_PRIVATE);
		SharedPreferences.Editor editor = sharedPreferences.edit();
		Log.debug("Firebase token isUpdated : " + isUpdatedToken);
		editor.putBoolean(KEY_IS_UPDATED_TOKEN, isUpdatedToken).apply();
		editor.putBoolean(KEY_IS_REGISTERED, false).apply();
		editor.putString(KEY_FIREBASE_TOKEN, firebaseToken).apply();
	}

	@SuppressWarnings("unused")
	static boolean isFirebaseTokenUpdated(@NonNull Context context) {
		SharedPreferences sharedPreferences = context.getSharedPreferences(PREFS_FILE_FIREBASE, Context.MODE_PRIVATE);
		boolean isUpdated = sharedPreferences.getBoolean(KEY_IS_UPDATED_TOKEN, true);
		Log.debug("Firebase token isUpdatedToken is " + isUpdated);
		return isUpdated;
	}

	public static boolean isFirebaseTokenRegistered(@NonNull Context context) {
		SharedPreferences sharedPreferences = context.getSharedPreferences(PREFS_FILE_FIREBASE, Context.MODE_PRIVATE);
		return sharedPreferences.getBoolean(KEY_IS_REGISTERED, false);
	}

	public static void setFirebaseTokenRegistered(@NonNull Context context, boolean isRegistered) {
		Log.debug("Firebase token isRegistered setting to " + isRegistered);
		SharedPreferences sharedPreferences = context.getSharedPreferences(PREFS_FILE_FIREBASE, Context.MODE_PRIVATE);
		SharedPreferences.Editor editor = sharedPreferences.edit();
		editor.putBoolean(KEY_IS_REGISTERED, isRegistered);
		editor.apply();
	}

	public static void unregisterFirebaseToken(@NonNull Context context) {
		setFirebaseTokenRegistered(context, false);
		SharedPreferences sharedPreferences = context.getSharedPreferences(PREFS_FILE_FIREBASE, Context.MODE_PRIVATE);
		SharedPreferences.Editor editor = sharedPreferences.edit();
		editor.remove(KEY_FIREBASE_TOKEN);
		editor.apply();
		Log.debug("Firebase token cleared");
	}

	private static String getFirebaseTokenFromCache(@NonNull Context context) {
		SharedPreferences sharedPreferences = context.getSharedPreferences(PREFS_FILE_FIREBASE, Context.MODE_PRIVATE);
		return sharedPreferences.getString(KEY_FIREBASE_TOKEN, null);
	}
	
	@Nullable
	public static String getFirebaseToken(@NonNull Context context) {
		String token = getFirebaseTokenFromCache(context);
		Log.debug("Firebase token retrieved from cache: " + token);
		if(TextUtils.isEmpty(token)) {
			// handle edge case where we missed onTokenRefresh - ex. App Upgrade
			token = getFirebaseInstanceToken();
			if(!TextUtils.isEmpty(token)) {
				Log.debug("Firebase token retrieved from Firebase: " + token);
				saveFirebaseToken(context, token);
			}
		}
		return token;
	}

}
