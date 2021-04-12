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

namespace UnrealGameSync
{
	public class OIDCTokenManager
	{
		private UserSettings Settings;
		private readonly Dictionary<string, OIDCTokenClient> TokenClients = new Dictionary<string, OIDCTokenClient>();

		public Dictionary<string, ProviderInfo> Providers { get; }

		private OIDCTokenManager(UserSettings InSettings, Dictionary<string, ProviderInfo> InProviders)
		{
			Settings = InSettings;
			Providers = InProviders;

			Dictionary<string, string> RefreshTokens = new Dictionary<string, string>();
			foreach (KeyValuePair<string, string> Pair in Settings.ProviderToRefreshTokens)
			{
				string ProviderIdentifier = Pair.Key;
				try
				{
					byte[] KeyBytes = Convert.FromBase64String(Pair.Value);
					byte[] UnencryptedBytes = ProtectedData.Unprotect(KeyBytes, null, DataProtectionScope.CurrentUser);
					RefreshTokens.TryAdd(ProviderIdentifier, Encoding.ASCII.GetString(UnencryptedBytes));
				}
				catch (Exception)
				{
					// ignore any invalid data in our stored refresh tokens, we will simply prompt the user to login again
				}
			}

			foreach (KeyValuePair<string, ProviderInfo> ProviderPair in InProviders)
			{
				ProviderInfo Provider = ProviderPair.Value;

				OIDCTokenClient TokenClient = new OIDCTokenClient(new Uri(Provider.ServerUri), Provider.ClientId, Provider.RedirectUri);

				string RefreshToken;
				if (RefreshTokens.TryGetValue(Provider.Identifier, out RefreshToken))
				{
					TokenClient.SetRefreshToken(RefreshToken);
				}
				TokenClients.Add(Provider.Identifier, TokenClient);
			}
		}

		public bool HasUnfinishedLogin()
		{
			return TokenClients.Any(Pair => Pair.Value.GetStatusForProvider() == OIDCStatus.NotLoggedIn);
		}

		internal static OIDCTokenManager CreateFromConfigFile(UserSettings Settings, List<DetectProjectSettingsTask> ConfigFiles)
		{
			// join the provider configuration from all projects
			Dictionary<string, ProviderInfo> Providers = new Dictionary<string, ProviderInfo>();
			foreach (DetectProjectSettingsTask DetectProjectSettingsTask in ConfigFiles)
			{
				if(DetectProjectSettingsTask == null)
				{
					continue;
				}

				ConfigFile ConfigFile = DetectProjectSettingsTask.LatestProjectConfigFile;
				if(ConfigFile == null)
				{
					continue;
				}

				ConfigSection ProviderSection = ConfigFile.FindSection("OIDCProvider");
				if (ProviderSection == null)
				{
					continue;
				}

				string[] ProviderValues = ProviderSection.GetValues("Provider", (string[]) null);
				foreach (ConfigObject Provider in ProviderValues.Select(s => new ConfigObject(s)).ToList())
				{
					string Identifier = Provider.GetValue("Identifier");
					string ServerUri = Provider.GetValue("ServerUri");
					string ClientId = Provider.GetValue("ClientId");
					string DisplayName = Provider.GetValue("DisplayName");
					string RedirectUri = Provider.GetValue("RedirectUri");

					// we might get a provider with the same identifier from another project, in which case we only keep the first one
					Providers.TryAdd(Identifier, new ProviderInfo(Identifier, ServerUri, ClientId, DisplayName, RedirectUri));
				}
			}

			if (Providers.Count == 0)
				return null;
			return new OIDCTokenManager(Settings, Providers);
		}

		public async Task Login(string ProviderIdentifier)
		{
			OIDCTokenClient TokenClient = TokenClients[ProviderIdentifier];

			string RefreshToken = await TokenClient.Login();
			if (!string.IsNullOrEmpty(RefreshToken))
			{
				// if we got a refresh token we store that for future use
				byte[] RefreshTokenBytes = Encoding.ASCII.GetBytes(RefreshToken);
				byte[] EncryptedBytes = ProtectedData.Protect(RefreshTokenBytes, null, DataProtectionScope.CurrentUser);

				Settings.ProviderToRefreshTokens[ProviderIdentifier] = Convert.ToBase64String(EncryptedBytes);
			}


		}

		public Task<string> GetAccessToken(string ProviderIdentifier)
		{
			return TokenClients[ProviderIdentifier].GetAccessToken();
		}

