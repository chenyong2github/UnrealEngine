# Copyright Epic Games, Inc. All Rights Reserved.

# Note that this is not a unit-testing suite, just a script one can run to get all the registered UE4 environments 
# launched, go through random game and close.

import gym
import ue4ml.logger as logger
from ue4ml.utils import random_action
from threading import Thread

WITH_THREADING = True
logger.set_level(logger.DEBUG)
ue4envs = [e.id for e in gym.envs.registry.all() if 'UE4' in e.id]


def launch_env(env_name):
    try:
        env = gym.make(env_name, server_port=None, timeout=10)
    except gym.error.Error:
        print('{}: FAILED TO LAUNCH'.format(env_name))
        return
    env.reset()
    while not env.game_over:
        env.step(random_action(env))    
    print('{}: done'.format(env_name))
    env.close()
    

if __name__ == '__main__':
    threads = []
    for name in ue4envs:
        if WITH_THREADING:
            t = Thread(target=launch_env, args=(name,))
            t.start()
            threads.append((name, t))
        else:
            launch_env(name)
            
    for name, t in threads:
        print('Joining {}'.format(name), end='')
        t.join()
        print(' DONE.'.format(name))
