# include "visualFeatures.h"

// Storing the feature hash (globals)
struct featureWord* featHashWords; // Vocab for feature words
int* featHashInd; // Storing the hash indices that reference to vocab
const int featHashSize = 200000; // Size of the hash for feature words
int featVocabSize = 0; // Actual vocab size for feature word 
int featVocabMaxSize = 5000; // Maximum number of feature vocab
long noTest = 0, noVal = 0; // Number of test and validation variables
float* cosDist; // Storing the cosine distances between all the feature vocabulary
float* valScore, *testScore; // Storing the scores for test and val
int verbose = 0;

/***************************************************************************/
// reading feature file
void readFeatureFile(char* filePath){
    // Opening the file
    FILE* filePt = fopen(filePath, "rb");

    if(filePt == NULL){
        printf("File at %s doesnt exist!\n", filePath);
        exit(1);
    }

    printf("\nReading %s...\n", filePath);

    char pWord[MAX_STRING_LENGTH], sWord[MAX_STRING_LENGTH], rWord[MAX_STRING_LENGTH];
    // Read and store the contents
    int noTuples = 0;
    while(fscanf(filePt, "<%[^<>:]:%[^<>:]:%[^<>:]>\n", pWord, sWord, rWord) != EOF){
        //printf("%s : %s : %s\n", pWord, sWord, rWord);
        
        // Getting the indices for p, s, r
        // Get the indices, for the current tuple
        prs[noTuples].p = addFeatureWord(pWord);
        prs[noTuples].r = addFeatureWord(rWord);
        prs[noTuples].s = addFeatureWord(sWord);
        //printf("%s : %s : %s\n", prs[noTuples].p.str, prs[noTuples].s.str, prs[noTuples].r.str);
        
        noTuples++;
    }

    // Debugging
    /*int i;
    for(i = 0; i < noTuples; i++){
        //printf("%s : %s : %s  ( ", prs[i].p.str, prs[i].s.str, prs[i].r.str);
        //printf("%d )\n", i);
        //printf("%d %d %d\n", prs[i].p, prs[i].s, prs[i].r);
        printf("%s : %s : %s\n", featHashWords[prs[i].p].str, 
                            featHashWords[prs[i].s].str,
                            featHashWords[prs[i].r].str);
    }*/

    // Sanity check
    if(noTuples != NUM_TRAINING){
        printf("\nNumber of training instances dont match in feature file!\n");
        exit(1);
    }

    fclose(filePt);
    printf("File read with %d tuples\n\n", noTuples);
}

// Reading the cluster ids
void readClusterIdFile(char* clusterPath){
    FILE* filePt = fopen(clusterPath, "rb");

    if(filePt == NULL){
        printf("File at %s doesnt exist!\n", clusterPath);
        exit(1);
    }

    int i = 0, clusterId;
    while(fscanf(filePt, "%d\n", &clusterId) != EOF){
        //if(prs[i].cId == -1) prs[i].cId = clusterId;
        prs[i].cId = clusterId;
        i++;
    }

    // Sanity check
    if(i != NUM_TRAINING){
        printf("\nNumber of training instances dont match in cluster file!\n");
        exit(1);
    }

    // Debugging
    /*for(i = 0; i < NUM_TRAINING; i++){
        printf("%s : %s : %s : (%d)\n", prs[i].p.str, prs[i].s.str, prs[i].r.str, prs[i].cId);
    }*/

    fclose(filePt);
}

// Reading the visual feature file
void readVisualFeatureFile(char* fileName){
    FILE* filePt = fopen(fileName, "rb");

    if(filePt == NULL){
        printf("File at %s doesnt exist!\n", fileName);
        exit(1);
    }

    int feature, i, noLines = 0;
    // Reading till EOF
    while(fscanf(filePt, "%d", &feature) != EOF){
        prs[noLines].feat = (int*) malloc(sizeof(int) * VISUAL_FEATURE_SIZE);
        prs[noLines].feat[0] = feature;

        for(i = 1; i < VISUAL_FEATURE_SIZE; i++){
            //printf("%d ", feature);
            fscanf(filePt, "%d", &feature);
            prs[noLines].feat[i] = feature;
        }
        //printf("%d\n", feature);

        //printf("Line : %d\n", noLines);
        noLines++;
    }

    // Closing the file
    fclose(filePt);
}

// Finding the indices of words for P,R,S
struct featureWord constructFeatureWord(char* word){
    int index = SearchVocab(word); 

