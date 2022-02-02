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
        private readonly IOptionsMonitor<ServiceCredentialSettings> _settings;
        private readonly ClientCredentialOAuthAuthenticator? _authenticator;

        public ServiceCredentials(IOptionsMonitor<ServiceCredentialSettings> settings, ISecretResolver secretResolver)
        {
            _settings = settings;
            if (!string.IsNullOrEmpty(settings.CurrentValue.OAuthLoginUrl))
            {
                string? clientId = secretResolver.Resolve(settings.CurrentValue.OAuthClientId);
                if (string.IsNullOrEmpty(clientId))
                    throw new ArgumentException("ClientId must be set when using a service credential");
                string? clientSecret = secretResolver.Resolve(settings.CurrentValue.OAuthClientSecret);
                if (string.IsNullOrEmpty(clientSecret))
                    throw new ArgumentException("ClientSecret must be set when using a service credential");
                _authenticator = new ClientCredentialOAuthAuthenticator(settings.CurrentValue.OAuthLoginUrl, clientId, clientSecret, settings.CurrentValue.OAuthScope);
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