		public OIDCStatus GetStatusForProvider(string ProviderIdentifier)
		{
			return TokenClients[ProviderIdentifier].GetStatusForProvider();
		}

		public class ProviderInfo
		{
			public string Identifier { get; }
			public string ServerUri { get; }
			public string ClientId { get; }
			public string DisplayName { get; }
			public string RedirectUri { get; }

			public ProviderInfo(string InIdentifier, string InServerUri, string InClientId, string InDisplayName, string InRedirectUri)
			{
				Identifier = InIdentifier; 
				ServerUri = InServerUri;
				ClientId = InClientId;
				DisplayName = InDisplayName;
				RedirectUri = InRedirectUri;
			}
		}
	}


	public enum OIDCStatus
	{
		Connected,
		NotLoggedIn,
		TokenRefreshRequired
	}

	class OIDCTokenClient
	{
		private readonly Uri AuthorityUri;
		private readonly string ClientId;
		private readonly string RedirectUri;
		private readonly string Scopes;

		private OidcClient OidcClient;

		private string RefreshToken;
		private string AccessToken;
		private DateTime TokenExpiry;

		private bool Initialized = false;


		public OIDCTokenClient(Uri InAuthorityUri, string InClientId, string InRedirectUri, string InScopes = "openid profile offline_access")
		{
			AuthorityUri = InAuthorityUri;
			ClientId = InClientId;
			RedirectUri = InRedirectUri;
			Scopes = InScopes;
		}

		private async Task Initialize()
		{
			if (Initialized)
				return;

			OidcClientOptions Options = new OidcClientOptions
			{
				Authority = AuthorityUri.ToString(),
				Policy = new Policy { Discovery = new DiscoveryPolicy { Authority = AuthorityUri.ToString() } },
				ClientId = ClientId,
				Scope = Scopes,
				FilterClaims = false,
				RedirectUri = RedirectUri,
				Flow = OidcClientOptions.AuthenticationFlow.AuthorizationCode,
				ResponseMode = OidcClientOptions.AuthorizeResponseMode.Redirect
			};

			// we need to fetch the discovery document ourselves to support OIDC Authorities which have a subresource for it
			// with Okta has for authorization servers for instance.
			DiscoveryDocumentResponse DiscoveryDocument = await GetDiscoveryDocument();
			Options.ProviderInformation = new ProviderInformation
			{
				IssuerName = DiscoveryDocument.Issuer,
				KeySet = DiscoveryDocument.KeySet,

				AuthorizeEndpoint = DiscoveryDocument.AuthorizeEndpoint,
				TokenEndpoint = DiscoveryDocument.TokenEndpoint,
				EndSessionEndpoint = DiscoveryDocument.EndSessionEndpoint,
				UserInfoEndpoint = DiscoveryDocument.UserInfoEndpoint,
				TokenEndPointAuthenticationMethods = DiscoveryDocument.TokenEndpointAuthenticationMethodsSupported
			};

			OidcClient = new OidcClient(Options);
			Initialized = true;
		}

		public async Task<string> Login()
		{
			await Initialize();

			// setup a local http server to listen for the result of the login
			using HttpListener Http = new HttpListener();
			Uri Uri = new Uri(RedirectUri);
			// build the url the server should be hosted at
			string Prefix = $"{Uri.Scheme}{Uri.SchemeDelimiter}{Uri.Authority}/";
			Http.Prefixes.Add(Prefix);
			Http.Start();

			// generate the appropriate codes we need to login
			AuthorizeState LoginState = await OidcClient.PrepareLoginAsync();
			// start the user browser
			OpenBrowser(LoginState.StartUrl);

			string ResponseData = await ProcessHttpRequest(Http);

			// parse the returned url for the tokens needed to complete the login
			IdentityModel.OidcClient.LoginResult LoginResult = await OidcClient.ProcessResponseAsync(ResponseData, LoginState);

			Http.Stop();

			if (LoginResult.IsError)
				throw new LoginFailedException("Failed to login due to error: " + LoginResult.Error);

			RefreshToken = LoginResult.RefreshToken;
			AccessToken = LoginResult.AccessToken;
			TokenExpiry = LoginResult.AccessTokenExpiration;

			return RefreshToken;
		}

