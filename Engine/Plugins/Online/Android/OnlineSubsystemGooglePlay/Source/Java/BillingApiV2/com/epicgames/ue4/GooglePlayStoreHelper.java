// Copyright Epic Games, Inc. All Rights Reserved.
//This file needs to be here so the "ant" build step doesnt fail when looking for a /src folder.

package com.epicgames.ue4;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentSender.SendIntentException;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.RemoteException;
import com.android.vending.billing.util.Base64; //WMM should use different base64 here.
import com.android.billingclient.api.*;
import java.util.ArrayList;
import java.util.List;
import org.json.JSONException;
import org.json.JSONObject; 

public class GooglePlayStoreHelper implements StoreHelper
{
	// Our IAB helper interface provided by google.
	private BillingClient mBillingClient;

	// Flag that determines whether the store is ready to use.
	private boolean bIsIapSetup;

	// Output device for log messages.
	private Logger Log;

	// Cache access to the games activity.
	private GameActivity gameActivity;

	// The google play license key.
	private String productKey;

	private final int UndefinedFailureResponse = -1;

	private int outstandingConsumeRequests = 0;

	private boolean bIsConsumeComplete = false;

	private class GooglePlayProductDescription
	{
		// Product offer id 
		public String id;
		// Product friendly name
		public String title;
		// Product description
		public String description;
		// Currency friendly string 
		public String price;
		// Raw price in currency units
		public Float priceRaw;
		// Local currency code
		public String currencyCode;
		// Original Json representation
		public String originalJson;
	}

	public interface PurchaseLaunchCallback
	{
		void launchForResult(PendingIntent pendingIntent, int requestCode);
	}

	public GooglePlayStoreHelper(String InProductKey, GameActivity InGameActivity, final Logger InLog)
	{
		// IAP is not ready to use until the service is instantiated.
		bIsIapSetup = false;

		Log = InLog;
		Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::GooglePlayStoreHelper");

		gameActivity = InGameActivity;
		productKey = InProductKey;

		PurchasesUpdatedListener purchasesUpdatedListener = new PurchasesUpdatedListener(){
			@Override
			public void onPurchasesUpdated(BillingResult billingResult, List<Purchase> purchases){
				if (billingResult.getResponseCode() == BillingClient.BillingResponseCode.OK
					&& purchases != null) {
						for (Purchase purchase : purchases) {
							onPurchaseResult(billingResult, purchase);
						}
				} else
				{ 
					Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::UserCancelled Purchase " + billingResult.getDebugMessage());
					nativePurchaseComplete(billingResult.getResponseCode(), "", "", "", "");
					// Handle an error caused by a user cancelling the purchase flow.
				}
			}
		};

		mBillingClient = BillingClient.newBuilder(gameActivity).setListener(purchasesUpdatedListener).enablePendingPurchases().build();
		
		mBillingClient.startConnection(new BillingClientStateListener() {
			@Override
			public void onBillingSetupFinished(BillingResult billingResult) {
				if (billingResult.getResponseCode() == BillingClient.BillingResponseCode.OK) {
					bIsIapSetup = true;
					Log.debug("In-app billing supported for " + gameActivity.getPackageName());
				}
				else
				{
					Log.debug("In-app billing NOT supported for " + gameActivity.getPackageName() + " error " + billingResult.getResponseCode());
				}
			}
    
			@Override
			public void onBillingServiceDisconnected() {
				// Try to restart the connection on the next request to
				// Google Play by calling the startConnection() method.
				bIsIapSetup = false;
			}
		});
	}

	///////////////////////////////////////////////////////
	// The StoreHelper interfaces implementation for Google Play Store.

	/**
	 * Determine whether the store is ready for purchases.
	 */
	public boolean IsAllowedToMakePurchases()
	{
		Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::IsAllowedToMakePurchases");
		return bIsIapSetup;
	}

