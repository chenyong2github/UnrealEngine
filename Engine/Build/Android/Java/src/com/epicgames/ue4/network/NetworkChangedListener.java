package com.epicgames.ue4.network;

import android.net.Network;

public interface NetworkChangedListener {
	
	void onNetworkAvailable(Network network);

	void onNetworkLost(Network network);
}