    struct featureWord feature;
    feature.str = (char*) malloc(MAX_STRING_LENGTH);
    strcpy(feature.str, word);

    // Initialize the fature embedding
    feature.magnitude = 0;
    feature.embed = (float*) malloc(layer1_size * sizeof(float));
    
    // Do something if not in vocab
    if(index == -1) {
        //printf("Not in vocab -> %s : %s\n", word, "") ;
        int count=0, i;

        // Split based on 's
        char* token = (char*) malloc(MAX_STRING_LENGTH);
        strcpy(token, word);

        char* first = multi_tok(token, "'s");
        char* second = multi_tok(NULL, "'s");

        // Join both the parts without the 's
        if(second != NULL) token = strcat(first, second);
        else token = first;

        char* temp = (char*) malloc(MAX_STRING_LENGTH);
        strcpy(temp, token);
        
        // Remove ' ', ',', '.', '?', '!', '\', '/'
        char* delim = " .,/!?\\";
        token = strtok(token, delim);
        // Going over the token to determine the number of parts
        while(token != NULL){
            count++;
            token = strtok(NULL, delim);
        }

        // Nsmallow store the word components, looping over them
        feature.index = (int*) malloc(count * sizeof(int));
        feature.count = count;
        
        token = strtok(temp, delim);
        count = 0;
        while(token != NULL){
            // Convert the token into lower case
            for(i = 0; token[i]; i++) token[i] = tolower(token[i]);
           
            // Save the index
            feature.index[count] = SearchVocab(token);
            //if(feature.index[count] == -1)
            //   printf("Word not found in dictionary => %s\t |  %s\n", token, word);

            //printf("%d \t", feature.index[count]);
            token = strtok(NULL, delim);
            count++;
        }
        //printf("\n");

    } else{
        //printf("In Vocab -> %s\n", word);
        feature.count = 1;
        feature.index = (int *) malloc(sizeof(int));
        feature.index[0] = index;
    }

    return feature;
}

// Initializing the refining
void initRefining(){
    long long a, b;
    unsigned long long next_random = 1;

    // Setup the network 
    a = posix_memalign((void **)&syn1, 128, (long long)vocab_size * layer1_size * sizeof(real));
    if (syn1 == NULL) {
        printf("Memory allocation failed\n"); 
        exit(1);
    }

    // Initialize the last layer of weights
    for (a = 0; a < NUM_CLUSTERS; a++) for (b = 0; b < layer1_size; b++){
        next_random = next_random * (unsigned long long)25214903917 + 11;
        syn1[a * layer1_size + b] = (((next_random & 0xFFFF) / (real)65536) - 0.5) / layer1_size;
    }

    // Setting up the hash
    featHashWords = (struct featureWord *) malloc(sizeof(struct featureWord) * featVocabMaxSize);
    featHashInd = (int*) malloc(sizeof(int) * featHashSize);
    for(a = 0; a < featHashSize; a++)
        featHashInd[a] = -1;
}

// Refine the network through clusters
void refineNetwork(){
    // Reading the features for debugging
    /*int x, z;
    for(x = 0; x < NUM_TRAINING; x++){
        for( z = 0; z < VISUAL_FEATURE_SIZE; z++){
            printf("%d ", prs[x].feat[z]);
        }
        printf("\n");
    }*/

    long long c, i;
    float* y = (float*) malloc(sizeof(float) * NUM_CLUSTERS);
    struct featureWord p, s, r;

    // Read each of the training instance
    for(i = 0; i < NUM_TRAINING; i++){
        //printf("Training %lld instance ....\n", i);
        
        // Checking possible fields to avoid segmentation error
        if(prs[i].cId < 1 || prs[i].cId > NUM_CLUSTERS) {
            printf("\nCluster id (%d) for %lld instance invalid!\n", prs[i].cId, i);
            exit(1);
        }

        //printf("Counts : %d %d %d\n", prs[i].p.count, prs[i].s.count, prs[i].r.count);

        // Updating the weights for P
        p = featHashWords[prs[i].p];
        for(c = 0; c < p.count; c++){
            // If not in vocab, continue
            if(p.index[c] == -1) continue;
            //printf("p: %d %d\n", p.index[c], p.count);

            // Predict the cluster
            computeMultinomial(y, p.index[c]);
            // Propage the error to the PRS features
            updateWeights(y, p.index[c], prs[i].cId);
        }
        
        // Updating the weights for S
        s = featHashWords[prs[i].s];
        for(c = 0; c < s.count; c++){
            // If not in vocab, continue
            if(s.index[c] == -1) continue;
            //printf("s: %d %d\n", s.index[c], s.count);

            // Predict the cluster
            computeMultinomial(y, s.index[c]);
            // Propage the error to the PRS features
            updateWeights(y, s.index[c], prs[i].cId);
        }

        // Updating the weights for R
        r = featHashWords[prs[i].r];
        for(c = 0; c < r.count; c++){
            // If not in vocab, continue
            if(r.index[c] == -1) continue;
            //printf("r: %d %d\n", r.index[c], r.count);

            // Predict the cluster
            computeMultinomial(y, r.index[c]);
            // Propage the error to the PRS features
            updateWeights(y, r.index[c], prs[i].cId);
        }
    }
}