	/**
	 * Query product details for the provided skus
	 */
	public boolean QueryInAppPurchases(String[] InProductIDs)
	{
		Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::QueryInAppPurchases");

		if (InProductIDs.length > 0)
		{
			ArrayList<String> skuList = new ArrayList<String>(InProductIDs.length);

			for (String productId : InProductIDs)
			{
				Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::QueryInAppPurchases - Querying " + productId);
				skuList.add(productId);
			}

			Log.debug("[GooglePlayStoreHelper] - NumSkus: " + skuList.size());

			SkuDetailsParams.Builder skuDetailsParams = SkuDetailsParams.newBuilder();
			skuDetailsParams = skuDetailsParams.setSkusList(skuList).setType(BillingClient.SkuType.INAPP);
			
			mBillingClient.querySkuDetailsAsync(skuDetailsParams.build(),
				new SkuDetailsResponseListener() {
					@Override
					public void onSkuDetailsResponse(BillingResult billingResult,
						List<SkuDetails> skuDetailsList) 
					{
						// Process the result.
						int response = billingResult.getResponseCode();

						if(response == BillingClient.BillingResponseCode.OK && skuDetailsList != null)
						{
							Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::QueryInAppPurchases - Response " + response + " SkuDetails:" + skuDetailsList.toString());
							
							ArrayList<GooglePlayProductDescription> productDescriptions = new ArrayList<GooglePlayProductDescription>();
							for (SkuDetails thisSku : skuDetailsList)
							{
								GooglePlayProductDescription newDescription = new GooglePlayProductDescription();
								newDescription.id = thisSku.getSku();
								newDescription.title = thisSku.getTitle();
								newDescription.description = thisSku.getDescription();
								newDescription.price = thisSku.getPrice();
								double priceRaw = thisSku.getPriceAmountMicros() / 1000000.0;
								newDescription.priceRaw = (float)priceRaw;
								newDescription.currencyCode = thisSku.getPriceCurrencyCode();
								newDescription.originalJson = thisSku.getOriginalJson();
								productDescriptions.add(newDescription);
							}

							// Should we send JSON, or somehow serialize this so we can have random access fields?
							ArrayList<String> productIds = new ArrayList<String>();
							ArrayList<String> titles = new ArrayList<String>();
							ArrayList<String> descriptions = new ArrayList<String>();
							ArrayList<String> prices = new ArrayList<String>();
							ArrayList<Float> pricesRaw = new ArrayList<Float>();
							ArrayList<String> currencyCodes = new ArrayList<String>();
							ArrayList<String> originalJson = new ArrayList<String>();
						
							for (GooglePlayProductDescription product : productDescriptions)
							{
								productIds.add(product.id);
								Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::QueryInAppPurchases - Parsing details for: " + product.id);

								titles.add(product.title);
								Log.debug("[GooglePlayStoreHelper] - title: " + product.title);

								descriptions.add(product.description);
								Log.debug("[GooglePlayStoreHelper] - description: " + product.description);

								prices.add(product.price);
								Log.debug("[GooglePlayStoreHelper] - price: " + product.price);

								pricesRaw.add(product.priceRaw);
								Log.debug("[GooglePlayStoreHelper] - price_amount_micros: " + product.priceRaw);

								currencyCodes.add(product.currencyCode);
								Log.debug("[GooglePlayStoreHelper] - price_currency_code: " + product.currencyCode);

								originalJson.add(product.originalJson);
								Log.debug("[GooglePlayStoreHelper] - original_json: " + product.originalJson);

							}

							float[] pricesRawPrimitive = new float[pricesRaw.size()];
							for (int i = 0; i < pricesRaw.size(); i++)
							{
								pricesRawPrimitive[i] = pricesRaw.get(i);
							}

							Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::QueryInAppPurchases " + productIds.size() + " items - Success!");
							nativeQueryComplete(BillingClient.BillingResponseCode.OK, productIds.toArray(new String[productIds.size()]), titles.toArray(new String[titles.size()]), descriptions.toArray(new String[descriptions.size()]), prices.toArray(new String[prices.size()]), pricesRawPrimitive, currencyCodes.toArray(new String[currencyCodes.size()]), originalJson.toArray(new String[originalJson.size()]));
						}
						else
						{
							Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::QueryInAppPurchases - Failed with: " + response);
							// If anything fails right now, stop immediately, not sure how to reconcile partial success/failure
							nativeQueryComplete(response, null, null, null, null, null, null, null);
						}
						
						Log.debug("[GooglePlayStoreHelper] - nativeQueryComplete done!");
					}
				}
			);			
		}
		else
		{
			// nothing to query
			Log.debug("[GooglePlayStoreHelper] - no products given to query");
			nativeQueryComplete(UndefinedFailureResponse, null, null, null, null, null, null, null);
			return false;
		}

		return true;
	}

