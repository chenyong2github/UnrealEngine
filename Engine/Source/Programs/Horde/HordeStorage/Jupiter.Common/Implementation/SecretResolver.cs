// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Threading.Tasks;
using Amazon.SecretsManager;
using Amazon.SecretsManager.Model;
using Microsoft.Extensions.DependencyInjection;

namespace Jupiter.Common.Implementation
{
    public interface ISecretResolver
    {
        string? Resolve(string value);
    }

    public class SecretResolver : ISecretResolver
    {
        private readonly IServiceProvider _serviceProvider;

        public SecretResolver(IServiceProvider serviceProvider)
        {
            _serviceProvider = serviceProvider;
        }

        public string? Resolve(string value)
        {
            int providerSeparator = value.IndexOf("!", StringComparison.Ordinal);
            if (providerSeparator != -1)
            {
                string providerId = value.Substring(0, providerSeparator);
                string providerPath = value.Substring(providerSeparator + 1);
                return ResolveUsingProvider(providerId, providerPath, value);
            }

            return value;
        }

        private string? ResolveUsingProvider(string providerId, string providerPath, string originalValue)
        {
            switch (providerId.ToLowerInvariant())
            {
                case "aws":
                    return ResolveAWSSecret(providerPath);
                default:
                    // no provider matches so just return the original value
                    return originalValue;
            }
        }

        private string? ResolveAWSSecret(string providerPath)
        {
            int keySeparator = providerPath.IndexOf("|");
            string? key = null;
            string arn = providerPath;
            if (keySeparator != -1)
            {
                arn = providerPath.Substring(0, keySeparator);
                key = providerPath.Substring(keySeparator + 1);
            }

            IAmazonSecretsManager? secretsManager = _serviceProvider.GetService<IAmazonSecretsManager>();
            if (secretsManager == null)
                throw new Exception($"Unable to get AWSSecretsManager when resolving aws secret resource: {providerPath}");

            Task<GetSecretValueResponse>? response = secretsManager.GetSecretValueAsync(new GetSecretValueRequest
            {
                SecretId = arn,
            });

            string secretValue = response.Result.SecretString;
            if (key == null)
                return secretValue;

            Dictionary<string, string>? keyCollection = JsonSerializer.Deserialize<Dictionary<string, string>>(secretValue);

            if (keyCollection == null)
                throw new Exception($"Unable to deserialize secret to a json payload for path: {providerPath}");
            if (keyCollection.TryGetValue(key, out string? s))
                return s;

            throw new Exception($"Unable to find key {key} in blob returned for secret {arn}");
        }
    }
}
