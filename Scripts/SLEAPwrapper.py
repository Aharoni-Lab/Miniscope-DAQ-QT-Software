import sys
import os

import numpy as np
import sleap

import tensorflow as tf

# NOTES:
#   Needed to upgrade from 3.6 to 3.7.9 due to dll issue with numpy
#   Needed to uninstall numpy then reinstall with pip install numpy==1.18.5 . This might not have been needed since I did this before upgrading to python 3.7


class MiniSLEAP:
    def __init__(self, *arg):
        print("PYTHON: In init of MiniSLEAP")
        self.modelPath = list()
        for a in arg:
            print("ARG" + a)
            self.modelPath.append(a)

        # Used due to some issue with tf and cuDNN
        gpus = tf.config.experimental.list_physical_devices('GPU')
        if gpus:
          try:
            # Currently, memory growth needs to be the same across GPUs
            for gpu in gpus:
              tf.config.experimental.set_memory_growth(gpu, True)
            logical_gpus = tf.config.experimental.list_logical_devices('GPU')
            print(len(gpus), "Physical GPUs,", len(logical_gpus), "Logical GPUs")
          except RuntimeError as e:
            # Memory growth must be set before GPUs have been initialized
            print(e)

    def setupSLEAP(self):
        print("PYTHON: In setupSLEAP")
        print (self.modelPath)
        self.predictor = sleap.load_model(self.modelPath, refinement="local",tracker=None)
        self.n_nodes = len(self.predictor.centroid_config.data.labels.skeletons[0].nodes)
#        print(self.n_nodes)
        self.model = self.predictor.inference_model
        return 0

    def getPose(self, image):
#        image = image[..., ::-1]  # BGR -> RGB

        # image can be a uint8 with 3 channels
        self.pred = self.model.predict(image[None, ...])
        self.peaks = self.pred["instance_peaks"][0]

        output = np.empty([3,self.n_nodes*2], dtype=np.float32);
        for j in range(self.n_nodes):
            if (self.peaks.shape[1] >= self.n_nodes):
                pts = self.peaks[:,j]
                if (not np.isnan(pts).all() and pts.shape[1] == 2 and pts.shape[0] == 2):
                    output[0,2*j] = pts[0,0]
                    output[1,2*j] = pts[0,1]
                    output[2,2*j] = 0.5

                    output[0,2*j + 1] = pts[1,0]
                    output[1,2*j + 1] = pts[1,1]
                    output[2,2*j + 1] = 0.5
                else:
                    output[0,2*j] = 0
                    output[1,2*j] = 0
                    output[2,2*j] = 0.1

                    output[0,2*j + 1] = 0
                    output[1,2*j + 1] = 0
                    output[2,2*j + 1] = 0.1
            else:
                output[0,2*j] = 0
                output[1,2*j] = 0
                output[2,2*j] = 0.1

                output[0,2*j + 1] = 0
                output[1,2*j + 1] = 0
                output[2,2*j + 1] = 0.1


        return output



