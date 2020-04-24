# Copyright Epic Games, Inc. All Rights Reserved.
 
import unittest
from ue4ml.core import UnrealEnv 
from ue4ml.error import *


class ConnectionTest(unittest.TestCase):
    def test_no_auto_connect(self):
        env = UnrealEnv(auto_connect=False)
        self.assertFalse(env.is_connected())
        with self.assertRaises(NotConnected):
            env.reset()
        env.close()
        self.assertIsNone(env.conn)