// Evaluate y_i for each output cluster
void computeMultinomial(float* y, int wordId){
    // y stores the multinomial distribution
    float dotProduct = 0, sum = 0;
    long long a, b, offset1, offset2;

    // Offset to access the outer layer weights
    offset1 = wordId * layer1_size;
    for (b = 0; b < NUM_CLUSTERS; b++){
        dotProduct = 0;
        // Offset to access the values of hidden layer weights
        offset2 = b * layer1_size;

        for (a = 0; a < layer1_size; a++){
            dotProduct += syn0[offset1 + a] * syn1[offset2 + a];
        }

        // Exponential (clip if less or greater than the limit)
        if (dotProduct <= - MAX_EXP) dotProduct = -MAX_EXP;
        else if (dotProduct >= MAX_EXP) dotProduct = MAX_EXP;
        else dotProduct = expTable[(int) ((dotProduct + MAX_EXP) * (EXP_TABLE_SIZE / MAX_EXP / 2))];

        y[b] = dotProduct;
    }

    // Normalizing to create a probability measure
    for(b = 0; b < NUM_CLUSTERS; b++) sum += y[b];

    if(sum > 0)
        for(b = 0; b < NUM_CLUSTERS; b++) y[b] = y[b]/sum;
}

// Updating the weights given the multinomial prediction, word id and true cluster id
void updateWeights(float* y, int wordId, int trueId){
    // compute gradient for outer layer weights, gradient g
    float* e = (float*) malloc(NUM_CLUSTERS * sizeof(float));
    long long a, b, c, offset1, offset2;
    float learningRateInner = 0.01, learningRateOuter = 0.01;

    // Computing error
    for(b = 0; b < NUM_CLUSTERS; b++){
        if(b == trueId - 1) e[b] = y[b] - 1;
        else e[b] = y[b];
    }
    // Save inner layer weights for correct updates
    float* syn0copy = (float*) malloc(sizeof(float) * layer1_size);

    offset1 = layer1_size * wordId;
    for (c = 0; c < layer1_size; c++) syn0copy[c] = syn0[offset1 + c];
    // compute gradient for inner layer weights
    // update inner layer weights
    // Offset for accessing inner weights
    for(b = 0; b < NUM_CLUSTERS; b++){
        // Offset for accesing outer weights
        offset2 = layer1_size * b;
        
        for(c = 0; c < layer1_size; c++)
            syn0[offset1 + c] -= learningRateInner * e[b] * syn1[offset2 + c];
    }

    // compute gradient for outer layer weights
    // update outer layer weights
    for(a = 0; a < NUM_CLUSTERS; a++){
        offset2 = layer1_size * a;
        for(b = 0; b < layer1_size; b++){
            syn1[offset2 + b] -= learningRateOuter * e[a] * syn0copy[b];
        }
    }

    // Cleaning the copy
    free(e);
    free(syn0copy);
}

