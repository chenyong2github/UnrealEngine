// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.ComponentModel;
using System.Globalization;
using Newtonsoft.Json;

namespace Jupiter.Implementation
{
    [JsonConverter(typeof(BucketIdJsonConverter))]
    [TypeConverter(typeof(BucketIdTypeConverter))]

    public readonly struct BucketId: IEquatable<BucketId>
    {
        public BucketId(string bucketId)
        {
             _text = bucketId;
 
            if (_text.Length == 0)
            {
				throw new ArgumentException("Buckets must have at least one character.");
            }
 
            const int MaxLength = 64;
            if (_text.Length > MaxLength)
            {
                throw new ArgumentException($"Buckets may not be longer than {MaxLength} characters");
            }
 
            for (int Idx = 0; Idx < _text.Length; Idx++)
            {
                if (!IsValidCharacter(_text[Idx]))
                {
                    throw new ArgumentException($"{_text} is not a valid bucket id");
                }
            }
        }

        static bool IsValidCharacter(char c)
        {
            if (c >= 'a' && c <= 'z')
            {
                return true;
            }
            if (c >= '0' && c <= '9')
            {
                return true;
            }
            if (c == '-' || c == '_' || c == '.')
            {
                return true;
            }
            return false;
        }

        readonly string _text;

        public bool Equals(BucketId other)
        {
            return string.Equals(_text , other._text, StringComparison.Ordinal);
        }

        public override bool Equals(object? obj)
        {
            return obj is BucketId other && Equals(other);
        }

        public override int GetHashCode()
        {
            return _text.GetHashCode();
        }

        public override string ToString()
        {
            return _text;
        }

        public static bool operator ==(BucketId left, BucketId right)
        {
            return left.Equals(right);
        }
 
        public static bool operator !=(BucketId left, BucketId right)
        {
            return !left.Equals(right);
        }
    }

    public class BucketIdJsonConverter: JsonConverter<BucketId>
    {
        public override void WriteJson(JsonWriter writer, BucketId value, JsonSerializer serializer)
        {
            writer.WriteValue(value!.ToString());
        }

        public override BucketId ReadJson(JsonReader reader, Type objectType, BucketId existingValue, bool hasExistingValue, Newtonsoft.Json.JsonSerializer serializer)
        {
            string? s = (string?)reader.Value;

            return new BucketId(s!);
        }
    }

    public sealed class BucketIdTypeConverter : TypeConverter
    {
        /// <inheritdoc/>
        public override bool CanConvertFrom(ITypeDescriptorContext? context, Type sourceType)
        {
            if (sourceType == typeof(string))
                return true;

            return base.CanConvertFrom(context, sourceType);
        }
 
        public override object? ConvertFrom(ITypeDescriptorContext? context, CultureInfo? culture, object value)  
        {
            if (value is string s)
            {
                return new BucketId(s);
            }

            return base.ConvertFrom(context, culture, value);  
        }
    }
}

