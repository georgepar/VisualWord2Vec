#!/bin/bash

# Script to run the entire pipeline for the system 
# (prepare these files before to accomodate current run)
# 1. Read the coco_test from json file and tokenize
# 2. Convert into text file for google word2vec code
# 3. Run google word2vec to get the embeddings, save as mat files
# 4. Run the learnModel.m training the models for final evaluation

# Reading coco_test and tokenize
#python /home/satwik/VisualWord2Vec/code/utils/tokenizeData.py
#python /home/satwik/VisualWord2Vec/code/utils/json2txt.py

# Training google word2vec
nohup /usr/local/MATLAB/R2014a/bin/matlab -nodesktop -nodisplay -nosplash -r "run('/home/satwik/VisualWord2Vec/code/src/trainWord2Vec.m')" < /dev/null > output_word2vec 2> errors_word2vec

# Leaning the model learning script
#nohup /usr/local/MATLAB/R2014a/bin/matlab -nodesktop -nodisplay -nosplash -r "run('/home/satwik/VisualWord2Vec/code/learnModelSweepWrapper.m')" < /dev/null > src/dumps/output_sweep_traindata_july27 2> src/dumps/errors_sweep_traindata_july27 &
#nohup /usr/local/MATLAB/R2014a/bin/matlab -nodesktop -nodisplay -nosplash -r "run('/home/satwik/VisualWord2Vec/code/learnModel.m')" < /dev/null > src/dumps/output_raw_test 2> src/dumps/errors_raw_test &
#nohup /usr/local/MATLAB/R2014a/bin/matlab -nodesktop -nodisplay -nosplash -r "run('/home/satwik/VisualWord2Vec/code/learnModel.m')" < /dev/null > src/dumps/output_token_test 2> src/dumps/errors_token_test 
#nohup /usr/local/MATLAB/R2014a/bin/matlab -nodesktop -nodisplay -nosplash -r "run('/home/satwik/VisualWord2Vec/code/learnModel.m')" < /dev/null > src/dumps/output_test 2> src/dumps/errors_test &

#/usr/local/MATLAB/R2014a/bin/matlab -nodesktop -nodisplay -nosplash -r "run('/home/satwik/VisualWord2Vec/code/learnModel.m')" < /dev/null > dumps/output_token 2> dumps/errors_token


