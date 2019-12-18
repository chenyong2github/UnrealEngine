// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

package com.epicgames.ue4.network;

import android.content.Context;
import android.support.annotation.NonNull;

public interface NetworkConnectivityClient {
	public interface Listener {
		void onNetworkAvailable();

		void onNetworkLost();
	}

	void initNetworkCallback(@NonNull Context context);

	/**
	 * See {@link NetworkConnectivityClient#addListener(Listener, boolean)}
	 */
	boolean addListener(Listener listener);

	/**
	 * @param listener The listener to add. Will be stored as a weak reference so a hard reference
	 *                 must be saved externally.
	 * @param fireImmediately Whether to trigger the listener with the current network state
	 *                        immediately after adding.
	 * @return Whether the change listener was added. Will be false if already registered.
	 */
	boolean addListener(Listener listener, boolean fireImmediately);

	/**
	 * Remove a given listener.
	 * @return Whether the change listener was removed. Will be false if not currently registered.
	 */
	boolean removeListener(Listener listener);
	
	/**
	 * Check for network connectivity
	 */
	void checkConnectivity();
}
