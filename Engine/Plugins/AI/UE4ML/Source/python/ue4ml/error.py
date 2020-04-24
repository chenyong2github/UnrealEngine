# Copyright Epic Games, Inc. All Rights Reserved.

from gym.error import Error
import msgpackrpc

class UE4MLError(Exception):
    pass


class FailedToLaunch(UE4MLError):
    """Raised when the we fail to launch the executable, usually due to wrong executable directory or file being used.
    """
    pass

class NotConnected(UE4MLError):
    """Raised when the we fail to launch the executable, usually due to wrong executable directory or file being used.
    """
    pass

# error aliases
RPCError = msgpackrpc.error.RPCError
ReconnectionLimitReached = msgpackrpc.error.TransportError
ConnectionTimeoutError = msgpackrpc.error.TimeoutError