// Computes the multinomial distribution for a phrase
void computeMultinomialPhrase(float* y, int* wordId, int noWords){
    // y stores the multinomial distribution
    float dotProduct = 0, sum = 0;
    long long a, b, i, offset1, offset2;

    // Offset to access the outer layer weights
    for (b = 0; b < NUM_CLUSTERS; b++){
        dotProduct = 0;
        // Offset to access the values of hidden layer weights
        offset2 = b * layer1_size;

        // Take mean for all the words
        for(i = 0; i < noWords; i++){
            offset1 = wordId[i] * layer1_size;
            for (a = 0; a < layer1_size; a++)
                dotProduct += 1.0/noWords * syn0[offset1 + a] * syn1[offset2 + a];
        }

        // Exponential (clip if less or greater than the limit)
        if (dotProduct <= - MAX_EXP) dotProduct = -MAX_EXP;
        else if (dotProduct >= MAX_EXP) dotProduct = MAX_EXP;
        else dotProduct = expTable[(int) ((dotProduct + MAX_EXP) * (EXP_TABLE_SIZE / MAX_EXP / 2))];

        y[b] = dotProduct;
    }

    // Normalizing to create a probability measure
    for(b = 0; b < NUM_CLUSTERS; b++) sum += y[b];

    if(sum > 0)
        for(b = 0; b < NUM_CLUSTERS; b++) y[b] = y[b]/sum;
}

// Updates the weights for a phrase
void updateWeightsPhrase(float* y, int* wordId, int noWords, int trueId){
    // compute gradient for outer layer weights, gradient g
    float* e = (float*) malloc(NUM_CLUSTERS * sizeof(float));
    long long a, b, c, i, offset1, offset2;
    float learningRateOuter = 0.01, learningRateInner = 0.01;

    // Computing error
    for(b = 0; b < NUM_CLUSTERS; b++){
        if(b == trueId - 1) e[b] = y[b] - 1;
        else e[b] = y[b];
    }

    // Save inner layer weights for correct updates
    float* syn0copy = (float*) malloc(sizeof(float) * layer1_size * noWords);

    for (i = 0; i < noWords; i++){
        offset1 = layer1_size * wordId[i];
        offset2 = layer1_size * i;
        for (c = 0; c < layer1_size; c++) syn0copy[offset2 + c] = syn0[offset1 + c];
    }

    // compute gradient for inner layer weights
    // update inner layer weights
    // Offset for accessing inner weights
    for(i = 0; i < noWords; i++){
        offset1 = layer1_size * wordId[i];
        for(b = 0; b < NUM_CLUSTERS; b++){
            // Offset for accesing outer weights
            offset2 = layer1_size * b;
            
            for(c = 0; c < layer1_size; c++)
                syn0[offset1 + c] -= 1.0/noWords * learningRateInner * e[b] * syn1[offset2 + c];
        }
    }

    // compute gradient for outer layer weights
    // update outer layer weights
    for(i = 0; i < noWords; i++){
        offset1 = layer1_size * i;
        for(a = 0; a < NUM_CLUSTERS; a++){
            offset2 = layer1_size * a;
            for(b = 0; b < layer1_size; b++){
                syn1[offset2 + b] -= 1.0/noWords * learningRateOuter * e[a] * syn0copy[offset1 + b];
            }
        }
    }

    free(syn0copy);
    free(e);
}

// Saving the feature embeddings needed for comparing, at the given file name
void saveEmbeddings(char* saveName){
    FILE* filePt = fopen(saveName, "wb");
    int i;

    // Re-compute the embeddings before saving
    computeEmbeddings();
    
    // Go through the vocab and save the embeddings
    for(i = 0; i < featVocabSize; i++)
        saveFeatureEmbedding(featHashWords[i], filePt);
    
    fclose(filePt);
}

// Save a particular embedding
void saveFeatureEmbedding(struct featureWord feature, FILE* filePt){
    // Saving to the file
    int i;
    for(i = 0; i < layer1_size - 1; i++)
        fprintf(filePt, "%f ", feature.embed[i]);
    fprintf(filePt, "%f\n", feature.embed[layer1_size-1]);
}

// Saving the feature vocab
void saveFeatureWordVocab(char* fileName){
    FILE* filePt = fopen(fileName, "wb");
    int i;

    // Go through the vocab and save the embeddings
    for(i = 0; i < featVocabSize; i++)
        fprintf(filePt, "%s\n", featHashWords[i].str);

    fclose(filePt);

}

// Compute embeddings
void computeEmbeddings(){
    long i;
    // Computing the feature embeddings
    for(i = 0; i < featVocabSize; i++){
        if(featHashWords[i].embed == NULL)
            // Allocate and then compute the feature embedding
            featHashWords[i].embed = (float*) malloc(layer1_size * sizeof(float));

        // Computing the feature embedding
        computeFeatureEmbedding(&featHashWords[i]);
    }
}

