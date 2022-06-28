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
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace EpicGames.OIDC
{
	public class OidcTokenManager
	{
		private readonly Dictionary<string, OidcTokenClient> _tokenClients = new Dictionary<string, OidcTokenClient>();
		private readonly ITokenStore _tokenStore;

		public OidcTokenManager(IServiceProvider provider, IOptionsMonitor<OidcTokenOptions> settings, ITokenStore tokenStore)
		{
			_tokenStore = tokenStore;

			Dictionary<string, string> refreshTokens = new Dictionary<string, string>();
			foreach (string oidcProvider in settings.CurrentValue.Providers.Keys)
			{
				if (tokenStore.TryGetRefreshToken(oidcProvider, out string? refreshToken))
				{
					refreshTokens.TryAdd(oidcProvider, refreshToken);
				}
			}

			Providers = settings.CurrentValue.Providers;
			foreach ((string key, ProviderInfo providerInfo) in settings.CurrentValue.Providers)
			{
				OidcTokenClient tokenClient = ActivatorUtilities.CreateInstance<OidcTokenClient>(provider, providerInfo);

				if (refreshTokens.TryGetValue(key, out string? refreshToken))
				{
					tokenClient.SetRefreshToken(refreshToken);
				}

				_tokenClients.Add(key, tokenClient);
			}
		}

		public OidcTokenManager(Dictionary<string, ProviderInfo> providers, ITokenStore tokenStore)
		{
			_tokenStore = tokenStore;

			Dictionary<string, string> refreshTokens = new Dictionary<string, string>();
			foreach (string oidcProvider in providers.Keys)
			{
				if (tokenStore.TryGetRefreshToken(oidcProvider, out string? refreshToken))
				{
					refreshTokens.TryAdd(oidcProvider, refreshToken);
				}
			}

			Providers = providers;
			foreach ((string key, ProviderInfo providerInfo) in Providers)
			{
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

			_authorityUri = providerInfo.ServerUri;
			_clientId = providerInfo.ClientId;
			_redirectUri = providerInfo.RedirectUri;
			_scopes = providerInfo.Scopes;
		}

		public OidcTokenClient(ProviderInfo providerInfo)
		{
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
			const string HttpPage = "<html><head></head><body><h1 style=\"text-align: center;\">Please close this browser to finish your login.<h1></body></html>";
			byte[] buffer = Encoding.UTF8.GetBytes(HttpPage);
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

		public string Scopes { get; set; } = "openid profile offline_access";
	}
}