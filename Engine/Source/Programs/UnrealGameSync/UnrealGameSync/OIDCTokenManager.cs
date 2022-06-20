// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using IdentityModel.Client;
using IdentityModel.OidcClient;
using IdentityModel.OidcClient.Results;

#nullable disable

namespace UnrealGameSync
{
	public class OidcTokenManager
	{
		private UserSettings _settings;
		private readonly Dictionary<string, OidcTokenClient> _tokenClients = new Dictionary<string, OidcTokenClient>();

		public Dictionary<string, ProviderInfo> Providers { get; }

		private OidcTokenManager(UserSettings inSettings, Dictionary<string, ProviderInfo> inProviders)
		{
			_settings = inSettings;
			Providers = inProviders;

			Dictionary<string, string> refreshTokens = new Dictionary<string, string>();
			foreach (KeyValuePair<string, string> pair in _settings.ProviderToRefreshTokens)
			{
				string providerIdentifier = pair.Key;
				try
				{
					byte[] keyBytes = Convert.FromBase64String(pair.Value);
					byte[] unencryptedBytes = ProtectedData.Unprotect(keyBytes, null, DataProtectionScope.CurrentUser);
					refreshTokens.TryAdd(providerIdentifier, Encoding.ASCII.GetString(unencryptedBytes));
				}
				catch (Exception)
				{
					// ignore any invalid data in our stored refresh tokens, we will simply prompt the user to login again
				}
			}

			foreach (KeyValuePair<string, ProviderInfo> providerPair in inProviders)
			{
				ProviderInfo provider = providerPair.Value;

				OidcTokenClient tokenClient = new OidcTokenClient(new Uri(provider.ServerUri), provider.ClientId, provider.RedirectUri);

				string refreshToken;
				if (refreshTokens.TryGetValue(provider.Identifier, out refreshToken))
				{
					tokenClient.SetRefreshToken(refreshToken);
				}
				_tokenClients.Add(provider.Identifier, tokenClient);
			}
		}

		public bool HasUnfinishedLogin()
		{
			return _tokenClients.Any(pair => pair.Value.GetStatusForProvider() == OidcStatus.NotLoggedIn);
		}

		internal static OidcTokenManager CreateFromConfigFile(UserSettings settings, List<OpenProjectInfo> configFiles)
		{
			// join the provider configuration from all projects
			Dictionary<string, ProviderInfo> providers = new Dictionary<string, ProviderInfo>();
			foreach (OpenProjectInfo detectProjectSettingsTask in configFiles)
			{
				if(detectProjectSettingsTask == null)
				{
					continue;
				}

				ConfigFile configFile = detectProjectSettingsTask.LatestProjectConfigFile;
				if(configFile == null)
				{
					continue;
				}

				ConfigSection providerSection = configFile.FindSection("OIDCProvider");
				if (providerSection == null)
				{
					continue;
				}

				string[] providerValues = providerSection.GetValues("Provider", (string[]) null);
				foreach (ConfigObject provider in providerValues.Select(s => new ConfigObject(s)).ToList())
				{
					string identifier = provider.GetValue("Identifier");
					string serverUri = provider.GetValue("ServerUri");
					string clientId = provider.GetValue("ClientId");
					string displayName = provider.GetValue("DisplayName");
					string redirectUri = provider.GetValue("RedirectUri");

					// we might get a provider with the same identifier from another project, in which case we only keep the first one
					providers.TryAdd(identifier, new ProviderInfo(identifier, serverUri, clientId, displayName, redirectUri));
				}
			}

			if (providers.Count == 0)
				return null;
			return new OidcTokenManager(settings, providers);
		}

		public async Task Login(string providerIdentifier)
		{
			OidcTokenClient tokenClient = _tokenClients[providerIdentifier];

			string refreshToken = await tokenClient.Login();
			if (!string.IsNullOrEmpty(refreshToken))
			{
				// if we got a refresh token we store that for future use
				byte[] refreshTokenBytes = Encoding.ASCII.GetBytes(refreshToken);
				byte[] encryptedBytes = ProtectedData.Protect(refreshTokenBytes, null, DataProtectionScope.CurrentUser);

				_settings.ProviderToRefreshTokens[providerIdentifier] = Convert.ToBase64String(encryptedBytes);
			}


		}

