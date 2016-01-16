//  Copyright 2013 Google Inc. All Rights Reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ctype.h>
#include <unistd.h>

/**********************************************************************************/
// [S] added
# include "macros.h"
# include "structs.h"
# include "visualFeatures.h"
# include "debugFunctions.h"
# include "vpFunctions.h"
# include "helperFunctions.h"
# include "cocoFunctions.h"
# include "vqaFunctions.h"
# include "genomeFunctions.h"
/***********************************************************************************/
// Extern variables
extern float prevTestAcc, prevValAcc;
extern long noTest;

extern float *syn0P, *syn0S, *syn0R;

// Variations 
int trainPhrases = 0; // Handle phrases as a unit / separately
int trainMulti = 0; // Train single / multiple models for P,R,S
int clusterCommonSense = 25; // Number of initial clusters to use
int clusterCOCO = 5000; // Number of initial clusters to use
int clusterVQA = 100; // Number of initial clusters to use
int clusterVP = 100; // Number of initial clusters to use
int clusterGenome = 5000; // Number of initial clusters to use for genome
int usePCA = 0;  // Reduce the dimensions through PCA
int permuteMAP = 0; // Permute the data and compute mAP multiple times
int debugModeVP = 0; // Debug mode for VP task
int debugModeVQA = 0; // Debug mode for VQA task
int debugModeGenome = 0; // Debug mode for genome visual
int windowVP = 5; // window size for training on sentences
int useAlternate = 0; // Use word2vec for unrefined words
// Training the sentences in one of the modes
// Could be one of DESCRIPTIONS, SENTENCES, WORDS, WINDOWS;
enum TrainMode trainMode = SENTENCES;

/***********************************************************************************/
const int vocab_hash_size = 30000000;  // Maximum 30 * 0.7 = 21M words in the vocabulary

char train_file[MAX_STRING], output_file[MAX_STRING];
char save_vocab_file[MAX_STRING], read_vocab_file[MAX_STRING];
struct vocab_word *vocab;
int binary = 0, cbow = 1, debug_mode = 2, window = 5, min_count = 5, num_threads = 12, min_reduce = 1;
int *vocab_hash;
long long vocab_max_size = 1000, vocab_size = 0, layer1_size = 100;
long long train_words = 0, word_count_actual = 0, iter = 5, file_size = 0, classes = 0;
real alpha = 0.025, starting_alpha, sample = 1e-3;
real *syn0, *syn1, *syn1neg, *expTable;
clock_t start;

int hs = 0, negative = 5;
const int table_size = 1e8;
int *table;

int* refineVocab; // Keep track of words that are being refined
float* syn0raw; // Backup for raw word2vec, without refining

void InitUnigramTable() {
  int a, i;
  double train_words_pow = 0;
  double d1, power = 0.75;
  table = (int *)malloc(table_size * sizeof(int));
  for (a = 0; a < vocab_size; a++) train_words_pow += pow(vocab[a].cn, power);
  i = 0;
  d1 = pow(vocab[i].cn, power) / train_words_pow;
  for (a = 0; a < table_size; a++) {
    table[a] = i;
    if (a / (double)table_size > d1) {
      i++;
      d1 += pow(vocab[i].cn, power) / train_words_pow;
    }
    if (i >= vocab_size) i = vocab_size - 1;
  }
}

// Reads a single word from a file, assuming space + tab + EOL to be word boundaries
void ReadWord(char *word, FILE *fin) {
  int a = 0, ch;
  while (!feof(fin)) {
    ch = fgetc(fin);
    if (ch == 13) continue;
    if ((ch == ' ') || (ch == '\t') || (ch == '\n')) {
      if (a > 0) {
        if (ch == '\n') ungetc(ch, fin);
        break;
      }
      if (ch == '\n') {
        strcpy(word, (char *)"</s>");
        return;
      } else continue;
    }
    word[a] = ch;
    a++;
    if (a >= MAX_STRING - 1) a--;   // Truncate too long words
  }
  word[a] = 0;
}

// Returns hash value of a word
int GetWordHash(char *word) {
  unsigned long long a, hash = 0;
  for (a = 0; a < strlen(word); a++) hash = hash * 257 + word[a];
  hash = hash % vocab_hash_size;
  return hash;
}

// Returns position of a word in the vocabulary; if the word is not found, returns -1
int SearchVocab(char *word) {
  unsigned int hash = GetWordHash(word);
  while (1) {
    if (vocab_hash[hash] == -1) return -1;
    if (!strcmp(word, vocab[vocab_hash[hash]].word)) return vocab_hash[hash];
    hash = (hash + 1) % vocab_hash_size;
  }
  return -1;
}

// Reads a word and returns its index in the vocabulary
int ReadWordIndex(FILE *fin) {
  char word[MAX_STRING];
  ReadWord(word, fin);
  if (feof(fin)) return -1;
  return SearchVocab(word);
}

// Adds a word to the vocabulary
int AddWordToVocab(char *word) {
  unsigned int hash, length = strlen(word) + 1;
  if (length > MAX_STRING) length = MAX_STRING;
  vocab[vocab_size].word = (char *)calloc(length, sizeof(char));
  strcpy(vocab[vocab_size].word, word);
  vocab[vocab_size].cn = 0;
  vocab_size++;
  // Reallocate memory if needed
  if (vocab_size + 2 >= vocab_max_size) {
    vocab_max_size += 1000;
    vocab = (struct vocab_word *)realloc(vocab, vocab_max_size * sizeof(struct vocab_word));
  }
  hash = GetWordHash(word);
  while (vocab_hash[hash] != -1) hash = (hash + 1) % vocab_hash_size;
  vocab_hash[hash] = vocab_size - 1;
  return vocab_size - 1;
}

// Used later for sorting by word counts
int VocabCompare(const void *a, const void *b) {
    return ((struct vocab_word *)b)->cn - ((struct vocab_word *)a)->cn;
}

// Sorts the vocabulary by frequency using word counts
void SortVocab() {
  int a, size;
  unsigned int hash;
  // Sort the vocabulary and keep </s> at the first position
  qsort(&vocab[1], vocab_size - 1, sizeof(struct vocab_word), VocabCompare);
  for (a = 0; a < vocab_hash_size; a++) vocab_hash[a] = -1;
  size = vocab_size;
  train_words = 0;
  for (a = 0; a < size; a++) {
    // Words occuring less than min_count times will be discarded from the vocab
    if ((vocab[a].cn < min_count) && (a != 0)) {
      vocab_size--;
      free(vocab[a].word);
    } else {
      // Hash will be re-computed, as after the sorting it is not actual
      hash=GetWordHash(vocab[a].word);
      while (vocab_hash[hash] != -1) hash = (hash + 1) % vocab_hash_size;
      vocab_hash[hash] = a;
      train_words += vocab[a].cn;
    }
  }
  vocab = (struct vocab_word *)realloc(vocab, (vocab_size + 1) * sizeof(struct vocab_word));
  // Allocate memory for the binary tree construction
  for (a = 0; a < vocab_size; a++) {
    vocab[a].code = (char *)calloc(MAX_CODE_LENGTH, sizeof(char));
    vocab[a].point = (int *)calloc(MAX_CODE_LENGTH, sizeof(int));
  }
}

// Reduces the vocabulary by removing infrequent tokens
void ReduceVocab() {
  int a, b = 0;
  unsigned int hash;
  for (a = 0; a < vocab_size; a++) if (vocab[a].cn > min_reduce) {
    vocab[b].cn = vocab[a].cn;
    vocab[b].word = vocab[a].word;
    b++;
  } else free(vocab[a].word);
  vocab_size = b;
  for (a = 0; a < vocab_hash_size; a++) vocab_hash[a] = -1;
  for (a = 0; a < vocab_size; a++) {
    // Hash will be re-computed, as it is not actual
    hash = GetWordHash(vocab[a].word);
    while (vocab_hash[hash] != -1) hash = (hash + 1) % vocab_hash_size;
    vocab_hash[hash] = a;
  }
  //fflush(stdout);
  min_reduce++;
}

