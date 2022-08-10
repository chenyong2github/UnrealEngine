// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using IdentityModel.Client;
using IdentityModel.OidcClient;
using IdentityModel.OidcClient.Results;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace EpicGames.OIDC
{
	public class OidcTokenManager
	{
		private readonly Dictionary<string, OidcTokenClient> _tokenClients = new Dictionary<string, OidcTokenClient>();
		private readonly ITokenStore _tokenStore;

		public OidcTokenManager(IServiceProvider provider, IOptionsMonitor<OidcTokenOptions> settings, ITokenStore tokenStore, List<string>? allowedProviders = null)
		{
			_tokenStore = tokenStore;

			Dictionary<string, string> refreshTokens = new Dictionary<string, string>();
			List<string> providerKeys = settings.CurrentValue.Providers.Keys.Where(k => allowedProviders == null || allowedProviders.Contains(k)).ToList();
			foreach (string oidcProvider in providerKeys)
			{
				if (tokenStore.TryGetRefreshToken(oidcProvider, out string? refreshToken))
				{
					refreshTokens.TryAdd(oidcProvider, refreshToken);
				}
			}

			Providers = settings.CurrentValue.Providers;
			foreach ((string key, ProviderInfo providerInfo) in settings.CurrentValue.Providers)
			{
				if (providerKeys != null && !providerKeys.Contains(key))
				{
					continue;
				}

				OidcTokenClient tokenClient = ActivatorUtilities.CreateInstance<OidcTokenClient>(provider, providerInfo);

				if (refreshTokens.TryGetValue(key, out string? refreshToken))
				{
					tokenClient.SetRefreshToken(refreshToken);
				}

				_tokenClients.Add(key, tokenClient);
			}
		}

		public static OidcTokenManager CreateTokenManager(IConfiguration providerConfiguration, ITokenStore tokenStore, List<string> allowedProviders)
		{
			return new OidcTokenManager(providerConfiguration, tokenStore, allowedProviders);
		}

		private OidcTokenManager(IConfiguration providerConfiguration, ITokenStore tokenStore, List<string>? allowedProviders = null)
		{
			_tokenStore = tokenStore;

			Dictionary<string, string> refreshTokens = new Dictionary<string, string>();

			OidcTokenOptions options = new OidcTokenOptions();
			providerConfiguration.Bind(options);

			List<string> providerKeys = options.Providers.Keys.Where(k => allowedProviders == null || allowedProviders.Contains(k)).ToList();

			foreach (string oidcProvider in providerKeys)
			{
				if (tokenStore.TryGetRefreshToken(oidcProvider, out string? refreshToken))
				{
					refreshTokens.TryAdd(oidcProvider, refreshToken);
				}
			}

			Providers = options.Providers;
			foreach ((string key, ProviderInfo providerInfo) in Providers)
			{
				if (providerKeys != null && !providerKeys.Contains(key))
				{
					continue;
				}

				OidcTokenClient tokenClient = new OidcTokenClient(providerInfo);

				if (refreshTokens.TryGetValue(key, out string? refreshToken))
				{
					tokenClient.SetRefreshToken(refreshToken);
				}

				_tokenClients.Add(key, tokenClient);
			}
		}

		public Dictionary<string, ProviderInfo> Providers { get; }

		public bool HasUnfinishedLogin()
		{
			return _tokenClients.Any(pair => pair.Value.GetStatusForProvider() == OidcStatus.NotLoggedIn);
		}

		public async Task<OidcTokenInfo> Login(string providerIdentifier)
		{
			OidcTokenClient tokenClient = _tokenClients[providerIdentifier];

			OidcTokenInfo tokenInfo = await tokenClient.Login();
			
			if (!String.IsNullOrEmpty(tokenInfo.RefreshToken))
			{
				_tokenStore.AddRefreshToken(providerIdentifier, tokenInfo.RefreshToken);
			}

			return tokenInfo;
		}

		public Task<OidcTokenInfo> GetAccessToken(string providerIdentifier)
		{
			return _tokenClients[providerIdentifier].GetAccessToken();
		}

		public OidcStatus GetStatusForProvider(string providerIdentifier)
		{
			return _tokenClients[providerIdentifier].GetStatusForProvider();
		}
	}

	public enum OidcStatus
	{
		Connected,
		NotLoggedIn,
		TokenRefreshRequired
	}

	public class OidcTokenInfo
	{
		public string? IdentityToken { get; set; }
		public string? RefreshToken { get; set; }
		public string? AccessToken { get; set; }
		public DateTimeOffset TokenExpiry { get; set; }

		public bool IsValid => IdentityToken != null && RefreshToken != null && AccessToken != null;
	};

	class OidcTokenClient
	{
		private readonly ProviderInfo _providerInfo;
		private readonly Uri _authorityUri;
		private readonly string _clientId;
		private readonly Uri _redirectUri;
		private readonly string _scopes;

		private OidcClient? _oidcClient;

		private string? _refreshToken;
		private string? _accessToken;
		private DateTimeOffset _tokenExpiry;

		private bool _initialized = false;

		private readonly ILogger<OidcTokenClient>? _logger;

		public OidcTokenClient(ILogger<OidcTokenClient> logger, ProviderInfo providerInfo)
		{
			_logger = logger;
			_providerInfo = providerInfo;

			_authorityUri = providerInfo.ServerUri;
			_clientId = providerInfo.ClientId;
			_redirectUri = providerInfo.RedirectUri;
			_scopes = providerInfo.Scopes;
		}

		public OidcTokenClient(ProviderInfo providerInfo)
		{
			_providerInfo = providerInfo;
			_logger = null;

			_authorityUri = providerInfo.ServerUri;
			_clientId = providerInfo.ClientId;
			_redirectUri = providerInfo.RedirectUri;
			_scopes = providerInfo.Scopes;
		}

		private async Task Initialize()
		{
			if (_initialized)
			{
				return;
			}

			OidcClientOptions options = new OidcClientOptions
			{
				Authority = _authorityUri.ToString(),
				Policy = new Policy { Discovery = new DiscoveryPolicy { Authority = _authorityUri.ToString() } },
				ClientId = _clientId,
				Scope = _scopes,
				FilterClaims = false,
				RedirectUri = _redirectUri.ToString(),
				LoadProfile = _providerInfo.LoadClaimsFromUserProfile,
				//Flow = OidcClientOptions.AuthenticationFlow.AuthorizationCode,
				//ResponseMode = OidcClientOptions.AuthorizeResponseMode.Redirect
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

		public async Task<OidcTokenInfo> Login()
		{
			await Initialize();

			// setup a local http server to listen for the result of the login
			using HttpListener http = new HttpListener();
			Uri uri = _redirectUri;
			// build the url the server should be hosted at
			string prefix = $"{uri.Scheme}{Uri.SchemeDelimiter}{uri.Authority}/";
			http.Prefixes.Add(prefix);
			http.Start();

			// generate the appropriate codes we need to login
			AuthorizeState loginState = await _oidcClient!.PrepareLoginAsync();
			// start the user browser
			OpenBrowser(loginState.StartUrl);

			string? responseData = await ProcessHttpRequest(http);

			// parse the returned url for the tokens needed to complete the login
			LoginResult loginResult = await _oidcClient.ProcessResponseAsync(responseData, loginState);

			http.Stop();

			if (loginResult.IsError)
			{
				throw new LoginFailedException("Failed to login due to error: " + loginResult.Error);
			}

			_refreshToken = loginResult.RefreshToken;
			_accessToken = loginResult.AccessToken;
			_tokenExpiry = loginResult.AccessTokenExpiration;

			return new OidcTokenInfo
			{
				IdentityToken = loginResult.IdentityToken,
				RefreshToken = loginResult.RefreshToken,
				AccessToken = loginResult.AccessToken,
				TokenExpiry = loginResult.AccessTokenExpiration
			};
		}

		private static async Task<string?> ProcessHttpRequest(HttpListener http)
		{
			HttpListenerContext context = await http.GetContextAsync();
			string? responseData;
			switch (context.Request.HttpMethod)
			{
				case "GET":
					responseData = context.Request.RawUrl;
					break;
				case "POST":
				{
					HttpListenerRequest request = context.Request;
					if (request.ContentType != null && !request.ContentType.Equals("application/x-www-form-urlencoded",
						StringComparison.OrdinalIgnoreCase))
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
					responseData = await reader.ReadToEndAsync();
					break;
				}
				default:
					// if we receive any other http method something is very odd. Tell them to use a different method.
					context.Response.StatusCode = 415;
					return null;
			}

			// generate a simple http page to show the user
			HttpListenerResponse response = context.Response;
			string httpPage = String.Format("<html><head></head><body style=\"background-color: black; color: white; \"><div style=\"text-align: center; \"><img style=\"object-fit:contain; width: 40 %; height: 30 %; \" alt=\"Unreal Engine Logo\" src=\"{0}\" /></div><br /><br /><h1 style=\"margin:10%; text-align: center;\">Please close this browser to finish your login.<h1></body></html>",
				"data:image/png; base64,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");
			byte[] buffer = Encoding.UTF8.GetBytes(httpPage);
			response.ContentLength64 = buffer.Length;
			Stream responseOutput = response.OutputStream;
			await responseOutput.WriteAsync(buffer, 0, buffer.Length);
			responseOutput.Close();

			return responseData;
		}

		private void OpenBrowser(string url)
		{
			_logger?.LogInformation("Opening system browser to {Url}", url);

			try
			{
				Process.Start(url);
			}
			catch
			{
				// hack because of this: https://github.com/dotnet/corefx/issues/10361
				if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
				{
					url = url.Replace("&", "^&", StringComparison.InvariantCultureIgnoreCase);
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

		private async Task<OidcTokenInfo> DoRefreshToken(string inRefreshToken)
		{
			await Initialize();

			// use the refresh token to acquire a new access token
			RefreshTokenResult refreshTokenResult = await _oidcClient!.RefreshTokenAsync(inRefreshToken);

			if (refreshTokenResult.IsError)
			{
				if (refreshTokenResult.Error == "invalid_grant")
				{
					// the refresh token is no logger valid, resetting it and treating us as not logged in
					_refreshToken = null;
					throw new NotLoggedInException();
				}
				else
				{
					throw new Exception($"Error using the refresh token: {refreshTokenResult.Error} , details: {refreshTokenResult.ErrorDescription}");
				}
			}
			
			_refreshToken = refreshTokenResult.RefreshToken;
			_accessToken = refreshTokenResult.AccessToken;
			_tokenExpiry = refreshTokenResult.AccessTokenExpiration;

			return new OidcTokenInfo
			{
				IdentityToken = refreshTokenResult.IdentityToken,
				RefreshToken = refreshTokenResult.RefreshToken,
				AccessToken = refreshTokenResult.AccessToken,
				TokenExpiry = refreshTokenResult.AccessTokenExpiration
			};
		}

		private async Task<DiscoveryDocumentResponse> GetDiscoveryDocument()
		{
			string discoUrl = $"{_authorityUri}/.well-known/openid-configuration";

			using HttpClient client = new HttpClient();
			using DiscoveryDocumentRequest doc = new DiscoveryDocumentRequest
			{
				Address = discoUrl,
				Policy =
				{
					ValidateEndpoints = false
				}
			};
			DiscoveryDocumentResponse disco = await client.GetDiscoveryDocumentAsync(doc);

			if (disco.IsError)
			{
				throw new Exception(disco.Error);
			}

			return disco;
		}
		public async Task<OidcTokenInfo> GetAccessToken()
		{
			if (String.IsNullOrEmpty(_refreshToken))
			{
				throw new NotLoggedInException();
			}

			// if the token is valid for another few minutes we can use it
			// we avoid using a token that is about to expire to make sure we can finish the call we expect to do with it before it expires
			if (!String.IsNullOrEmpty(_accessToken) && _tokenExpiry.AddMinutes(2) > DateTime.Now)
			{
				return new OidcTokenInfo
				{
					RefreshToken = _refreshToken,
					AccessToken = _accessToken,
					TokenExpiry = _tokenExpiry
				};
			}

			return await DoRefreshToken(_refreshToken);
		}

		public OidcStatus GetStatusForProvider()
		{
			if (String.IsNullOrEmpty(_refreshToken))
			{
				return OidcStatus.NotLoggedIn;
			}

			if (String.IsNullOrEmpty(_accessToken))
			{
				return OidcStatus.TokenRefreshRequired;
			}

			if (_tokenExpiry < DateTime.Now)
			{
				return OidcStatus.TokenRefreshRequired;
			}

			return OidcStatus.Connected;
		}

		public void SetRefreshToken(string inRefreshToken)
		{
			_refreshToken = inRefreshToken;
		}
	}

	public class LoginFailedException : Exception
	{
		public LoginFailedException(string message) : base(message)
		{
		}
	}

	public class NotLoggedInException : Exception
	{
	}

	public class OidcTokenOptions
	{
		public Dictionary<string, ProviderInfo> Providers { get; set; } = new Dictionary<string, ProviderInfo>();
	}

	public class ProviderInfo
	{
		[Required] public Uri ServerUri { get; set; } = null!;

		[Required] public string ClientId { get; set; } = null!;

		[Required] public string DisplayName { get; set; } = null!;

		[Required] public Uri RedirectUri { get; set; } = null!;
		[Required] public bool LoadClaimsFromUserProfile { get; set; } = false;

		public string Scopes { get; set; } = "openid profile offline_access";
	}
}