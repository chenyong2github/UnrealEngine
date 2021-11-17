// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Threading.Tasks;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Authorization;
using Microsoft.Extensions.Options;

namespace Jupiter
{
    // verifies that you have access to a namespace by checking if you have a corresponding claim to that namespace
    public class NamespaceAuthorizationHandler : AuthorizationHandler<NamespaceAccessRequirement, NamespaceId>
    {
        private readonly IOptionsMonitor<AuthorizationSettings> _authorizationSettings;

        public NamespaceAuthorizationHandler(IOptionsMonitor<AuthorizationSettings> authorizationSettings)
        {
            _authorizationSettings = authorizationSettings;
        }

        protected override Task HandleRequirementAsync(AuthorizationHandlerContext context, NamespaceAccessRequirement requirement,
            NamespaceId namespaceName)
        {
            if (context.User.HasClaim(claim => claim.Type == "AllNamespaces"))
            {
                context.Succeed(requirement);
                return Task.CompletedTask;
            }

            if (_authorizationSettings.CurrentValue.NamespaceToClaim.TryGetValue(namespaceName.ToString(), out string? expectedClaim))
            {
                // if expected claim is * then everyone is allowed to use the namespace
                if (expectedClaim == "*")
                {
                    context.Succeed(requirement);
                }

                if (context.User.HasClaim(claim => claim.Type == expectedClaim))
                {
                    context.Succeed(requirement);
                }
            }


            return Task.CompletedTask;
        }
    }

    public class AuthorizationSettings
    {
        [Required] public Dictionary<string, string> NamespaceToClaim { get; set; } = null!;
    }

    public class NamespaceAccessRequirement : IAuthorizationRequirement
    {
        public const string Name = "NamespaceAccess";
    }
}
