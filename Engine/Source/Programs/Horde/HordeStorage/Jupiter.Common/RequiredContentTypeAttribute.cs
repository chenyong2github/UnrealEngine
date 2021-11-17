// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Linq;
using Microsoft.AspNetCore.Mvc.ActionConstraints;
using Microsoft.Extensions.Primitives;

namespace Jupiter
{
    public class RequiredContentTypeAttribute : Attribute, IActionConstraint
    {
        private readonly string _mediaTypeName;

        public RequiredContentTypeAttribute(string mediaTypeName)
        {
            _mediaTypeName = mediaTypeName;
        }

        public int Order
        {
            get { return 0; }
        }

        public bool Accept(ActionConstraintContext context)
        {
            StringValues contentTypeHeader = context.RouteContext.HttpContext.Request.Headers["Content-Type"];

            bool valid = contentTypeHeader.ToList().Contains(_mediaTypeName, StringComparer.InvariantCultureIgnoreCase);
            return valid;
        }
    }
}