// Create binary Huffman tree using the word counts
// Frequent words will have short uniqe binary codes
void CreateBinaryTree() {
  long long a, b, i, min1i, min2i, pos1, pos2, point[MAX_CODE_LENGTH];
  char code[MAX_CODE_LENGTH];
  long long *count = (long long *)calloc(vocab_size * 2 + 1, sizeof(long long));
  long long *binary = (long long *)calloc(vocab_size * 2 + 1, sizeof(long long));
  long long *parent_node = (long long *)calloc(vocab_size * 2 + 1, sizeof(long long));
  for (a = 0; a < vocab_size; a++) count[a] = vocab[a].cn;
  for (a = vocab_size; a < vocab_size * 2; a++) count[a] = 1e15;
  pos1 = vocab_size - 1;
  pos2 = vocab_size;
  // Following algorithm constructs the Huffman tree by adding one node at a time
  for (a = 0; a < vocab_size - 1; a++) {
    // First, find two smallest nodes 'min1, min2'
    if (pos1 >= 0) {
      if (count[pos1] < count[pos2]) {
        min1i = pos1;
        pos1--;
      } else {
        min1i = pos2;
        pos2++;
      }
    } else {
      min1i = pos2;
      pos2++;
    }
    if (pos1 >= 0) {
      if (count[pos1] < count[pos2]) {
        min2i = pos1;
        pos1--;
      } else {
        min2i = pos2;
        pos2++;
      }
    } else {
      min2i = pos2;
      pos2++;
    }
    count[vocab_size + a] = count[min1i] + count[min2i];
    parent_node[min1i] = vocab_size + a;
    parent_node[min2i] = vocab_size + a;
    binary[min2i] = 1;
  }
  // Now assign binary code to each vocabulary word
  for (a = 0; a < vocab_size; a++) {
    b = a;
    i = 0;
    while (1) {
      code[i] = binary[b];
      point[i] = b;
      i++;
      b = parent_node[b];
      if (b == vocab_size * 2 - 2) break;
    }
    vocab[a].codelen = i;
    vocab[a].point[0] = vocab_size - 2;
    for (b = 0; b < i; b++) {
      vocab[a].code[i - b - 1] = code[b];
      vocab[a].point[i - b] = point[b] - vocab_size;
    }
  }
  free(count);
  free(binary);
  free(parent_node);
}

void LearnVocabFromTrainFile() {
  char word[MAX_STRING];
  FILE *fin;
  long long a, i;
  for (a = 0; a < vocab_hash_size; a++) vocab_hash[a] = -1;
  fin = fopen(train_file, "rb");
  if (fin == NULL) {
    printf("ERROR: training data file not found!\n");
    exit(1);
  }
  vocab_size = 0;
  AddWordToVocab((char *)"</s>");
  while (1) {
    ReadWord(word, fin);
    if (feof(fin)) break;
    train_words++;
    if ((debug_mode > 1) && (train_words % 100000 == 0)) {
      printf("%lldK%c", train_words / 1000, 13);
      //fflush(stdout);
    }
    i = SearchVocab(word);
    if (i == -1) {
      a = AddWordToVocab(word);
      vocab[a].cn = 1;
    } else vocab[i].cn++;
    if (vocab_size > vocab_hash_size * 0.7) ReduceVocab();
  }
  SortVocab();
  if (debug_mode > 0) {
    printf("Vocab size: %lld\n", vocab_size);
    printf("Words in train file: %lld\n", train_words);
  }
  file_size = ftell(fin);
  fclose(fin);
}

void SaveVocab() {
  long long i;
  FILE *fo = fopen(save_vocab_file, "wb");
  for (i = 0; i < vocab_size; i++) fprintf(fo, "%s %lld\n", vocab[i].word, vocab[i].cn);
  fclose(fo);
}

void ReadVocab() {
  long long a, i = 0;
  char c;
  char word[MAX_STRING];
  FILE *fin = fopen(read_vocab_file, "rb");
  if (fin == NULL) {
    printf("Vocabulary file not found\n");
    exit(1);
  }
  for (a = 0; a < vocab_hash_size; a++) vocab_hash[a] = -1;
  vocab_size = 0;
  while (1) {
    ReadWord(word, fin);
    if (feof(fin)) break;
    a = AddWordToVocab(word);
    fscanf(fin, "%lld%c", &vocab[a].cn, &c);
    i++;
  }
  SortVocab();
  if (debug_mode > 0) {
    printf("Vocab size: %lld\n", vocab_size);
    printf("Words in train file: %lld\n", train_words);
  }
  fin = fopen(train_file, "rb");
  if (fin == NULL) {
    printf("ERROR: training data file not found!\n");
    exit(1);
  }
  fseek(fin, 0, SEEK_END);
  file_size = ftell(fin);
  fclose(fin);
}

void InitNet() {
  long long a, b;
  unsigned long long next_random = 1;
  a = posix_memalign((void **)&syn0, 128, (long long)vocab_size * layer1_size * sizeof(real));
  if (syn0 == NULL) {printf("Memory allocation failed\n"); exit(1);}
  if (hs) {
    a = posix_memalign((void **)&syn1, 128, (long long)vocab_size * layer1_size * sizeof(real));
    if (syn1 == NULL) {printf("Memory allocation failed\n"); exit(1);}
    for (a = 0; a < vocab_size; a++) for (b = 0; b < layer1_size; b++)
     syn1[a * layer1_size + b] = 0;
  }
  if (negative>0) {
    a = posix_memalign((void **)&syn1neg, 128, (long long)vocab_size * layer1_size * sizeof(real));
    if (syn1neg == NULL) {printf("Memory allocation failed\n"); exit(1);}
    for (a = 0; a < vocab_size; a++) for (b = 0; b < layer1_size; b++)
     syn1neg[a * layer1_size + b] = 0;
  }
  for (a = 0; a < vocab_size; a++) for (b = 0; b < layer1_size; b++) {
    next_random = next_random * (unsigned long long)25214903917 + 11;
    syn0[a * layer1_size + b] = (((next_random & 0xFFFF) / (real)65536) - 0.5) / layer1_size;
  }

  CreateBinaryTree();
}

