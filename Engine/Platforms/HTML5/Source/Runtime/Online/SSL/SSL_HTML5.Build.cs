// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SSL_HTML5 : SSL
{
	protected virtual bool PlatformSupportsSSL { get { return true; } }

    public SSL_HTML5(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDefinitions.Add("USE_DEFAULT_SSLCERT=1");
    }
}
