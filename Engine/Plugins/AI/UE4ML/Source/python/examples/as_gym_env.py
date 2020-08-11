# Copyright Epic Games, Inc. All Rights Reserved.

import gym
import ue4ml.logger as logger
from ue4ml.utils import random_action
import ue4ml.utils


logger.set_level(logger.DEBUG)

# see ue4ml.utils.ArgumentParser.__init__ for list of default parameters
parser = ue4ml.utils.ArgumentParser()
parser.add_argument('--iter', type=int, default=3, help='number of games to play')
args = parser.parse_args()

env = gym.make(args.env, server_port=args.port)

for i in range(args.iter): 
    env.reset()
    reward = 0
    done = False
    while not env.game_over:
        _, reward, done, _ = env.step(random_action(env))

    print('{}: Score: {}'.format(i, reward))

env.close()