void *TrainModelThread(void *id) {
  long long a, b, d, cw, word, last_word, sentence_length = 0, sentence_position = 0;
  long long word_count = 0, last_word_count = 0, sen[MAX_SENTENCE_LENGTH + 1];
  long long l1, l2, c, target, label, local_iter = iter;
  unsigned long long next_random = (long long)id;
  real f, g;
  clock_t now;
  real *neu1 = (real *)calloc(layer1_size, sizeof(real));
  real *neu1e = (real *)calloc(layer1_size, sizeof(real));
  FILE *fi = fopen(train_file, "rb");
  fseek(fi, file_size / (long long)num_threads * (long long)id, SEEK_SET);
  while (1) {
    if (word_count - last_word_count > 10000) {
      word_count_actual += word_count - last_word_count;
      last_word_count = word_count;
      if ((debug_mode > 1)) {
        now=clock();
        printf("%cAlpha: %f  Progress: %.2f%%  Words/thread/sec: %.2fk  ", 13, alpha,
         word_count_actual / (real)(iter * train_words + 1) * 100,
         word_count_actual / ((real)(now - start + 1) / (real)CLOCKS_PER_SEC * 1000));
        //fflush(stdout);
      }
      alpha = starting_alpha * (1 - word_count_actual / (real)(iter * train_words + 1));
      if (alpha < starting_alpha * 0.0001) alpha = starting_alpha * 0.0001;
    }
    if (sentence_length == 0) {
      while (1) {
        word = ReadWordIndex(fi);
        if (feof(fi)) break;
        if (word == -1) continue;
        word_count++;
        if (word == 0) break;
        // The subsampling randomly discards frequent words while keeping the ranking same
        if (sample > 0) {
          real ran = (sqrt(vocab[word].cn / (sample * train_words)) + 1) * (sample * train_words) / vocab[word].cn;
          next_random = next_random * (unsigned long long)25214903917 + 11;
          if (ran < (next_random & 0xFFFF) / (real)65536) continue;
        }
        sen[sentence_length] = word;
        sentence_length++;
        if (sentence_length >= MAX_SENTENCE_LENGTH) break;
      }
      sentence_position = 0;
    }
    if (feof(fi) || (word_count > train_words / num_threads)) {
      word_count_actual += word_count - last_word_count;
      local_iter--;
      if (local_iter == 0) break;
      word_count = 0;
      last_word_count = 0;
      sentence_length = 0;
      fseek(fi, file_size / (long long)num_threads * (long long)id, SEEK_SET);
      continue;
    }
    word = sen[sentence_position];
    if (word == -1) continue;
    for (c = 0; c < layer1_size; c++) neu1[c] = 0;
    for (c = 0; c < layer1_size; c++) neu1e[c] = 0;
    next_random = next_random * (unsigned long long)25214903917 + 11;
    b = next_random % window;
    if (cbow) {  //train the cbow architecture
      // in -> hidden
      // [S] : cw - Stands for whole context window size ?
      cw = 0;
      for (a = b; a < window * 2 + 1 - b; a++) if (a != window) {
        c = sentence_position - window + a;
        if (c < 0) continue;
        if (c >= sentence_length) continue;
        last_word = sen[c];
        if (last_word == -1) continue;
        for (c = 0; c < layer1_size; c++) neu1[c] += syn0[c + last_word * layer1_size];
        cw++;
      }
      if (cw) {
        for (c = 0; c < layer1_size; c++) neu1[c] /= cw;
        if (hs) for (d = 0; d < vocab[word].codelen; d++) {
          f = 0;
          l2 = vocab[word].point[d] * layer1_size;
          // Propagate hidden -> output
          for (c = 0; c < layer1_size; c++) f += neu1[c] * syn1[c + l2];
          if (f <= -MAX_EXP) continue;
          else if (f >= MAX_EXP) continue;
          else f = expTable[(int)((f + MAX_EXP) * (EXP_TABLE_SIZE / MAX_EXP / 2))];
          // 'g' is the gradient multiplied by the learning rate
          g = (1 - vocab[word].code[d] - f) * alpha;
          // Propagate errors output -> hidden
          for (c = 0; c < layer1_size; c++) neu1e[c] += g * syn1[c + l2];
          // Learn weights hidden -> output
          for (c = 0; c < layer1_size; c++) syn1[c + l2] += g * neu1[c];
        }
        // NEGATIVE SAMPLING
        if (negative > 0) for (d = 0; d < negative + 1; d++) {
          if (d == 0) {
            target = word;
            label = 1;
          } else {
            next_random = next_random * (unsigned long long)25214903917 + 11;
            target = table[(next_random >> 16) % table_size];
            if (target == 0) target = next_random % (vocab_size - 1) + 1;
            if (target == word) continue;
            label = 0;
          }
          l2 = target * layer1_size;
          f = 0;
          for (c = 0; c < layer1_size; c++) f += neu1[c] * syn1neg[c + l2];
          if (f > MAX_EXP) g = (label - 1) * alpha;
          else if (f < -MAX_EXP) g = (label - 0) * alpha;
          else g = (label - expTable[(int)((f + MAX_EXP) * (EXP_TABLE_SIZE / MAX_EXP / 2))]) * alpha;
          for (c = 0; c < layer1_size; c++) neu1e[c] += g * syn1neg[c + l2];
          for (c = 0; c < layer1_size; c++) syn1neg[c + l2] += g * neu1[c];
        }
        // hidden -> in
        for (a = b; a < window * 2 + 1 - b; a++) if (a != window) {
          c = sentence_position - window + a;
          if (c < 0) continue;
          if (c >= sentence_length) continue;
          last_word = sen[c];
          if (last_word == -1) continue;
          for (c = 0; c < layer1_size; c++) syn0[c + last_word * layer1_size] += neu1e[c];
        }
      }
    } else {  //train skip-gram
      for (a = b; a < window * 2 + 1 - b; a++) if (a != window) {
        c = sentence_position - window + a;
        if (c < 0) continue;
        if (c >= sentence_length) continue;
        last_word = sen[c];
        if (last_word == -1) continue;
        // [S] : l1 - position in the layer one weights vector
        // What do neu1e and neu1 contain ? 
        l1 = last_word * layer1_size;
        for (c = 0; c < layer1_size; c++) neu1e[c] = 0;
        // HIERARCHICAL SOFTMAX
        if (hs) for (d = 0; d < vocab[word].codelen; d++) {
          // [S] : f evaluates the dot product of inner representation of the context word 
          // and representation of the internal node
          f = 0;
          l2 = vocab[word].point[d] * layer1_size;
          // Propagate hidden -> output
          for (c = 0; c < layer1_size; c++) f += syn0[c + l1] * syn1[c + l2];
          if (f <= -MAX_EXP) continue;
          else if (f >= MAX_EXP) continue;
          else f = expTable[(int)((f + MAX_EXP) * (EXP_TABLE_SIZE / MAX_EXP / 2))];
          // 'g' is the gradient multiplied by the learning rate
          g = (1 - vocab[word].code[d] - f) * alpha;
          // Propagate errors output -> hidden
          for (c = 0; c < layer1_size; c++) neu1e[c] += g * syn1[c + l2];
          // Learn weights hidden -> output
          for (c = 0; c < layer1_size; c++) syn1[c + l2] += g * syn0[c + l1];
        }
        // NEGATIVE SAMPLING
        if (negative > 0) for (d = 0; d < negative + 1; d++) {
          if (d == 0) {
            target = word;
            label = 1;
          } else {
            next_random = next_random * (unsigned long long)25214903917 + 11;
            target = table[(next_random >> 16) % table_size];
            if (target == 0) target = next_random % (vocab_size - 1) + 1;
            if (target == word) continue;
            label = 0;
          }
          l2 = target * layer1_size;
          f = 0;
          for (c = 0; c < layer1_size; c++) f += syn0[c + l1] * syn1neg[c + l2];
          if (f > MAX_EXP) g = (label - 1) * alpha;
          else if (f < -MAX_EXP) g = (label - 0) * alpha;
          else g = (label - expTable[(int)((f + MAX_EXP) * (EXP_TABLE_SIZE / MAX_EXP / 2))]) * alpha;
          for (c = 0; c < layer1_size; c++) neu1e[c] += g * syn1neg[c + l2];
          for (c = 0; c < layer1_size; c++) syn1neg[c + l2] += g * syn0[c + l1];
        }
        // Learn weights input -> hidden
        for (c = 0; c < layer1_size; c++) syn0[c + l1] += neu1e[c];
      }
    }
    sentence_position++;
    if (sentence_position >= sentence_length) {
      sentence_length = 0;
      continue;
    }
  }
  fclose(fi);
  free(neu1);
  free(neu1e);
  pthread_exit(NULL);
}

///////////////////////////////////////////////////////////////////////////////
// [S] : Most of the changes are made here
// TODO:
// 1. Train the word2vec through coco
// 2. Read the training instance and corresponding P,R,S and cluster id (done)
// 3. Finding the word indices of the P,R,S words (alternative, if not found) (done)
// 4. Change the last layer to include the number of clusters as last layer
// 5. Derive equations for loss
// 6. Run the system for N = 10
// 7. Get clustering into C code for avoiding writing into files
//****************************************************************************