	/**
	 * Start the purchase flow for a particular sku
	 */
	final int purchaseIntentIdentifier = 1001;
	public boolean BeginPurchase(String ProductID, String ObfuscatedAccountId)
	{
		ArrayList<String> skuList = new ArrayList<String>(1);
		skuList.add(ProductID);

		SkuDetailsParams.Builder skuDetailsParams = SkuDetailsParams.newBuilder();
		skuDetailsParams.setSkusList(skuList).setType(BillingClient.SkuType.INAPP);
		
		class SkuDetailsResponseListenerImpl implements SkuDetailsResponseListener
		{
			String ObfuscatedAccountId = null;
			public SkuDetailsResponseListenerImpl setObfuscatedAccountId(String InObfuscatedAccountId)
			{
				ObfuscatedAccountId = InObfuscatedAccountId;
				return this;
			}
			
			@Override
			public void onSkuDetailsResponse(BillingResult billingResult,
				List<SkuDetails> skuDetailsList) 
			{
				int response = billingResult.getResponseCode();
				if(response == BillingClient.BillingResponseCode.OK && skuDetailsList != null)
				{
					for(SkuDetails skuDetails : skuDetailsList)
					{
						BillingFlowParams.Builder flowParams = BillingFlowParams.newBuilder().setSkuDetails(skuDetails);
						
						if(ObfuscatedAccountId != null)
						{
							flowParams = flowParams.setObfuscatedAccountId(ObfuscatedAccountId);
						}
	
						if (gameActivity.IsInVRMode())
						{
							int responseCode = mBillingClient.isFeatureSupported(BillingClient.FeatureType.IN_APP_ITEMS_ON_VR).getResponseCode();
							boolean isSupportedInVR = (responseCode == BillingClient.BillingResponseCode.OK);
							if (isSupportedInVR)
							{
								Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::BeginPurchase - v7 VR purchase" + skuDetails.getSku());
								flowParams = flowParams.setVrPurchaseFlow(true);
							}
							else
							{
								Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::BeginPurchase - v3 IAB purchase:" + skuDetails.getSku());
								flowParams = flowParams.setVrPurchaseFlow(false);
							}
						}
						else
						{
							Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::BeginPurchase - v3 IAB purchase:" + skuDetails.getSku());
							flowParams = flowParams.setVrPurchaseFlow(false);
						}

						int responseCode = mBillingClient.launchBillingFlow(gameActivity, flowParams.build()).getResponseCode();
						Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::BeginPurchase - Launching billing flow " + skuDetails.getSku());
					}
				}
				else
				{
					Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::BeginPurchase - Failed! " + TranslateServerResponseCode(response));
					nativePurchaseComplete(UndefinedFailureResponse, "", "", "", "");
				}
			}
		}
		
		mBillingClient.querySkuDetailsAsync(skuDetailsParams.build(), new SkuDetailsResponseListenerImpl().setObfuscatedAccountId(ObfuscatedAccountId));
		return true;
	}

