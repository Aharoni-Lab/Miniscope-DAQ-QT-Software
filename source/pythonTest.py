import numpy as np
import cv2
from dlclive import DLCLive, Processor

class MiniDLC:
    def __init__(self, modelPath, resizeVal):
        print("In init")
        self.modelPath = modelPath
        self.resize = .25 #resizeVal
        self.dlcLive = DLCLive(modelPath, resize=resizeVal, display=False)

    def sayHi(self):
        print(self.resize)
        return 0

    def initInference(self, image):
        self.pose = self.dlcLive.init_inference(image)
        return self.pose

    def getPose(self, image):
        self.pose = self.dlcLive.get_pose(image)
#        print(self.pose)
        return self.pose