// Function for common sense task
void commonSenseWrapper(){
    // Rename clusterArg in current function
    int clusterArg = clusterCommonSense;

    // Load the word2vec embeddings from Xiao's
    //char wordPath[] = "/home/satwik/VisualWord2Vec/word2vecVisual/modelsNdata/al_vectors.txt";
    char wordPath[] = "modelsNdata/vis-genome/word2vec_genome_train.bin";
    //char wordPath[] = "/home/satwik/VisualWord2Vec/models/wiki_embeddings.bin";
    //char wordPath[] = "/home/satwik/VisualWord2Vec/data/coco-cnn/word2vec_coco_caption_before.bin";
    loadWord2Vec(wordPath);

    // [S] added
    char* visualPath = (char*) malloc(sizeof(char) * 100);
    char* postPath = (char*) malloc(sizeof(char) * 100);
    char* prePath = (char*) malloc(sizeof(char) * 100);
    char* vocabPath = (char*) malloc(sizeof(char) * 100);
    char* embedDumpPath = (char*) malloc(sizeof(char) * 100);
    char* featurePathICCV = (char*) malloc(sizeof(char) * 100);
    char* featurePathCOCO = (char*) malloc(sizeof(char) * 100);
    char* featurePathVQA = (char*) malloc(sizeof(char) * 100);

    // Common sense task
    // Reading the file for relation word
    featurePathVQA = "/home/satwik/VisualWord2Vec/data/vqa/vqa_psr_features.txt";
    //featurePathCOCO = "/home/satwik/VisualWord2Vec/data/coco-cnn/PSR_features_coco.txt";
    featurePathICCV = "/home/satwik/VisualWord2Vec/data/PSR_features.txt";
    //char featurePath[] = "/home/satwik/VisualWord2Vec/data/PSR_features.txt";
    //char featurePath[] = "/home/satwik/VisualWord2Vec/data/PSR_features_lemma.txt";
    //char featurePath[] = "/home/satwik/VisualWord2Vec/data/PSR_features_18.txt";
    //char featurePath[] = "/home/satwik/VisualWord2Vec/data/PSR_features_R_120.txt";

    //char featurePath[] = "/home/satwik/VisualWord2Vec/data/vp_train_sentences_lemma.txt";

    //char clusterPath[] = "/home/satwik/VisualWord2Vec/code/clustering/clusters_10.txt";
    sprintf(postPath, "/home/satwik/VisualWord2Vec/word2vecVisual/modelsNdata/word2vec_wiki_post_%d_%d_%d_%d.txt", 
                                        trainPhrases, usePCA, trainMulti, clusterArg);
    sprintf(prePath, "/home/satwik/VisualWord2Vec/word2vecVisual/modelsNdata/word2vec_wiki_pre_%d_%d_%d_%d.txt", 
                                        trainPhrases, usePCA, trainMulti, clusterArg);
    sprintf(vocabPath, "/home/satwik/VisualWord2Vec/word2vecVisual/modelsNdata/word2vec_vocab_%d_%d_%d_%d.txt",
                                        trainPhrases, usePCA, trainMulti, clusterArg);
    char testFile[] = "/home/satwik/VisualWord2Vec/data/test_features.txt";
    char valFile[] = "/home/satwik/VisualWord2Vec/data/val_features.txt";

    if(usePCA)
        visualPath = "/home/satwik/VisualWord2Vec/data/pca_features.txt";
    else{
        //visualPath = "/home/satwik/VisualWord2Vec/data/float_features_18.txt";
        //visualPath = "/home/satwik/VisualWord2Vec/data/coco-cnn/float_features_coco.txt";
        //visualPath = "/home/satwik/VisualWord2Vec/data/vqa/vqa_float_features.txt";
        visualPath = "/home/satwik/VisualWord2Vec/data/float_features.txt";
        //visualPath = "/home/satwik/VisualWord2Vec/data/float_features_R_120.txt";
    }

    // Writing word2vec from file
    //char wordPath[] = "/home/satwik/VisualWord2Vec/code/word2vecVisual/modelsNdata/word2vec_save.txt";
    //saveWord2Vec(wordPath);

    // Initializing the hash
    initFeatureHash();
    // Reading for the word features, cluster ids and visual features
    // clusterid reading will be avoided when clustering is ported to c
    readRefineTrainFeatureFiles(featurePathICCV, NULL);
    
    // reading cluster files from matlab
    //char clusterpath[] = "/home/satwik/visualword2vec/data/coco-cnn/cluster_100_coco_train.txt";
    //readclusteridfile(clusterpath);
    // Clustering in C
    noClusters = 0;
    readVisualFeatureFile(visualPath);
    char clusterSavePath[] = "/home/satwik/VisualWord2Vec/word2vecVisual/modelsNdata/cluster_id_save.txt";
    // To save clusterId / distance, provide save path; else NULL
    clusterVisualFeatures(clusterArg, NULL);
    //gmmVisualFeatures(clusterArg, NULL);
    //return;
    
    // Read the validation and test sets    
    if(noTest == 0)
        // Clean the strings for test and validation sets, store features
        readTestValFiles(valFile, testFile);

    // Saving the feature word vocabulary(split simply means the corresponding components)
    //saveFeatureWordVocab(vocabPath);
    //char splitPath[] = "/home/satwik/VisualWord2Vec/code/word2vecVisual/modelsNdata/split_vocab.txt";  
    //saveFeatureWordVocabSplit(splitPath);
    // Saving the feature vocabulary
    //saveFeatureWordVocab(vocabPath);
    
    // Store the basemodel test tuple scores and best model test tuple scores
    float* baseTestScores = (float*) malloc(sizeof(float) * noTest);
    float* bestTestScores = (float*) malloc(sizeof(float) * noTest);

    if(trainMulti){
        // Initializing the refining network
        initMultiRefining();
        // Performing the multi model common sense task
        performMultiCommonSenseTask(baseTestScores);
    }
    else{
        // Initializing the refining network
        //initRefiningRegress();
        initRefining();
        // Perform common sense task
        performCommonSenseTask(baseTestScores);
    }

    // Saving the embeddings, before refining
    /*if(trainMulti)
        saveMultiEmbeddings(prePath);
    else
        saveEmbeddings(prePath);*/

    // Reset valAccuracy as the first run doesnt count
    prevValAcc = 0; 
    prevTestAcc = 0;

    printf("\n\n (PCA, phrases, multi, noClusters) = (%d, %d, %d, %d)\n\n", 
                                        usePCA, trainPhrases, trainMulti, clusterArg);
    
    int noOverfit = 1;
    int iter = 0;

    // Debugging for regressing visual features
    /*while(noOverfit){
        // Refine the network
        refineNetwork();
        //refineNetworkRegress();
    
        // Perform the common sense task 
        noOverfit = performCommonSenseTask(bestTestScores);
    }*/

    while(noOverfit){
        // Refine the network for multi model
        if(trainMulti){
            if(trainPhrases)
                refineMultiNetworkPhrase();
            else
                refineMultiNetwork();
        }
        // Refine the network
        else{
            if(trainPhrases)
                refineNetworkPhrase();
            else
                refineNetwork();
        }

        // Saving the embeddings snapshots
        /*sprintf(embedDumpPath, "/home/satwik/VisualWord2Vec/code/word2vecVisual/modelsNdata/word2vec_wiki_iter_%d.bin",
                                            iter);
        saveWord2Vec(embedDumpPath);
        iter++;*/
        
        if(trainMulti)
            // Performing the multi model common sense task
            //noOverfit = performMultiCommonSenseTask(NULL);
            noOverfit = performMultiCommonSenseTask(bestTestScores);
        else
            // Perform common sense task
            //noOverfit = performCommonSenseTask(NULL);
            noOverfit = performCommonSenseTask(bestTestScores);
    }

    // Saving the embeddings, after refining
    /*if(trainMulti)
        saveMultiEmbeddings(postPath);
    else
        saveEmbeddings(postPath);*/

    // Find test tuples with best improvement, for further visualization
    //findBestTestTuple(baseTestScores, bestTestScores);
}