// Compute embedding for a feature word
void computeFeatureEmbedding(struct featureWord* feature){
    // Go through the current feature and get the mean of components
    int i, c, actualCount = 0;
    long long offset;
    float* mean;
    mean = (float*) calloc(layer1_size, sizeof(float));

    // Get the mean feature for the word
    for(c = 0; c < feature->count; c++){
        // If not in vocab, continue
        if(feature->index[c] == -1) continue;

        // Write the vector
        offset = feature->index[c] * layer1_size;
        for (i = 0; i < layer1_size; i++) 
            mean[i] += syn0[offset + i];

        // Increase the count
        actualCount++;
    }

    // Normalizing if non-zero count
    if(actualCount)
        for (i = 0; i < layer1_size; i++)
            mean[i] = mean[i]/actualCount;

    // Saving the embedding in the featureWord
    for(i = 0; i < layer1_size; i++)
        feature->embed[i] = mean[i];

    // Compute the magnitude of mean
    float magnitude = 0;
    for(i = 0; i < layer1_size; i++)
        magnitude += mean[i] * mean[i];
        
    //feature->magnitude = (float*) malloc(sizeof(float));
    feature->magnitude = sqrt(magnitude);

    free(mean);
}

// Searching a feature word
int searchFeatureWord(char* word){
    unsigned int hash = getFeatureWordHash(word);

    while (1){
        if (featHashInd[hash] == -1) {
            return -1;
        }
        if (!strcmp(word, featHashWords[featHashInd[hash]].str)){
            return featHashInd[hash];
        }
        hash = (hash + 1) % featHashSize;
    }

    return -1;
}

// Adding a feature word
int addFeatureWord(char* word){
    // search for feature if already exists
    int featureInd = searchFeatureWord(word);
    
    // If yes, ignore
    if(featureInd != -1) 
        return featureInd;
    else{
        // If no, add and re-adjust featVocabSize and featVocabMaxSize
        // adding a new featureWord
        unsigned int hash = getFeatureWordHash(word);

        // Get the index where new feature word should be stored
        while(1){
            if(featHashInd[hash] != -1) 
                hash = (hash + 1) % featHashSize;
            else
                break;
        }
        // Add the word and increase vocab size
        featHashInd[hash] = featVocabSize;
        featHashWords[featVocabSize] = constructFeatureWord(word);
        featVocabSize++;

        // Adjusting the size of vocab if needed
        if(featVocabSize + 2 > featVocabMaxSize){
            featVocabMaxSize += 1000;
            featHashWords = (struct featureWord *) realloc(featHashWords, 
                                        featVocabMaxSize * sizeof(struct featureWord));
        }

        return featHashInd[hash];
    }
}

// Hash function computation
int getFeatureWordHash(char* word){
    unsigned long long a, hash = 0;
    for (a = 0; a < strlen(word); a++)
        hash = hash * 257 + word[a];
    hash = hash % featHashSize;
    return hash;
}

// Multiple character split
// Source: http://stackoverflow.com/questions/29788983/split-char-string-with-multi-character-delimiter-in-c
char *multi_tok(char *input, char *delimiter) {
    static char *string;
    if (input != NULL)
        string = input;

    if (string == NULL)
        return string;

    char *end = strstr(string, delimiter);
    if (end == NULL) {
        char *temp = string;
        string = NULL;
        return temp;
    }

    char *temp = string;

    *end = '\0';
    string = end + strlen(delimiter);
    return temp;
}

// Clustering kmeans wrapper
// Source: http://yael.gforge.inria.fr/tutorial/tuto_kmeans.html
void clusterVisualFeatures(int noClusters){
    int k = noClusters;                           /* number of cluster to create */
    int d = VISUAL_FEATURE_SIZE;                           /* dimensionality of the vectors */
    int n = NUM_TRAINING;                         /* number of vectors */
    //int nt = 1;                           /* number of threads to use */
    int niter = 0;                        /* number of iterations (0 for convergence)*/
    int redo = 1;                         /* number of redo */

    // Populate the features
    float * v = fvec_new (d * n);    /* random set of vectors */
    long i, j, offset;
    for (i = 0; i < n; i++){
        offset = i * d;
        for(j = 0; j < d; j++)
            v[offset + j] = (float) prs[i].feat[j];
    }

    /* variables are allocated externaly */
    float * centroids = fvec_new (d * k); /* output: centroids */
    float * dis = fvec_new (n);           /* point-to-cluster distance */
    int * assign = ivec_new (n);          /* quantization index of each point */
    int * nassign = ivec_new (k);         /* output: number of vectors assigned to each centroid */

    double t1 = getmillisecs();
    // Cluster the features
    kmeans (d, n, k, niter, v, 1, 1, redo, centroids, dis, assign, nassign);
    double t2 = getmillisecs();

    printf ("kmeans performed in %.3fs\n\n", (t2 - t1)  / 1000);
    //ivec_print (nassign, k);

    // Write the cluster ids to the prsTuple structure
    for (i = 0; i < n; i++)
        prs[i].cId = assign[i] + 1;

    // Debugging the cId for the prs tuples
    /*for (i = 0; i < n; i++)
        printf("%i\n", prs[i].cId);*/

    // Free memory
    free(v); free(centroids); free(dis); free(assign); free(nassign);
}

