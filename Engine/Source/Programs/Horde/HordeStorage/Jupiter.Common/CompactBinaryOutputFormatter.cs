// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc.Formatters;
using Microsoft.Net.Http.Headers;
using Serilog;

namespace Jupiter
{
    public class CompactBinaryOutputFormatter : OutputFormatter
    {
        private readonly ILogger _logger = Log.ForContext<CompactBinaryOutputFormatter>();

        public CompactBinaryOutputFormatter()
        {
            SupportedMediaTypes.Add(MediaTypeHeaderValue.Parse(CustomMediaTypeNames.UnrealCompactBinary));
        }

        public override async Task WriteResponseBodyAsync(OutputFormatterWriteContext context)
        {
            if (context.Object == null)
                return;

            CompactBinaryWriter compactBinaryWriter = ConvertToCompactBinary(context.Object);
            
            HttpResponse response = context.HttpContext.Response;
            byte[] value = compactBinaryWriter.Save();

            response.ContentLength = value.LongLength;
            await response.Body.WriteAsync(value);
        }

        private CompactBinaryWriter ConvertToCompactBinary(object o)
        {
            CompactBinaryWriter writer = new CompactBinaryWriter();
            writer.BeginObject();
            
            foreach (PropertyInfo propertyInfo in o.GetType().GetProperties())
            {
                // convert to camelCase, looks better then PascalCase which is the default for a C# object
                string name = propertyInfo.Name.Substring(0,1).ToLower() + propertyInfo.Name.Substring(1);
                Type propertyType = propertyInfo.PropertyType;
                Type? innerType = Nullable.GetUnderlyingType(propertyType);
                if (innerType != null)
                {
                    propertyType = innerType;
                }

                Type[] propertyInterfaces = propertyType.GetInterfaces();

                object? value = propertyInfo.GetValue(o);
                if (value == null)
                {
                    writer.AddNull(name);
                }
                else if (propertyType == typeof(BlobIdentifier))
                {
                    BlobIdentifier blob = (BlobIdentifier) value!;
                    writer.AddBinaryAttachment(blob, name);
                }
                else if (propertyType == typeof(string))
                {
                    string s = (string) value!;
                    writer.AddString(s, name);
                }
                else if (propertyType == typeof(DateTime))
                {
                    DateTime dt = (DateTime) value!; 
                    writer.AddDateTime(dt, name);
                }
                else if (propertyType == typeof(int))
                {
                    int i = (int) value!;
                    // writer.AddInteger(i, name);
                }
                else if (propertyType.IsArray)
                {
                    Type elementType = propertyType.GetElementType()!;

                    if (elementType == typeof(BlobIdentifier))
                    {
                        BlobIdentifier[] blobs = (BlobIdentifier[])value;
                        writer.AddUniformArray(blobs, CompactBinaryFieldType.BinaryAttachment, name);
                    }
                    else if (elementType == typeof(string))
                    {
                        string[] strings = (string[])value;
                        writer.AddUniformArray(strings, CompactBinaryFieldType.String, name);
                    }
                    else if (elementType == typeof(byte))
                    {
                        byte[] data = (byte[])value;
                        writer.AddBinary(data, name);
                    }
                    else
                    {
                        throw new NotImplementedException($"Type {elementType} not support for arrays when converting to compact binary for field \"{name}\"");
                    }
                }
                else if (propertyType.IsGenericType && propertyType.GetGenericTypeDefinition() == typeof(IDictionary<,>))
                {
                    // TODO: Serialize dictionaries as objects
                }
                else if (propertyInfo.PropertyType.IsClass)
                {
                    //writer.AddObject(name, ConvertToCompactBinary(value!));
                }
                else
                {
                    throw new NotImplementedException($"Unsupported type in compact binary conversion: {propertyType.Name} for field \"{name}\"");
                }
            }

            writer.EndObject();
            return writer;
        }
    }
}
