import sys
import numpy as np
import cv2
import colorcet as cc
from dlclive import DLCLive, Processor
import tensorflow as tf

class MiniDLC:
    def __init__(self, modelPath, resizeVal):
        print("In init")
        self.modelPath = modelPath
        self.resize = resizeVal

    def setupDLC(self):
          # Some Notes:
          # Need to install CUDA from NVidia. I had to use v10.1 and move all needed .dll's to C:Windows\System32
          # Need to install cuDNN from NVidia. Follow install instructions and still move .dll to System32 folder
          # Was still getting a CUDNN_STATUS_ALLOC_FAILED error so added the "allow_growth" code below to get it to work
          # If having error: Could not create cudnn handle: CUDNN_STATUS_ALLOC_FAILED uncomment below
        print("In setupDLC.")
        try:
            config = tf.compat.v1.ConfigProto()
            config.gpu_options.allow_growth = True
            self.dlcLive = DLCLive(self.modelPath, resize=self.resize, tf_config=config)
            return 0
        except:
#            e = sys.exc_info()[0]
            try:
                self.dlcLive = DLCLive(self.modelPath, resize=self.resize)
                return 0
            except:
                return 1

    def sayHi(self):
        print(self.resize)
        return 0

    def getColors(self):
        # Minimic DLC's coloring
        colorArray = np.zeros((60,1),dtype=np.uint8)
        all_colors = getattr(cc, "bmy")
        colors = all_colors[:: int(len(all_colors) / 20)]
#        print(type(colors))
#        print(type(colors[0]))
#        print(len(colors))
        for idx in range(20):
            colors[idx] = colors[idx].lstrip("#")
#            print(colors[idx])
            colorArray[idx*3,0] = int(colors[idx][:2],16)
            colorArray[idx*3+1,0] = int(colors[idx][2:4],16)
            colorArray[idx*3+2,0] = int(colors[idx][4:6],16)
        return colorArray

    def initInference(self, image):
        self.pose = self.dlcLive.init_inference(image)
        return self.pose

    def getPose(self, image):
        self.pose = self.dlcLive.get_pose(image)
#        print(self.pose)
        return self.pose
