// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Jupiter.Common.Implementation;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;
using RestSharp.Authenticators;

namespace Horde.Storage.Implementation
{
    public interface IServiceCredentials
    {
        IAuthenticator? GetAuthenticator();

        string? GetToken();
    }

    public class ServiceCredentials : IServiceCredentials
    {
        private readonly ClientCredentialOAuthAuthenticator? _authenticator;

        public ServiceCredentials(IOptionsMonitor<ServiceCredentialSettings> settingsMonitor, ISecretResolver secretResolver)
        {
            ServiceCredentialSettings settings = settingsMonitor.CurrentValue;
            if (!string.IsNullOrEmpty(settings.OAuthLoginUrl))
            {
                string? clientId = secretResolver.Resolve(settings.OAuthClientId);
                if (string.IsNullOrEmpty(clientId))
                    throw new ArgumentException("ClientId must be set when using a service credential");
                string? clientSecret = secretResolver.Resolve(settings.OAuthClientSecret);
                if (string.IsNullOrEmpty(clientSecret))
                    throw new ArgumentException("ClientSecret must be set when using a service credential");
                _authenticator = new ClientCredentialOAuthAuthenticator(settings.OAuthLoginUrl, clientId, clientSecret, settings.OAuthScope);
            }
        }

        public IAuthenticator? GetAuthenticator()
        {
            return _authenticator;
        }

        public string? GetToken()
        {
            return _authenticator?.Authenticate();
        }
    }

}