// Function for visual paraphrase task
void visualParaphraseWrapper(){
    int clusterArg = clusterVP;
    // Read the embeddings from the file
    //char embedFile[] = "modelsNdata/word2vec_vp_lemma_50hidden.bin";
    //char embedFile[] = "modelsNdata/word2vec_vp_lemma_100hidden.bin";
    //char embedFile[] = "modelsNdata/wiki_vp_before_100.bin";
    //char embedFile[] = "modelsNdata/wiki_vp_before_50.bin";
    char embedFile[] = "modelsNdata/vp/word2vec_coco_vp_lemma.bin";
    loadWord2Vec(embedFile);

    // Reading the file for training
    char* visualPath = (char*) malloc(sizeof(char) * 100);
    char* featurePath = (char*) malloc(sizeof(char) * 100);

    if(debugModeVP)
        featurePath = "/home/satwik/VisualWord2Vec/data/vp_train_debug.txt";
    else
        featurePath = "/home/satwik/VisualWord2Vec/data/vp_train_full.txt";

    if(usePCA)
        visualPath = "/home/satwik/VisualWord2Vec/data/abstract_features_train_pca.txt";
    else{
        if(debugModeVP)
            visualPath = "/home/satwik/VisualWord2Vec/data/abstract_features_debug.txt";
        else
            visualPath = "/home/satwik/VisualWord2Vec/data/abstract_features_train.txt";
    }

    // Reading for the word features and visual features
    readVPTrainSentences(featurePath);
    readVPAbstractVisualFeatures(visualPath);
    
    // Tokenizing the training sentences
    tokenizeTrainSentences();
    
    // Compute embeddings
    performVPTask();

    // Clustering the visual features
    if(debugModeVP)
        clusterVPAbstractVisualFeatures(2, NULL);
    else
        clusterVPAbstractVisualFeatures(clusterArg, NULL);

    // Begin the refining
    int i, noIters = 100;
    if(debugModeVP)
        noIters = 1;
    else
        noIters = 100;
    
    // Initializing the refining network
    initRefining();

    for(i = 0; i < noIters; i++){
        printf("Refining : %d / %d\n", i, noIters);

        // Refining the embeddings
        refineNetworkVP();
        
        // Compute embeddings
        performVPTask();
    }

    // Save the refined word2vec features for the VP sentences
    //writeVPSentenceEmbeddings(); 
}

// Function for training from ms coco dataset
void mscocoWrapper(){
    // Cluster argument assignment
    int clusterArg = clusterCOCO;
    // Load the embeddings (pre-trained) to save time
    //char beforeEmbedPath[] = "/home/satwik/VisualWord2Vec/data/coco-cnn/word2vec_coco_caption_before.bin";
    // Load the word2vec embeddings from Xiao's
    char beforeEmbedPath[] = "/home/satwik/VisualWord2Vec/code/word2vecVisual/modelsNdata/al_vectors.txt";
    loadWord2Vec(beforeEmbedPath);

    ////////// Dirty work of setting up paths//////////////////////////////////////////////////////////
    // [S] added
    char* visualPath = (char*) malloc(sizeof(char) * 100);
    char* postPath = (char*) malloc(sizeof(char) * 100);
    char* prePath = (char*) malloc(sizeof(char) * 100);
    char* vocabPath = (char*) malloc(sizeof(char) * 100);
    char* embedDumpPath = (char*) malloc(sizeof(char) * 100);
    char* mapPath = (char*) malloc(sizeof(char) * 100);

    // Common sense task
    // Reading the file for relation word
    char featurePath[] = "/home/satwik/VisualWord2Vec/data/PSR_features.txt";
    //char featurePath[] = "/home/satwik/VisualWord2Vec/data/PSR_features_lemma.txt";
    //char featurePath[] = "/home/satwik/VisualWord2Vec/data/PSR_features_18.txt";
    //char featurePath[] = "/home/satwik/VisualWord2Vec/data/PSR_features_R_120.txt";

    //char featurePath[] = "/home/satwik/VisualWord2Vec/data/vp_train_sentences_lemma.txt";

    sprintf(postPath, "/home/satwik/VisualWord2Vec/code/word2vecVisual/modelsNdata/word2vec_wiki_post_%d_%d_%d_%d.txt", 
                                        trainPhrases, usePCA, trainMulti, clusterArg);
    sprintf(prePath, "/home/satwik/VisualWord2Vec/code/word2vecVisual/modelsNdata/word2vec_wiki_pre_%d_%d_%d_%d.txt", 
                                        trainPhrases, usePCA, trainMulti, clusterArg);
    sprintf(vocabPath, "/home/satwik/VisualWord2Vec/code/word2vecVisual/modelsNdata/word2vec_vocab_%d_%d_%d_%d.txt",
                                        trainPhrases, usePCA, trainMulti, clusterArg);
    char testFile[] = "/home/satwik/VisualWord2Vec/data/test_features.txt";
    char valFile[] = "/home/satwik/VisualWord2Vec/data/val_features.txt";

    if(usePCA){
        visualPath = "/home/satwik/VisualWord2Vec/data/coco-cnn/fc7_features_train_pca.txt";
        mapPath = "/home/satwik/VisualWord2Vec/data/coco-cnn/captions_coco_train_map.txt";
    }
    else{
        //visualPath = "/home/satwik/VisualWord2Vec/data/coco-cnn/fc7_features_debug.txt";
        visualPath = "/home/satwik/VisualWord2Vec/data/coco-cnn/fc7_features_train.txt";
        mapPath = "/home/satwik/VisualWord2Vec/data/coco-cnn/captions_coco_train_map.txt";
    }

    // Paths for train sentences and their cluster ids for COCO captions
    //char clusterPath[] = "/home/satwik/VisualWord2Vec/data/coco-cnn/cluster_100_coco_train.txt";
    //char trainPath[] = "/home/satwik/VisualWord2Vec/data/coco-cnn/captions_coco_lemma_debug.txt";
    char trainPath[] = "/home/satwik/VisualWord2Vec/data/coco-cnn/captions_coco_train_lemma_nomaps.txt";
    ///////////////////////////////////////////////////////////////////////////////////////////////////
    
    // Initializing the hash
    initFeatureHash();
    // Reading for the word features, cluster ids and visual features
    readRefineTrainFeatureFiles(featurePath, NULL);
    
    // Reading cluster file for ms coco
    readTrainSentencesCOCO(trainPath, mapPath);

    // Read the features and cluster to get the ids
    readVisualFeatureFileCOCO(visualPath);
    
    char* clusterPath = (char*) malloc(sizeof(char) * 100);
    if(usePCA)
        sprintf(clusterPath, "/home/satwik/VisualWord2Vec/data/coco-cnn/C_pca_cluster_%d.txt",
                                clusterArg);
    else
        sprintf(clusterPath, "/home/satwik/VisualWord2Vec/data/coco-cnn/C_cluster_%d.txt",
                                clusterArg);

    // Check if cluster file exists, else cluster
    if( access(clusterPath, F_OK) != -1){
        // Reading the cluster ids
        readClusterIdCOCO(clusterPath);
    }
    else{
        // To save clusterId / distance, provide save path; else NULL
        clusterVisualFeaturesCOCO(clusterArg, clusterPath);
    }
                        
    // Tokenizing the files
    tokenizeTrainSentencesCOCO();

    // Read the validation and test sets    
    if(noTest == 0)
        // Clean the strings for test and validation sets, store features
        readTestValFiles(valFile, testFile);

    // Store the basemodel test tuple scores and best model test tuple scores
    float* baseTestScores = (float*) malloc(sizeof(float) * noTest);
    float* bestTestScores = (float*) malloc(sizeof(float) * noTest);

    // Refine the embeddings (single only)
    // Initializing the refining network
    initRefining();
    // Perform common sense task
    performCommonSenseTask(baseTestScores);

    // Reset valAccuracy as the first run doesnt count
    prevValAcc = 0; 
    prevTestAcc = 0;

    printf("\n\n (PCA, phrases, multi, noClusters) = (%d, %d, %d, %d)\n\n", 
                                        usePCA, trainPhrases, trainMulti, clusterArg);
    
    int noOverfit = 1;
    int iter = 0;
    while(noOverfit){
        // Refine the network
        refineNetworkCOCO();
        // Perform common sense task
        noOverfit = performCommonSenseTask(bestTestScores);
    }
}