	/**
	 * Restore previous purchases the user may have made.
	 */
	public boolean RestorePurchases(String[] InProductIDs, boolean[] bConsumable)
	{
		Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::RestorePurchases");

		ArrayList<Purchase> ownedProducts = new ArrayList<Purchase>();
		int responseCode = GatherOwnedPurchaseData(ownedProducts);
		if (responseCode == BillingClient.BillingResponseCode.OK)
		{
			Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::RestorePurchases - User has previously purchased " + ownedProducts.size() + " inapp products" );

			final ArrayList<Purchase> f_ownedProducts = ownedProducts;

			final ArrayList<String> f_ownedSkus = new ArrayList<String>();
			final ArrayList<String> f_purchaseDataList = new ArrayList<String>();
			final ArrayList<String> f_signatureList = new ArrayList<String>();
			final String[] RestoreProductIDs = InProductIDs;
			final boolean[] bRestoreConsumableFlags = bConsumable;

			// Is this here to ensure callbacks are fired on the main thread?
			Handler mainHandler = new Handler(Looper.getMainLooper());
			mainHandler.post(new Runnable()
			{
				@Override
				public void run()
				{
					Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::RestorePurchases - Now consuming any purchases that may have been missed.");
					final ArrayList<String> productTokens = new ArrayList<String>();
					final ArrayList<String> receipts = new ArrayList<String>();
					final ArrayList<Integer> cachedErrorResponses = new ArrayList<Integer>();

					bIsConsumeComplete = false;
					// @todo possible bug, restores ALL ownedSkus, not just those provided by caller in RestoreProductIDs
					for (Purchase ownedProduct : f_ownedProducts)
					{
						
						productTokens.add(ownedProduct.getPurchaseToken());

						boolean bTryToConsume = false;

						// This is assuming that all purchases should be consumed. Consuming a purchase that is meant to be a one-time purchase makes it so the
						// user is able to buy it again. Also, it makes it so the purchase will not be able to be restored again in the future.

						for (int Idy = 0; Idy < RestoreProductIDs.length; Idy++)
						{
							if( ownedProduct.getSku().equals(RestoreProductIDs[Idy]) )
							{
								if(Idy < bRestoreConsumableFlags.length)
								{
									bTryToConsume = bRestoreConsumableFlags[Idy];
									Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::RestorePurchases - Found Consumable Flag for Product " + ownedProduct.getSku() + " bConsumable = " + bTryToConsume);
								}
								break;
							}
						}

						final Purchase constProduct = ownedProduct;
						if (bTryToConsume)
						{
							Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::RestorePurchases - Attempting to consume " + ownedProduct.getSku());
							ConsumeParams consumeParams = ConsumeParams.newBuilder().setPurchaseToken(ownedProduct.getPurchaseToken()).build();
							mBillingClient.consumeAsync(consumeParams, (new ConsumeResponseListener() {
								@Override
								public void onConsumeResponse(BillingResult billingResult, String purchaseToken) {
									if(billingResult.getResponseCode() == BillingClient.BillingResponseCode.OK)
									{
										// How do we get purchase here.. need to figure out a way to persist cachedResponse and receipts
										Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::RestorePurchases - Purchase restored for " + constProduct.getSku());
										String receipt = Base64.encode(constProduct.getOriginalJson().getBytes());
										receipts.add(receipt);
										
										f_ownedSkus.add(constProduct.getSku());
										f_signatureList.add(constProduct.getSignature());
									}
									else
									{
										Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::RestorePurchases - consumePurchase failed for " + constProduct.getSku() + " with error " + billingResult.getResponseCode());
										receipts.add("");
										cachedErrorResponses.add(billingResult.getResponseCode());
									}
									outstandingConsumeRequests--;

									if(outstandingConsumeRequests <= 0)
									{
										Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::RestorePurchases - Success!");
										int finalResponse = (cachedErrorResponses.size() > 0) ? cachedErrorResponses.get(0) : BillingClient.BillingResponseCode.OK;
										nativeRestorePurchasesComplete(finalResponse, f_ownedSkus.toArray(new String[f_ownedSkus.size()]), productTokens.toArray(new String[productTokens.size()]), receipts.toArray(new String[receipts.size()]), f_signatureList.toArray(new String[f_signatureList.size()]));
										bIsConsumeComplete = true;
									}
								}
							}));
								
							outstandingConsumeRequests++;
						}
						else
						{
							// How do we get purchase here.. need to figure out a way to persist cachedResponse and receipts
							Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::RestorePurchases - Purchase restored for " + constProduct.getSku());
							String receipt = Base64.encode(constProduct.getOriginalJson().getBytes());
							receipts.add(receipt);
										
							f_ownedSkus.add(constProduct.getSku());
							f_signatureList.add(constProduct.getSignature());
						}
					}

					// There was nothing to consume, so we are done!
					if(outstandingConsumeRequests <= 0 && !bIsConsumeComplete)
					{
						Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::RestorePurchases - Success!");
						nativeRestorePurchasesComplete(BillingClient.BillingResponseCode.OK, f_ownedSkus.toArray(new String[f_ownedSkus.size()]), productTokens.toArray(new String[productTokens.size()]), receipts.toArray(new String[receipts.size()]), f_signatureList.toArray(new String[f_signatureList.size()]));
					}
				}
			});
		}
		else
		{
			nativeRestorePurchasesComplete(responseCode, null, null, null, null);
			Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::RestorePurchases - Failed to collect existing purchases");
			return false;
		}

		return true;
	}