// Common sense evaluation
void performCommonSenseTask(){
    printf("Common sense task\n\n");
    // Read the validation and test sets    
    char testFile[] = "/home/satwik/VisualWord2Vec/data/test_features.txt";
    char valFile[] = "/home/satwik/VisualWord2Vec/data/val_features.txt";

    // Clean the strings for test and validation sets, store features
    readTestValFiles(valFile, testFile);

    // Get the features for test and validation sets
    // Re-evaluate the features for the entire vocab
    computeEmbeddings();

    // Evaluate the cosine distance
    evaluateCosDistance();

    // Going through all the test / validation examples
    // For each, going through training instances and computing the score
    valScore = (float*) malloc(noVal * sizeof(float));
    testScore = (float*) malloc(noTest * sizeof(float));
    // Threshold sweeping for validation
    float threshold;
    for(threshold = -2.0; threshold < 3.0; threshold += 0.1){
        
        computeTestValScores(val, noVal, threshold, valScore);
        computeTestValScores(test, noTest, threshold, testScore);

        // Compute the accuracy
        float* precVal = computeMAP(valScore, val, noVal);
        float* precTest = computeMAP(testScore, test, noTest);

        printf("Precision (threshold , val , test) : %f %f %f\n", 
                                        threshold, precVal[0], precTest[0]);
    }
}

// Reading the test and validation files
void readTestValFiles(char* valName, char* testName){
    // Read the file
    long noTuples = 0, i;
    
    // Counting the number of lines
    char pWord[MAX_STRING_LENGTH], 
         rWord[MAX_STRING_LENGTH], 
         sWord[MAX_STRING_LENGTH];
    int gTruth = -1;

    FILE* filePt = fopen(valName, "rb");
    while(fscanf(filePt, "<%[^<>:]:%[^<>:]:%[^<>:]> %d\n", pWord, rWord, sWord, &gTruth) != EOF)
        noTuples++;

    // Rewind the stream and read again
    rewind(filePt);
    
    // Initialize and save the feature words
    val = (struct prsTuple*) malloc(sizeof(struct prsTuple) * noTuples);

    for(i = 0; i < noTuples; i++){
        if(fscanf(filePt, "<%[^<>:]:%[^<>:]:%[^<>:]> %d\n", pWord, rWord, sWord, &gTruth) != EOF){
            val[i].p = addFeatureWord(pWord);
            val[i].r = addFeatureWord(rWord);
            val[i].s = addFeatureWord(sWord);
        
            val[i].cId = gTruth;
            //printf("%d = <%d:%d:%d> %d\n", i, val[i].p, val[i].r, val[i].s, val[i].cId);
        }
    }

    noVal = noTuples;
    printf("Found %ld tuples in %s...\n\n", noTuples, valName);
    // Close the file
    fclose(filePt);
    /*******************************************************************************/
    // Test file
    // filePt 
    filePt = fopen(testName, "rb");
    noTuples = 0;
    while(fscanf(filePt, "<%[^<>:]:%[^<>:]:%[^<>:]> %d\n", pWord, rWord, sWord, &gTruth) != EOF)
        noTuples++;

    // Rewind the stream and read again
    rewind(filePt);
    
    // Initialize and save the feature words
    test = (struct prsTuple*) malloc(sizeof(struct prsTuple) * noTuples);
    for(i = 0; i < noTuples; i++){
        if(fscanf(filePt, "<%[^<>:]:%[^<>:]:%[^<>:]> %d\n", pWord, rWord, sWord, &gTruth) != EOF){
            test[i].p = addFeatureWord(pWord);
            test[i].r = addFeatureWord(rWord);
            test[i].s = addFeatureWord(sWord);
        
            test[i].cId = gTruth;
            //printf("%d <%d:%d:%d> %d\n", i, test[i].p, test[i].r, test[i].s, test[i].cId);
        }
    }

    noTest = noTuples;
    printf("Found %ld tuples in %s...\n\n", noTuples, testName);
    // Close the file
    fclose(filePt);
}