// Function for training from vqa dataset
void vqaWrapper(){
    // Cluster argument assignment
    int clusterArg = clusterVQA;
    // Load the embeddings (pre-trained) to save time
    //char beforeEmbedPath[] = "/home/satwik/VisualWord2Vec/data/coco-cnn/word2vec_coco_caption_before.bin";
    // Load the word2vec embeddings from Xiao's
    //char beforeEmbedPath[] = "/home/satwik/VisualWord2Vec/code/word2vecVisual/modelsNdata/al_vectors.txt";
    char beforeEmbedPath[] = "/home/satwik/VisualWord2Vec/data/vqa/word2vec_vqa_before.bin";
    loadWord2Vec(beforeEmbedPath);

    ////////// Dirty work of setting up paths//////////////////////////////////////////////////////////
    // [S] added
    char* visualPath = (char*) malloc(sizeof(char) * 100);
    char* postPath = (char*) malloc(sizeof(char) * 100);
    char* prePath = (char*) malloc(sizeof(char) * 100);
    char* vocabPath = (char*) malloc(sizeof(char) * 100);
    char* embedDumpPath = (char*) malloc(sizeof(char) * 100);
    char* mapPath = (char*) malloc(sizeof(char) * 100);
    char* trainPath = (char*) malloc(sizeof(char) * 100);

    // Common sense task
    // Reading the file for relation word
    char featurePath[] = "/home/satwik/VisualWord2Vec/data/PSR_features.txt";
    //char featurePath[] = "/home/satwik/VisualWord2Vec/data/PSR_features_lemma.txt";
    //char featurePath[] = "/home/satwik/VisualWord2Vec/data/PSR_features_18.txt";
    //char featurePath[] = "/home/satwik/VisualWord2Vec/data/PSR_features_R_120.txt";

    //char featurePath[] = "/home/satwik/VisualWord2Vec/data/vp_train_sentences_lemma.txt";

    sprintf(postPath, "/home/satwik/VisualWord2Vec/code/word2vecVisual/modelsNdata/word2vec_wiki_post_%d_%d_%d_%d.txt", 
                                        trainPhrases, usePCA, trainMulti, clusterArg);
    sprintf(prePath, "/home/satwik/VisualWord2Vec/code/word2vecVisual/modelsNdata/word2vec_wiki_pre_%d_%d_%d_%d.txt", 
                                        trainPhrases, usePCA, trainMulti, clusterArg);
    sprintf(vocabPath, "/home/satwik/VisualWord2Vec/code/word2vecVisual/modelsNdata/word2vec_vocab_%d_%d_%d_%d.txt",
                                        trainPhrases, usePCA, trainMulti, clusterArg);
    char testFile[] = "/home/satwik/VisualWord2Vec/data/test_features.txt";
    char valFile[] = "/home/satwik/VisualWord2Vec/data/val_features.txt";

    if(debugModeVQA){
        visualPath = "/home/satwik/VisualWord2Vec/data/vqa/float_features_vqa_debug.txt";
        mapPath = "/home/satwik/VisualWord2Vec/data/vqa/vqa_feature_map_debug.txt";
    }
    else{
        if(usePCA)
            visualPath = "/home/satwik/VisualWord2Vec/data/vqa/float_features_yash_pca.txt";
            //visualPath = "/home/satwik/VisualWord2Vec/data/vqa/float_features_vqa_pca.txt";
        else
            visualPath = "/home/satwik/VisualWord2Vec/data/vqa/float_features_yash.txt";
            //visualPath = "/home/satwik/VisualWord2Vec/data/vqa/float_features_vqa.txt";
        mapPath = "/home/satwik/VisualWord2Vec/data/vqa/vqa_feature_map.txt";
    }

    // Paths for train sentences and their cluster ids for VQA captions
    if(debugModeVQA)
        trainPath = "/home/satwik/VisualWord2Vec/data/vqa/vqa_train_captions_debug.txt";
    else
        trainPath = "/home/satwik/VisualWord2Vec/data/vqa/vqa_train_captions_lemma.txt";

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    
    // Initializing the hash
    initFeatureHash();
    // Reading for the word features, cluster ids and visual features
    readRefineTrainFeatureFiles(featurePath, NULL);
    
    // Reading cluster file for ms coco
    readTrainSentencesVQA(trainPath, mapPath);

    // Read the features and cluster to get the ids
    readVisualFeatureFileVQA(visualPath);
    
    char* clusterPath = (char*) malloc(sizeof(char) * 100);
    if(usePCA)
        sprintf(clusterPath, "/home/satwik/VisualWord2Vec/data/vqa/C_cluster_pca_%d.txt",
                                clusterArg);
    else
        sprintf(clusterPath, "/home/satwik/VisualWord2Vec/data/vqa/C_cluster_%d.txt",
                                clusterArg);
    // Check if cluster file exists, else cluster
    if( access(clusterPath, F_OK) != -1 && !debugModeVQA){
        // Reading the cluster ids
        readClusterIdVQA(clusterPath);
    }
    else{
        // To save clusterId / distance, provide save path; else NULL
        if(debugModeVQA)
            clusterVisualFeaturesVQA(2, NULL);
        else
            clusterVisualFeaturesVQA(clusterArg, clusterPath);
    }
                        
    // Tokenizing the files
    tokenizeTrainSentencesVQA();

    // Read the validation and test sets    
    if(noTest == 0)
        // Clean the strings for test and validation sets, store features
        readTestValFiles(valFile, testFile);

    // Store the basemodel test tuple scores and best model test tuple scores
    float* baseTestScores = (float*) malloc(sizeof(float) * noTest);
    float* bestTestScores = (float*) malloc(sizeof(float) * noTest);

    // Refine the embeddings (single only)
    // Initializing the refining network
    initRefining();
    // Perform common sense task
    performCommonSenseTask(baseTestScores);

    // Reset valAccuracy as the first run doesnt count
    prevValAcc = 0; 
    prevTestAcc = 0;

    printf("\n\n (PCA, phrases, multi, noClusters) = (%d, %d, %d, %d)\n\n", 
                                        usePCA, trainPhrases, trainMulti, clusterArg);
    
    int noOverfit = 1;
    int iter = 0;
    while(noOverfit){
        // Refine the network
        refineNetworkVQA();
        // Perform common sense task
        noOverfit = performCommonSenseTask(bestTestScores);
    }
}

