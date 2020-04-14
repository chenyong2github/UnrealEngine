# Copyright Epic Games, Inc. All Rights Reserved.

from gym.error import Error


class MissingBinary(Error):
    """Raised when the we fail to launch the executable, usually due to wrong executable directory or file being used.
    """
    pass