	public boolean QueryExistingPurchases()
	{
		Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::QueryExistingPurchases");
		ArrayList<String> ownedSkus = new ArrayList<String>();
		ArrayList<String> purchaseDataList = new ArrayList<String>();
		ArrayList<String> signatureList = new ArrayList<String>();

		ArrayList<Purchase> ownedProducts = new ArrayList<Purchase>();

		int responseCode = GatherOwnedPurchaseData(ownedProducts);
		if (responseCode == BillingClient.BillingResponseCode.OK)
		{
			Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::QueryExistingPurchases - User has previously purchased " + ownedSkus.size() + " inapp products" );

			ArrayList<String> productTokens = new ArrayList<String>();
			ArrayList<String> receipts = new ArrayList<String>();

			for (Purchase purchase : ownedProducts)
			{
				productTokens.add(purchase.getPurchaseToken());
				ownedSkus.add(purchase.getSku());
				signatureList.add(purchase.getSignature());
				String receipt = Base64.encode(purchase.getOriginalJson().getBytes());
				receipts.add(receipt);
			}

			Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::QueryExistingPurchases - Success!");
			nativeQueryExistingPurchasesComplete(responseCode, ownedSkus.toArray(new String[ownedSkus.size()]), productTokens.toArray(new String[productTokens.size()]), receipts.toArray(new String[receipts.size()]), signatureList.toArray(new String[signatureList.size()]));
			return true;
		}
		else
		{
			Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::QueryExistingPurchases - Failed to collect existing purchases" +  TranslateServerResponseCode(responseCode));
			nativeQueryExistingPurchasesComplete(responseCode, null, null, null, null);
			return false;
		}
	}

	public void ConsumePurchase(String purchaseToken)
	{
		Log.debug("[GooglePlayStoreHelper] - Beginning ConsumePurchase: " + purchaseToken);

		final String f_purchaseToken = purchaseToken;

		Handler mainHandler = new Handler(Looper.getMainLooper());
		mainHandler.post(new Runnable()
		{
			@Override
			public void run()
			{
				ConsumeParams consumeParams = ConsumeParams.newBuilder().setPurchaseToken(f_purchaseToken).build();
				mBillingClient.consumeAsync(consumeParams, (new ConsumeResponseListener() {
					@Override
					public void onConsumeResponse(BillingResult billingResult, String purchaseToken) {
						if(billingResult.getResponseCode() == BillingClient.BillingResponseCode.OK)
						{
							Log.debug("[GooglePlayStoreHelper] - ConsumePurchase success");
						}
						else
						{
							Log.debug("[GooglePlayStoreHelper] - ConsumePurchase failed with error " + TranslateServerResponseCode(billingResult.getResponseCode()));
						}
					}
				}));
			}
		});
	}
	
	public boolean onActivityResult(int requestCode, int resultCode, Intent data)
	{
		Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::onActivityResult unimplemented on BillingApiV2");
		return false;
	}

	///////////////////////////////////////////////////////
	// Game Activity/Context driven methods we need to listen for.

	/**
	 * On Destory we should unbind our IInAppBillingService service
	 */
	public void onDestroy()
	{
		Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::onDestroy");

		if (mBillingClient != null)
		{
			mBillingClient = null; //WMM ???
		}
	}

	/**
	 * Route taken by the Purchase workflow. We listen for our purchaseIntentIdentifier request code and
	 * handle the response accordingly
	 */
	public boolean onPurchaseResult(BillingResult billingResult, Purchase purchase)
	{
		Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::onPurchaseResult");

		if (billingResult == null)
		{
			Log.debug("Null data in purchase activity result.");
			nativePurchaseComplete(UndefinedFailureResponse, "", "", "", "");
			return true;
		}

		int responseCode = billingResult.getResponseCode();
		if (responseCode == BillingClient.BillingResponseCode.OK)
		{
			Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::onActivityResult - Processing purchase result. Response Code: " + TranslateServerResponseCode(responseCode));
			Log.debug("Purchase data: " + purchase.toString());
			Log.debug("Data signature: " + purchase.getSignature());

			if(purchase.getPurchaseState() == Purchase.PurchaseState.PURCHASED)
			{
				final String sku = purchase.getSku();
				final Purchase f_purchase = purchase;
				Handler mainHandler = new Handler(Looper.getMainLooper());
				mainHandler.post(new Runnable()
				{
					@Override
					public void run()
					{
						String receipt = Base64.encode(f_purchase.getOriginalJson().getBytes());
						nativePurchaseComplete(BillingClient.BillingResponseCode.OK, sku, f_purchase.getPurchaseToken(), receipt, f_purchase.getSignature());
					}
				});
			}
			else
			{
				// This is a pending purchase.. need to hold off on reporting as purchase to game until it actually occurs.
				// Special case - if pending purchase is USER_CANCELLED but the SKU is returned, then it is deferred
				final String sku = purchase.getSku();
				nativePurchaseComplete(BillingClient.BillingResponseCode.USER_CANCELED, sku, "", "", "");
			}
		}
		else if (responseCode == BillingClient.BillingResponseCode.USER_CANCELED)
		{
			Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::onActivityResult - Purchase canceled." + TranslateServerResponseCode(responseCode));
			nativePurchaseComplete(BillingClient.BillingResponseCode.USER_CANCELED, "", "", "", "");
		}
		else
		{
			Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::onActivityResult - Purchase failed. Result code: " +
				Integer.toString(responseCode) + ". Response: " + TranslateServerResponseCode(responseCode));
			nativePurchaseComplete(UndefinedFailureResponse, "", "", "", "");
		}