		public Task<string> GetAccessToken(string providerIdentifier)
		{
			return _tokenClients[providerIdentifier].GetAccessToken();
		}

		public OidcStatus GetStatusForProvider(string providerIdentifier)
		{
			return _tokenClients[providerIdentifier].GetStatusForProvider();
		}

		public class ProviderInfo
		{
			public string Identifier { get; }
			public string ServerUri { get; }
			public string ClientId { get; }
			public string DisplayName { get; }
			public string RedirectUri { get; }

			public ProviderInfo(string inIdentifier, string inServerUri, string inClientId, string inDisplayName, string inRedirectUri)
			{
				Identifier = inIdentifier; 
				ServerUri = inServerUri;
				ClientId = inClientId;
				DisplayName = inDisplayName;
				RedirectUri = inRedirectUri;
			}
		}
	}


	public enum OidcStatus
	{
		Connected,
		NotLoggedIn,
		TokenRefreshRequired
	}

	class OidcTokenClient
	{
		private readonly Uri _authorityUri;
		private readonly string _clientId;
		private readonly string _redirectUri;
		private readonly string _scopes;

		private OidcClient _oidcClient;

		private string _refreshToken;
		private string _accessToken;
		private DateTime _tokenExpiry;

		private bool _initialized = false;


		public OidcTokenClient(Uri inAuthorityUri, string inClientId, string inRedirectUri, string inScopes = "openid profile offline_access")
		{
			_authorityUri = inAuthorityUri;
			_clientId = inClientId;
			_redirectUri = inRedirectUri;
			_scopes = inScopes;
		}

		private async Task Initialize()
		{
			if (_initialized)
				return;

			OidcClientOptions options = new OidcClientOptions
			{
				Authority = _authorityUri.ToString(),
				Policy = new Policy { Discovery = new DiscoveryPolicy { Authority = _authorityUri.ToString() } },
				ClientId = _clientId,
				Scope = _scopes,
				FilterClaims = false,
				RedirectUri = _redirectUri,
				Flow = OidcClientOptions.AuthenticationFlow.AuthorizationCode,
				ResponseMode = OidcClientOptions.AuthorizeResponseMode.Redirect
			};

			// we need to fetch the discovery document ourselves to support OIDC Authorities which have a subresource for it
			// with Okta has for authorization servers for instance.
			DiscoveryDocumentResponse discoveryDocument = await GetDiscoveryDocument();
			options.ProviderInformation = new ProviderInformation
			{
				IssuerName = discoveryDocument.Issuer,
				KeySet = discoveryDocument.KeySet,

				AuthorizeEndpoint = discoveryDocument.AuthorizeEndpoint,
				TokenEndpoint = discoveryDocument.TokenEndpoint,
				EndSessionEndpoint = discoveryDocument.EndSessionEndpoint,
				UserInfoEndpoint = discoveryDocument.UserInfoEndpoint,
				TokenEndPointAuthenticationMethods = discoveryDocument.TokenEndpointAuthenticationMethodsSupported
			};

			_oidcClient = new OidcClient(options);
			_initialized = true;
		}

		public async Task<string> Login()
		{
			await Initialize();

			// setup a local http server to listen for the result of the login
			using HttpListener http = new HttpListener();
			Uri uri = new Uri(_redirectUri);
			// build the url the server should be hosted at
			string prefix = $"{uri.Scheme}{Uri.SchemeDelimiter}{uri.Authority}/";
			http.Prefixes.Add(prefix);
			http.Start();

			// generate the appropriate codes we need to login
			AuthorizeState loginState = await _oidcClient.PrepareLoginAsync();
			// start the user browser
			OpenBrowser(loginState.StartUrl);

			string responseData = await ProcessHttpRequest(http);

			// parse the returned url for the tokens needed to complete the login
			IdentityModel.OidcClient.LoginResult loginResult = await _oidcClient.ProcessResponseAsync(responseData, loginState);

			http.Stop();

			if (loginResult.IsError)
				throw new LoginFailedException("Failed to login due to error: " + loginResult.Error);

			_refreshToken = loginResult.RefreshToken;
			_accessToken = loginResult.AccessToken;
			_tokenExpiry = loginResult.AccessTokenExpiration;

			return _refreshToken;
		}