// Function for training using visual-genome dataset
void visualGenomeWrapper(){
    int clusterArg = clusterGenome;
    // Read the embeddings from the file
    char embedFile[] = "modelsNdata/vis-genome/word2vec_genome_train.bin";
    loadWord2Vec(embedFile);

    // Reading the file for training
    char* visualPath = (char*) malloc(sizeof(char) * 100);
    char* featurePath = (char*) malloc(sizeof(char) * 100);

    // Either debug mode or full run mode
    if(debugModeGenome){
        featurePath = "/home/satwik/VisualWord2Vec/data/vis-genome/train/text_debug";
        visualPath = "/home/satwik/VisualWord2Vec/data/vis-genome/train/vis_debug";
    }
    else{
        //featurePath = "/home/satwik/VisualWord2Vec/data/vis-genome/train/text_debug_small";
        //featurePath = "/home/satwik/VisualWord2Vec/data/vis-genome/train/text_debug_big";
        featurePath = "/home/satwik/VisualWord2Vec/data/vis-genome/train/text_features_00";

        // Use PCA
        if (usePCA)
            visualPath = "/home/satwik/VisualWord2Vec/data/abstract_features_train_pca.txt";
        else
            //visualPath = "/home/satwik/VisualWord2Vec/data/vis-genome/train/vis_debug_small";
            //visualPath = "/home/satwik/VisualWord2Vec/data/vis-genome/train/vis_debug_big";
            visualPath = "/home/satwik/VisualWord2Vec/data/vis-genome/train/vis_features_header";
    }

    // Test and validation sets for the common sense task
    char tuplePath[] = "/home/satwik/VisualWord2Vec/data/PSR_features.txt";
    char testFile[] = "/home/satwik/VisualWord2Vec/data/test_features.txt";
    char valFile[] = "/home/satwik/VisualWord2Vec/data/val_features.txt";
    
    // Initializing the hash
    initFeatureHash();
    // Reading for the word features, cluster ids and visual features
    readRefineTrainFeatureFiles(tuplePath, NULL);

    // Reading for the word features and visual features
    readTrainSentencesGenome(featurePath);
    readVisualFeatureFileGenome(visualPath);
    
    // Tokenizing the training sentences
    tokenizeTrainSentencesGenome();
    
    char* clusterPath = (char*) malloc(sizeof(char) * 100);
    if(usePCA)
        sprintf(clusterPath, "/home/satwik/VisualWord2Vec/data/vis-genome/C_pca_cluster_%d.txt",
                                clusterArg);
    else
        sprintf(clusterPath, "/home/satwik/VisualWord2Vec/data/vis-genome/C_cluster_%d.txt",
                                clusterArg);

    // Check if cluster file exists, else cluster
    if( access(clusterPath, F_OK) != -1){
        // Reading the cluster ids
        readClusterIdGenome(clusterPath);
    }
    else{
        // To save clusterId / distance, provide save path; else NULL
        clusterVisualFeaturesGenome(clusterArg, clusterPath);
    }

    // Clustering the visual features
    /*if(debugModeGenome)
        clusterVisualFeaturesGenome(2, NULL);
    else
        clusterVisualFeaturesGenome(clusterArg, NULL);*/

    // Begin the refining, based on the cross validation performance
    // Initializing the refining network
    initRefining();

    // Read the validation and test sets    
    if(noTest == 0)
        // Clean the strings for test and validation sets, store features
        readTestValFiles(valFile, testFile);

    // Store the basemodel test tuple scores and best model test tuple scores
    float* baseTestScores = (float*) malloc(sizeof(float) * noTest);
    float* bestTestScores = (float*) malloc(sizeof(float) * noTest);

    // Refine the embeddings (single only)
    // Initializing the refining network
    initRefining();
    // Perform common sense task
    performCommonSenseTask(baseTestScores);

    // Reset valAccuracy as the first run doesnt count
    prevValAcc = 0; 
    prevTestAcc = 0;

    printf("\n\n (PCA, phrases, multi, noClusters) = (%d, %d, %d, %d)\n\n", 
                                        usePCA, trainPhrases, trainMulti, clusterArg);
    
    int noOverfit = 1;
    while(noOverfit){
        // Refine the network
        refineNetworkGenome();
        // Perform common sense task
        noOverfit = performCommonSenseTask(bestTestScores);
    }
}

///////////////////////////////////////////////////////////////////////////
void TrainModel() {
    long a, b, c, d;
    FILE *fo;
    pthread_t *pt = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
    printf("Starting training using file %s\n", train_file);
    starting_alpha = alpha;
    if (read_vocab_file[0] != 0) ReadVocab(); else LearnVocabFromTrainFile();
    if (save_vocab_file[0] != 0) SaveVocab();
    if (output_file[0] == 0) return;
    InitNet();
    // [S] : Create a unigram distribution table for negative sampling
    if (negative > 0) InitUnigramTable();
    start = clock();
    // [S] : Creates the threads for execution
    //for (a = 0; a < num_threads; a++) pthread_create(&pt[a], NULL, TrainModelThread, (void *)a);
    // [S] : Waits for the completion of execution of the threads
    //for (a = 0; a < num_threads; a++) pthread_join(pt[a], NULL);

    // Save the embeddings before refining 
    //char beforeEmbedPath[] = "/home/satwik/VisualWord2Vec/models/wiki_embeddings.bin";
    //char beforeEmbedPath[] = "/home/satwik/VisualWord2Vec/models/wiki_embeddings_100.bin";
    //char beforeEmbedPath[] = "/home/satwik/VisualWord2Vec/models/wiki_embeddings_50.bin";
    //char beforeEmbedPath[] = "/home/satwik/VisualWord2Vec/models/wiki_embeddings_pre_refine.bin";
    //char beforeEmbedPath[] = "modelsNdata/word2vec_vp_lemma.bin";
    //char beforeEmbedPath[] = "modelsNdata/mscoco_before.bin";

    //char beforeEmbedPath[] = "/home/satwik/VisualWord2Vec/data/coco-cnn/word2vec_coco_caption_before.bin";
    //char beforeEmbedPath[] = "/home/satwik/VisualWord2Vec/data/vqa/word2vec_vqa_before.bin";
    //char beforeEmbedPath[] = "/home/satwik/VisualWord2Vec/data/vqa/word2vec_vqa_train_captions.bin";
    //loadWord2Vec(beforeEmbedPath);
    //saveWord2Vec(beforeEmbedPath);
    //***************************************************************************************
    
    // Visual paraphrase task
    //visualParaphraseWrapper();

    // Training from MS COCO
    //mscocoWrapper();

    // Training from VQA abstract 
    //vqaWrapper();

    // Marking the change
    //printf("\nChange over!\n");
    
    // Common sense task
    //commonSenseWrapper();

    // Visual genome task
    visualGenomeWrapper();
    return;

    //***************************************************************************************
    /***************************************************************************************/
    // skip writing to the file
    /***************************************************************************************/
    // Write the three models separately (P,R,S)
    // P 
    /*char outputP[] = "/home/satwik/VisualWord2Vec/models/p_wiki_model.txt";
    fo = fopen(outputP, "wb");
    syn0 = syn0P;
    // Save the word vectors
    fprintf(fo, "%lld %lld\n", vocab_size, layer1_size);
    for (a = 0; a < vocab_size; a++) {
      fprintf(fo, "%s ", vocab[a].word);
      if (binary) for (b = 0; b < layer1_size; b++) fwrite(&syn0[a * layer1_size + b], sizeof(real), 1, fo);
      else for (b = 0; b < layer1_size; b++) fprintf(fo, "%lf ", syn0[a * layer1_size + b]);
      fprintf(fo, "\n");
    }
    fclose(fo);
    
    // R
    char outputR[] = "/home/satwik/VisualWord2Vec/models/r_wiki_model.txt";
    fo = fopen(outputR, "wb");
    syn0 = syn0R;
    // Save the word vectors
    fprintf(fo, "%lld %lld\n", vocab_size, layer1_size);
    for (a = 0; a < vocab_size; a++) {
      fprintf(fo, "%s ", vocab[a].word);
      if (binary) for (b = 0; b < layer1_size; b++) fwrite(&syn0[a * layer1_size + b], sizeof(real), 1, fo);
      else for (b = 0; b < layer1_size; b++) fprintf(fo, "%lf ", syn0[a * layer1_size + b]);
      fprintf(fo, "\n");
    }
    fclose(fo);

    // S
    char outputS[] = "/home/satwik/VisualWord2Vec/models/s_wiki_model.txt";
    fo = fopen(outputS, "wb");
    syn0 = syn0S;
    // Save the word vectors
    fprintf(fo, "%lld %lld\n", vocab_size, layer1_size);
    for (a = 0; a < vocab_size; a++) {
      fprintf(fo, "%s ", vocab[a].word);
      if (binary) for (b = 0; b < layer1_size; b++) fwrite(&syn0[a * layer1_size + b], sizeof(real), 1, fo);
      else for (b = 0; b < layer1_size; b++) fprintf(fo, "%lf ", syn0[a * layer1_size + b]);
      fprintf(fo, "\n");
    }
    fclose(fo);
    return;*/
    /***************************************************************************************/
    
    fo = fopen(output_file, "wb");
    if (classes == 0) {
    // Save the word vectors
    fprintf(fo, "%lld %lld\n", vocab_size, layer1_size);
    for (a = 0; a < vocab_size; a++) {
      fprintf(fo, "%s ", vocab[a].word);
      if (binary) for (b = 0; b < layer1_size; b++) fwrite(&syn0[a * layer1_size + b], sizeof(real), 1, fo);
      else for (b = 0; b < layer1_size; b++) fprintf(fo, "%lf ", syn0[a * layer1_size + b]);
      fprintf(fo, "\n");
    }
    } else {
    // Run K-means on the word vectors
    int clcn = classes, iter = 10, closeid;
    int *centcn = (int *)malloc(classes * sizeof(int));
    int *cl = (int *)calloc(vocab_size, sizeof(int));
    real closev, x;
    real *cent = (real *)calloc(classes * layer1_size, sizeof(real));
    for (a = 0; a < vocab_size; a++) cl[a] = a % clcn;
    for (a = 0; a < iter; a++) {
      for (b = 0; b < clcn * layer1_size; b++) cent[b] = 0;
      for (b = 0; b < clcn; b++) centcn[b] = 1;
      for (c = 0; c < vocab_size; c++) {
        for (d = 0; d < layer1_size; d++) cent[layer1_size * cl[c] + d] += syn0[c * layer1_size + d];
        centcn[cl[c]]++;
      }
      for (b = 0; b < clcn; b++) {
        closev = 0;
        for (c = 0; c < layer1_size; c++) {
          cent[layer1_size * b + c] /= centcn[b];
          closev += cent[layer1_size * b + c] * cent[layer1_size * b + c];
        }
        closev = sqrt(closev);
        for (c = 0; c < layer1_size; c++) cent[layer1_size * b + c] /= closev;
      }
      for (c = 0; c < vocab_size; c++) {
        closev = -10;
        closeid = 0;
        for (d = 0; d < clcn; d++) {
          x = 0;
          for (b = 0; b < layer1_size; b++) x += cent[layer1_size * d + b] * syn0[c * layer1_size + b];
          if (x > closev) {
            closev = x;
            closeid = d;
          }
        }
        cl[c] = closeid;
      }
    }
    // Save the K-means classes
    for (a = 0; a < vocab_size; a++) fprintf(fo, "%s %d\n", vocab[a].word, cl[a]);
    free(centcn);
    free(cent);
    free(cl);
    }
    fclose(fo);
}

