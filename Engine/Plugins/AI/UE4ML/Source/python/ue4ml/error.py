# Copyright Epic Games, Inc. All Rights Reserved.

from gym.error import Error
import msgpackrpc

class UE4MLError(Exception):
    pass


class FailedToLaunch(UE4MLError):
    """Raised when the we fail to launch the executable, usually due to wrong executable directory or file being used.
    """
    pass

class UE4InstanceNoLongerRunning(UE4MLError):
    """Raised when we detect that the UE4 instance launched by the script is no longer running. It could have crashed,
        been closed manually or the instance itself has shut down. Check ue4 logs for details.
    """
    pass

class UnableToReachRPCServer(UE4MLError):
    """Raised when initial calls to rpc server fail (due to time out or reconnection limit reaching)
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
