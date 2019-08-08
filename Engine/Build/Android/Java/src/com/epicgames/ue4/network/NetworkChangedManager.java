package com.epicgames.ue4.network;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkRequest;
import android.support.annotation.NonNull;

import com.epicgames.ue4.Logger;

import java.lang.ref.WeakReference;
import java.util.HashSet;
import java.util.Set;

public final class NetworkChangedManager {

	private static final Logger Log = new Logger("UE4", "NetworkChangedManager");
	private static final String SYSTEM = "NetworkManager";
	private static final String COMPONENT = "ConnectivityManager";
	private static final String ACTION_ERROR = "InstanceNotAvailable";

	private static NetworkChangedManager instance;

	@NonNull
	private Set<WeakReference<NetworkChangedListener>> networkChangedListeners = new HashSet<>();

	@NonNull
	public static synchronized NetworkChangedManager getInstance() {
		if (instance == null) {
			instance = new NetworkChangedManager();
		}
		return instance;
	}

	private NetworkChangedManager() {
	}

	public void initNetworkCallback(@NonNull Context context) {
		if (android.os.Build.VERSION.SDK_INT < 21)
		{
			// unsupported before Lollipop
			return;
		}

		ConnectivityManager connectivityManager = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
		if (connectivityManager != null) {
			NetworkRequest.Builder builder = new NetworkRequest.Builder();
			connectivityManager.registerNetworkCallback(
				builder.build(),
				new ConnectivityManager.NetworkCallback() {
					@Override
					public void onAvailable(Network network) {
						Log.verbose("Network Available");
						for (WeakReference<NetworkChangedListener> listenerWeakRef : networkChangedListeners) {
							NetworkChangedListener listener = listenerWeakRef.get();
							if (listener != null) {
								listener.onNetworkAvailable(network);
							} else {
								removeListener(listenerWeakRef);
							}
						}
					}

					@Override
					public void onLost(Network network) {
						Log.verbose("Network Lost");
						for (WeakReference<NetworkChangedListener> listenerWeakRef : networkChangedListeners) {
							NetworkChangedListener listener = listenerWeakRef.get();
							if (listener != null) {
								listener.onNetworkLost(network);
							} else {
								removeListener(listenerWeakRef);
							}
						}
					}
				}
			);
		} else {
			Log.error("Unable to start connectivityManager");
		}
	}

	@SuppressWarnings({"unused", "UnusedReturnValue"})
	public boolean addListener(WeakReference<NetworkChangedListener> listener) {
		return networkChangedListeners.add(listener);
	}

	@SuppressWarnings({"unused", "UnusedReturnValue"})
	public boolean removeListener(WeakReference<NetworkChangedListener> listener) {
		return networkChangedListeners.remove(listener);
	}
}
