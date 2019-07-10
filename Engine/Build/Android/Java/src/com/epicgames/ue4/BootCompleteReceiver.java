// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

package com.epicgames.ue4;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;

import org.json.JSONException;
import org.json.JSONObject;

import java.util.Iterator;

public class BootCompleteReceiver extends BroadcastReceiver
{
	@Override
	public void onReceive(Context context, Intent intent)
	{
		// restore any scheduled notifications
		SharedPreferences preferences = context.getSharedPreferences("LocalNotificationPreferences", Context.MODE_PRIVATE);
		SharedPreferences.Editor editor = preferences.edit();
		try
		{
			boolean changed = false;
			JSONObject notificationDetails = new JSONObject(preferences.getString("notificationDetails", "{}"));
			for (Iterator<String> i = notificationDetails.keys(); i.hasNext(); )
			{
				try
				{
					String key = i.next();
					int notificationId = Integer.parseInt(key); 
					JSONObject details = notificationDetails.getJSONObject(key);
					String targetDateTime = details.getString("local-notification-targetDateTime");
					boolean localTime = details.getBoolean("local-notification-localTime");
					String title = details.getString("local-notification-title");
					String body = details.getString("local-notification-body");
					String action = details.getString("local-notification-action");
					String activationEvent = details.getString("local-notification-activationEvent");
					
					if (!GameActivity.LocalNotificationScheduleAtTime(context, notificationId, targetDateTime, localTime, title, body, action, activationEvent))
					{
						// if it fails, remove from details
						i.remove();
					}
				}
				catch (NumberFormatException | JSONException e)
				{
					e.printStackTrace();
					i.remove();
				}
			}
			
			if (changed)
			{
				editor.commit();
			}
		}
		catch (JSONException e)
		{
			e.printStackTrace();
		}
	}
}