		return true;
	}


	///////////////////////////////////////////////////////
	// Internal helper functions that deal assist with various IAB related events

	/**
	 * Get a text tranlation of the Response Codes returned by google play.
	 */
	private String TranslateServerResponseCode(final int serverResponseCode)
	{
		// taken from https://developer.android.com/reference/com/android/billingclient/api/BillingClient.BillingResponseCode
		switch(serverResponseCode)
		{
			case BillingClient.BillingResponseCode.OK:
				return "Success";
			case BillingClient.BillingResponseCode.USER_CANCELED:
				return "User pressed back or canceled a dialog";
			case BillingClient.BillingResponseCode.SERVICE_UNAVAILABLE:
				return "Network connection is down";
			case BillingClient.BillingResponseCode.SERVICE_TIMEOUT:
				return "The request has reached the maximum timeout before Google Play responds";
			case BillingClient.BillingResponseCode.SERVICE_DISCONNECTED:
				return "Play Store service is not connected now - potentially transient state";
			case BillingClient.BillingResponseCode.BILLING_UNAVAILABLE:
				return "Billing API version is not supported for the type requested";
			case BillingClient.BillingResponseCode.FEATURE_NOT_SUPPORTED:
				return "Requested feature is not supported by Play Store on the current device";
			case BillingClient.BillingResponseCode.ITEM_UNAVAILABLE:
				return "Requested product is not available for purchase";
			case BillingClient.BillingResponseCode.DEVELOPER_ERROR:
				return "Invalid arguments provided to the API. This error can also indicate that the application was not correctly signed or properly set up for In-app Billing in Google Play, or does not have the necessary permissions in its manifest";
			case BillingClient.BillingResponseCode.ERROR:
				return "Fatal error during the API action";
			case BillingClient.BillingResponseCode.ITEM_ALREADY_OWNED:
				return "Failure to purchase since item is already owned";
			case BillingClient.BillingResponseCode.ITEM_NOT_OWNED:
				return "Failure to consume since item is not owned";
			default:
				return "Unknown Server Response Code";
		}
	}

	/**
	 * Recursive functionality to gather all of the purchases owned by a user.
	 * if the user owns a lot of products then we need to getPurchases again with a continuationToken
	 */
	private int GatherOwnedPurchaseData(ArrayList<Purchase> inPurchases)
	{
		int responseCode = UndefinedFailureResponse;
		try
		{
			Purchase.PurchasesResult purchasesResult = mBillingClient.queryPurchases(BillingClient.SkuType.INAPP);

			responseCode = purchasesResult.getResponseCode();
			Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::GatherOwnedPurchaseData - getPurchases result. Response Code: " + TranslateServerResponseCode(responseCode));
			if (responseCode == BillingClient.BillingResponseCode.OK)
			{
				List<Purchase> ownedItems = purchasesResult.getPurchasesList();
				for (Purchase purchase : ownedItems)
				{
					// Don't Mark pending purchases as owned
					if(purchase.getPurchaseState() == Purchase.PurchaseState.PURCHASED)
					{
						inPurchases.add(purchase);
					}
				}
			}
		}
		catch (Exception e)
		{
			Log.debug("[GooglePlayStoreHelper] - GooglePlayStoreHelper::GatherOwnedPurchaseData - Failed for purchase request!. " + e.getMessage());
		}

		return responseCode;
	}

	// Callback that notify the C++ implementation that a task has completed
	public native void nativeQueryComplete(int responseCode, String[] productIDs, String[] titles, String[] descriptions, String[] prices, float[] pricesRaw, String[] currencyCodes, String[] originalJson );
	public native void nativePurchaseComplete(int responseCode, String ProductId, String ProductToken, String ReceiptData, String Signature);
	public native void nativeRestorePurchasesComplete(int responseCode, String[] ProductIds, String[] ProductTokens, String[] ReceiptsData, String[] Signatures);
	public native void nativeQueryExistingPurchasesComplete(int responseCode, String[] ProductIds, String[] ProductTokens, String[] ReceiptsData, String[] Signatures);
}