int ArgPos(char *str, int argc, char **argv) {
  int a;
  for (a = 1; a < argc; a++) if (!strcmp(str, argv[a])) {
    if (a == argc - 1) {
      printf("Argument missing for %s\n", str);
      exit(1);
    }
    return a;
  }
  return -1;
}

int main(int argc, char **argv) {
  int i;
  if (argc == 1) {
    printf("WORD VECTOR estimation toolkit v 0.1c\n\n");
    printf("Options:\n");
    printf("Parameters for training:\n");
    printf("\t-train <file>\n");
    printf("\t\tUse text data from <file> to train the model\n");
    printf("\t-output <file>\n");
    printf("\t\tUse <file> to save the resulting word vectors / word clusters\n");
    printf("\t-size <int>\n");
    printf("\t\tSet size of word vectors; default is 100\n");
    printf("\t-window <int>\n");
    printf("\t\tSet max skip length between words; default is 5\n");
    printf("\t-sample <float>\n");
    printf("\t\tSet threshold for occurrence of words. Those that appear with higher frequency in the training data\n");
    printf("\t\twill be randomly down-sampled; default is 1e-3, useful range is (0, 1e-5)\n");
    printf("\t-hs <int>\n");
    printf("\t\tUse Hierarchical Softmax; default is 0 (not used)\n");
    printf("\t-negative <int>\n");
    printf("\t\tNumber of negative examples; default is 5, common values are 3 - 10 (0 = not used)\n");
    printf("\t-threads <int>\n");
    printf("\t\tUse <int> threads (default 12)\n");
    printf("\t-iter <int>\n");
    printf("\t\tRun more training iterations (default 5)\n");
    printf("\t-min-count <int>\n");
    printf("\t\tThis will discard words that appear less than <int> times; default is 5\n");
    printf("\t-alpha <float>\n");
    printf("\t\tSet the starting learning rate; default is 0.025 for skip-gram and 0.05 for CBOW\n");
    printf("\t-classes <int>\n");
    printf("\t\tOutput word classes rather than word vectors; default number of classes is 0 (vectors are written)\n");
    printf("\t-debug <int>\n");
    printf("\t\tSet the debug mode (default = 2 = more info during training)\n");
    printf("\t-binary <int>\n");
    printf("\t\tSave the resulting vectors in binary moded; default is 0 (off)\n");
    printf("\t-save-vocab <file>\n");
    printf("\t\tThe vocabulary will be saved to <file>\n");
    printf("\t-read-vocab <file>\n");
    printf("\t\tThe vocabulary will be read from <file>, not constructed from the training data\n");
    printf("\t-cbow <int>\n");
    printf("\t\tUse the continuous bag of words model; default is 1 (use 0 for skip-gram model)\n");
    printf("\nExamples:\n");
    printf("./word2vec -train data.txt -output vec.txt -size 200 -window 5 -sample 1e-4 -negative 5 -hs 0 -binary 0 -cbow 1 -iter 3\n\n");
    return 0;
  }
  output_file[0] = 0;
  save_vocab_file[0] = 0;
  read_vocab_file[0] = 0;
  if ((i = ArgPos((char *)"-size", argc, argv)) > 0) layer1_size = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-train", argc, argv)) > 0) strcpy(train_file, argv[i + 1]);
  if ((i = ArgPos((char *)"-save-vocab", argc, argv)) > 0) strcpy(save_vocab_file, argv[i + 1]);
  if ((i = ArgPos((char *)"-read-vocab", argc, argv)) > 0) strcpy(read_vocab_file, argv[i + 1]);
  if ((i = ArgPos((char *)"-debug", argc, argv)) > 0) debug_mode = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-binary", argc, argv)) > 0) binary = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-cbow", argc, argv)) > 0) cbow = atoi(argv[i + 1]);
  if (cbow) alpha = 0.05;
  if ((i = ArgPos((char *)"-alpha", argc, argv)) > 0) alpha = atof(argv[i + 1]);
  if ((i = ArgPos((char *)"-output", argc, argv)) > 0) strcpy(output_file, argv[i + 1]);
  if ((i = ArgPos((char *)"-window", argc, argv)) > 0) window = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-sample", argc, argv)) > 0) sample = atof(argv[i + 1]);
  if ((i = ArgPos((char *)"-hs", argc, argv)) > 0) hs = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-negative", argc, argv)) > 0) negative = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-threads", argc, argv)) > 0) num_threads = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-iter", argc, argv)) > 0) iter = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-min-count", argc, argv)) > 0) min_count = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-classes", argc, argv)) > 0) classes = atoi(argv[i + 1]);
  vocab = (struct vocab_word *)calloc(vocab_max_size, sizeof(struct vocab_word));
  vocab_hash = (int *)calloc(vocab_hash_size, sizeof(int));
  expTable = (real *)malloc((EXP_TABLE_SIZE + 1) * sizeof(real));
  for (i = 0; i < EXP_TABLE_SIZE; i++) {
    expTable[i] = exp((i / (real)EXP_TABLE_SIZE * 2 - 1) * MAX_EXP); // Precompute the exp() table
    expTable[i] = expTable[i] / (expTable[i] + 1);                   // Precompute f(x) = x / (x + 1)
  }
  TrainModel();
  return 0;
}