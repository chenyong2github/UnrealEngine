# Copyright Epic Games, Inc. All Rights Reserved.

# Note that this is not a unit-testing suite, just a script one can run to get all the registered UE4 environments 
# launched, go through random game and close.

import gym
import ue4ml.logger as logger
from ue4ml.utils import random_action

logger.set_level(logger.DEBUG)
ue4envs = [e.id for e in gym.envs.registry.all() if 'UE4' in e.id]

for env_name in ue4envs:
    try:
        env = gym.make(env_name, server_port=None)
    except gym.error.Error:
        print('{}: FAILED TO LAUNCH'.format(env_name))
        continue
    env.reset()
    while not env.game_over:
        env.step(random_action(env))    
    print('{}: done'.format(env_name))
    env.close()