		private async Task<string> ProcessHttpRequest(HttpListener http)
		{
			HttpListenerContext context = await http.GetContextAsync();
			string responseData;
			if (context.Request.HttpMethod == "GET")
			{
				responseData = context.Request.RawUrl;

			}
			else if (context.Request.HttpMethod == "POST")
			{
				var request = context.Request;
				if (request.ContentType != null && !request.ContentType.Equals("application/x-www-form-urlencoded", StringComparison.OrdinalIgnoreCase))
				{
					// we do not support url encoded return types
					context.Response.StatusCode = 415;
					return null;
				}
				
				// attempt to parse the body

				// if there is no body we can not handle the post
				if (!context.Request.HasEntityBody)
				{
					context.Response.StatusCode = 415;
					return null;
				}

				await using Stream body = request.InputStream;
				using StreamReader reader = new StreamReader(body, request.ContentEncoding);
				responseData = reader.ReadToEnd();
			}
			else
			{
				// if we receive any other http method something is very odd. Tell them to use a different method.
				context.Response.StatusCode = 415;
				return null;
			}

			// generate a simple http page to show the user
			HttpListenerResponse response = context.Response;
			string httpPage = "<html><head><meta http-equiv='refresh' content='10></head><body>Please close this browser and return to UnrealGameSync.</body></html>";
			byte[] buffer = Encoding.UTF8.GetBytes(httpPage);
			response.ContentLength64 = buffer.Length;
			Stream responseOutput = response.OutputStream;
			await responseOutput.WriteAsync(buffer, 0, buffer.Length);
			responseOutput.Close();

			return responseData;
		}

		private void OpenBrowser(string url)
		{
			try
			{
				Process.Start(url);
			}
			catch
			{
				// hack because of this: https://github.com/dotnet/corefx/issues/10361
				if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
				{
					url = url.Replace("&", "^&");
					Process.Start(new ProcessStartInfo("cmd", $"/c start {url}") { CreateNoWindow = true });
				}
				else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
				{
					Process.Start("xdg-open", url);
				}
				else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
				{
					Process.Start("open", url);
				}
				else
				{
					throw;
				}
			}
		}


		private async Task<string> DoRefreshToken(string inRefreshToken)
		{
			await Initialize();

			// use the refresh token to acquire a new access token
			RefreshTokenResult refreshTokenResult = await _oidcClient.RefreshTokenAsync(inRefreshToken);

			_refreshToken = refreshTokenResult.RefreshToken;
			_accessToken = refreshTokenResult.AccessToken;
			_tokenExpiry = refreshTokenResult.AccessTokenExpiration;

			return _accessToken;
		}

		private async Task<DiscoveryDocumentResponse> GetDiscoveryDocument()
		{
			string discoUrl = $"{_authorityUri}/.well-known/openid-configuration";

			HttpClient client = new HttpClient();
			DiscoveryDocumentResponse disco = await client.GetDiscoveryDocumentAsync(new DiscoveryDocumentRequest
			{
				Address = discoUrl,
				Policy =
				{
					ValidateEndpoints = false
				}
			});

			if (disco.IsError) throw new Exception(disco.Error);

			return disco;
		}


		public async Task<string> GetAccessToken()
		{
			if (string.IsNullOrEmpty(_refreshToken))
				throw new NotLoggedInException();

			// if the token is valid for another few minutes we can use it
			// we avoid using a token that is about to expire to make sure we can finish the call we expect to do with it before it expires
			if (!string.IsNullOrEmpty(_accessToken) && _tokenExpiry.AddMinutes(2) > DateTime.Now)
			{
				return _accessToken;
			}

			return await DoRefreshToken(_refreshToken);
		}

		public OidcStatus GetStatusForProvider()
		{
			if (string.IsNullOrEmpty(_refreshToken))
				return OidcStatus.NotLoggedIn;

			if (string.IsNullOrEmpty(_accessToken))
				return OidcStatus.TokenRefreshRequired;

			if (_tokenExpiry < DateTime.Now)
				return OidcStatus.TokenRefreshRequired;

			return OidcStatus.Connected;
		}

		public void SetRefreshToken(string inRefreshToken)
		{
			_refreshToken = inRefreshToken;
		}
	}

	internal class LoginFailedException : Exception
	{
		public LoginFailedException(string message) : base(message)
		{
		}
	}

	internal class NotLoggedInException : Exception
	{
	}
}