		private async Task<string> ProcessHttpRequest(HttpListener Http)
		{
			HttpListenerContext Context = await Http.GetContextAsync();
			string ResponseData;
			if (Context.Request.HttpMethod == "GET")
			{
				ResponseData = Context.Request.RawUrl;

			}
			else if (Context.Request.HttpMethod == "POST")
			{
				var Request = Context.Request;
				if (Request.ContentType != null && !Request.ContentType.Equals("application/x-www-form-urlencoded", StringComparison.OrdinalIgnoreCase))
				{
					// we do not support url encoded return types
					Context.Response.StatusCode = 415;
					return null;
				}
				
				// attempt to parse the body

				// if there is no body we can not handle the post
				if (!Context.Request.HasEntityBody)
				{
					Context.Response.StatusCode = 415;
					return null;
				}

				await using Stream Body = Request.InputStream;
				using StreamReader Reader = new StreamReader(Body, Request.ContentEncoding);
				ResponseData = Reader.ReadToEnd();
			}
			else
			{
				// if we receive any other http method something is very odd. Tell them to use a different method.
				Context.Response.StatusCode = 415;
				return null;
			}

			// generate a simple http page to show the user
			HttpListenerResponse Response = Context.Response;
			string HttpPage = "<html><head><meta http-equiv='refresh' content='10></head><body>Please close this browser and return to UnrealGameSync.</body></html>";
			byte[] Buffer = Encoding.UTF8.GetBytes(HttpPage);
			Response.ContentLength64 = Buffer.Length;
			Stream ResponseOutput = Response.OutputStream;
			await ResponseOutput.WriteAsync(Buffer, 0, Buffer.Length);
			ResponseOutput.Close();

			return ResponseData;
		}

		private void OpenBrowser(string Url)
		{
			try
			{
				Process.Start(Url);
			}
			catch
			{
				// hack because of this: https://github.com/dotnet/corefx/issues/10361
				if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
				{
					Url = Url.Replace("&", "^&");
					Process.Start(new ProcessStartInfo("cmd", $"/c start {Url}") { CreateNoWindow = true });
				}
				else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
				{
					Process.Start("xdg-open", Url);
				}
				else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
				{
					Process.Start("open", Url);
				}
				else
				{
					throw;
				}
			}
		}


		private async Task<string> DoRefreshToken(string InRefreshToken)
		{
			await Initialize();

			// use the refresh token to acquire a new access token
			RefreshTokenResult RefreshTokenResult = await OidcClient.RefreshTokenAsync(InRefreshToken);

			RefreshToken = RefreshTokenResult.RefreshToken;
			AccessToken = RefreshTokenResult.AccessToken;
			TokenExpiry = RefreshTokenResult.AccessTokenExpiration;

			return AccessToken;
		}

		private async Task<DiscoveryDocumentResponse> GetDiscoveryDocument()
		{
			string DiscoUrl = $"{AuthorityUri}/.well-known/openid-configuration";

			HttpClient Client = new HttpClient();
			DiscoveryDocumentResponse Disco = await Client.GetDiscoveryDocumentAsync(new DiscoveryDocumentRequest
			{
				Address = DiscoUrl,
				Policy =
				{
					ValidateEndpoints = false
				}
			});

			if (Disco.IsError) throw new Exception(Disco.Error);

			return Disco;
		}


		public async Task<string> GetAccessToken()
		{
			if (string.IsNullOrEmpty(RefreshToken))
				throw new NotLoggedInException();

			// if the token is valid for another few minutes we can use it
			// we avoid using a token that is about to expire to make sure we can finish the call we expect to do with it before it expires
			if (!string.IsNullOrEmpty(AccessToken) && TokenExpiry.AddMinutes(2) > DateTime.Now)
			{
				return AccessToken;
			}

			return await DoRefreshToken(RefreshToken);
		}

		public OIDCStatus GetStatusForProvider()
		{
			if (string.IsNullOrEmpty(RefreshToken))
				return OIDCStatus.NotLoggedIn;

			if (string.IsNullOrEmpty(AccessToken))
				return OIDCStatus.TokenRefreshRequired;

			if (TokenExpiry < DateTime.Now)
				return OIDCStatus.TokenRefreshRequired;

			return OIDCStatus.Connected;
		}

		public void SetRefreshToken(string InRefreshToken)
		{
			RefreshToken = InRefreshToken;
		}
	}

	internal class LoginFailedException : Exception
	{
		public LoginFailedException(string Message) : base(Message)
		{
		}
	}

	internal class NotLoggedInException : Exception
	{
	}
}