// Cosine distance evaluation
void evaluateCosDistance(){
    if (verbose) printf("Evaluating pairwise dotproducts..\n\n");
    // Allocate memory for cosDist variable
    cosDist = (float*) malloc(featVocabSize * featVocabSize * sizeof(float));
    
    // For each pair, we evaluate the dot product along with normalization
    long a, b, i, offset;
    float magProd = 0, dotProduct;
    for(a = 0; a < featVocabSize; a++){
        offset = featVocabSize * a;
        for(b = 0; b < featVocabSize; b++){
            if(featHashWords[a].embed == NULL || featHashWords[b].embed == NULL)
                printf("NULL pointers : %ld %ld\n", a, b);

            dotProduct = 0;
            for(i = 0; i < layer1_size; i++){
                dotProduct += 
                    featHashWords[a].embed[i] * featHashWords[b].embed[i];
            }
            
            // Save the dotproduct
            magProd = (featHashWords[a].magnitude) * (featHashWords[b].magnitude);
            if(magProd)
                cosDist[offset + b] = dotProduct / magProd;
             else
                cosDist[offset + b] = 0.0;
        }
    }
}

// Computing the test and validation scores
void computeTestValScores(struct prsTuple* holder, long noInst, float threshold, float* scoreList){
    if(verbose) printf("Computing the scores...\n\n");

    // Iteration variables
    long a, b;
    float meanScore, pScore, rScore, sScore, curScore; 
    for(a = 0; a < noInst; a++){
        meanScore = 0.0;
        // For each training instance, find score, ReLU and max
        for(b = 0; b < NUM_TRAINING; b++){
            // Get P score
            pScore = cosDist[featVocabSize * prs[b].p + holder[a].p];
            
            // Get R score
            rScore = cosDist[featVocabSize * prs[b].r + holder[a].r];
            
            // Get S score
            sScore = cosDist[featVocabSize * prs[b].s + holder[a].s];
           
            // ReLU
            curScore = pScore + rScore + sScore - threshold;
            if(curScore < 0) curScore = 0;
            
            // Add it to the meanScore
            meanScore += curScore;
        }

        // Save the mean score for the current instance
        scoreList[a] = meanScore / NUM_TRAINING;
        //printf("%ld : %f\n", a, scoreList[a]);
    }
}

// Compute mAP and basic precision
float* computeMAP(float* score, struct prsTuple* holder, long noInst){
    if (verbose) printf("Computing MAP...\n\n");
    // Crude implementation
    long a, b;
    int* rankedLabels = (int*) malloc(sizeof(int) * noInst);
    float* rankedScores = (float*) malloc(sizeof(float) * noInst);

    // Make a copy of scores
    for(a = 0; a < noInst; a++) rankedScores[a] = score[a];

    long maxInd;
    // Get the rank of positive instances wrt to the scores
    for(a = 0; a < noInst; a++){
        maxInd = 0;
        for(b = 0; b < noInst; b++)
            // Check if max is also current max
            if(rankedScores[b] != -1 && 
                        rankedScores[maxInd] < rankedScores[b]) 
                maxInd = b;

        // Swap the max and element at that instance
        rankedLabels[a] = holder[maxInd].cId;
        // NULLing the max ind 
        rankedScores[maxInd] = -1;
    }

    float mAP = 0, base = 0;
    long noPositives = 0;
    // Compute the similarity wrt ideal ordering 1,2,3.....
    for(a = 0; a < noInst; a++)
        if(rankedLabels[a] == 1){
            // Increasing the positives
            noPositives++;
            mAP += noPositives / (float) (a + 1);
            //printf("%ld %ld %f\n", noPositives, a + 1, noPositives/(float) (a + 1));
        }
   
    // Compute mAP
    mAP = mAP / noPositives;
    // Compute base precision
    base = noPositives / (float)noInst;
    // Packing both in an array
    float* precision = (float*) malloc(2 * sizeof(float));
    precision[0] = mAP;
    precision[1] = base;

    // Free memory
    free(rankedScores);
    return precision;
}