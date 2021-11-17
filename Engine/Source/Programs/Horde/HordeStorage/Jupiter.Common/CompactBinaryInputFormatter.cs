// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Mvc.Formatters;
using Microsoft.Net.Http.Headers;

namespace Jupiter
{
    public class CompactBinaryInputFormatter : InputFormatter
    {
        public CompactBinaryInputFormatter()
        {
            SupportedMediaTypes.Add(MediaTypeHeaderValue.Parse(CustomMediaTypeNames.UnrealCompactBinary));
        }

        public override async Task<InputFormatterResult> ReadRequestBodyAsync(InputFormatterContext context)
        {
            byte[] body = await RequestUtil.ReadRawBody(context.HttpContext.Request);
            CompactBinaryObject cb = CompactBinaryObject.Load(body);
            
            return InputFormatterResult.Success(cb.ToPoco(context.ModelType));
        }
    }
}
